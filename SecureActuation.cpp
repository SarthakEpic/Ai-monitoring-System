#include "SecureActuation.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <sstream>

namespace {
long long NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
bool HasUnsafeText(const std::string& value) { return value.empty() || value.size() > 128 || value.find_first_of("\\/\r\n\t") != std::string::npos; }
std::string Field(const std::string& wire, const std::string& key) {
    const std::string prefix = key + "=";
    for (std::istringstream fields(wire); fields.good();) { std::string item; std::getline(fields, item, '|'); if (item.rfind(prefix, 0) == 0) return item.substr(prefix.size()); }
    return {};
}
}

const char* ToString(ActuatorCommand command) {
    switch (command) { case ActuatorCommand::BeginCanary: return "BEGIN_CANARY"; case ActuatorCommand::Rollback: return "ROLLBACK"; case ActuatorCommand::WatchdogRestore: return "WATCHDOG_RESTORE"; }
    return "BEGIN_CANARY";
}
const char* ToString(GuardianVerdict verdict) {
    switch (verdict) { case GuardianVerdict::SafeToCanary: return "SAFE_TO_CANARY"; case GuardianVerdict::Reject: return "REJECT"; case GuardianVerdict::Unknown: return "UNKNOWN"; }
    return "UNKNOWN";
}

bool SecureMessageValidation::Validate(const SecureProtocolMessage& message, std::string& reason) {
    if (message.version != 1) { reason = "unsupported_protocol_version"; return false; }
    if (HasUnsafeText(message.requestId) || HasUnsafeText(message.sessionId) || HasUnsafeText(message.proofId)) { reason = "invalid_bounded_identity_field"; return false; }
    if (message.sequence == 0 || message.issuedAtMs <= 0 || message.expiresAtMs <= message.issuedAtMs || message.expiresAtMs - message.issuedAtMs > 300000) { reason = "invalid_sequence_or_expiry"; return false; }
    return true;
}

bool SecureMessageValidation::Parse(const std::string& wire, SecureProtocolMessage& message, std::string& reason) {
    if (wire.empty() || wire.size() > 2048) { reason = "malformed_or_oversized_message"; return false; }
    try {
        message.version = std::stoi(Field(wire, "v")); message.requestId = Field(wire, "id"); message.sessionId = Field(wire, "session");
        message.sequence = std::stoull(Field(wire, "seq")); message.issuedAtMs = std::stoll(Field(wire, "issued")); message.expiresAtMs = std::stoll(Field(wire, "expires"));
        message.proofId = Field(wire, "proof"); const auto command = Field(wire, "cmd");
        if (command == "BEGIN_CANARY") message.command = ActuatorCommand::BeginCanary;
        else if (command == "ROLLBACK") message.command = ActuatorCommand::Rollback;
        else if (command == "WATCHDOG_RESTORE") message.command = ActuatorCommand::WatchdogRestore;
        else { reason = "command_not_allowlisted"; return false; }
    } catch (...) { reason = "malformed_message_fields"; return false; }
    return Validate(message, reason);
}

bool ReplayProtection::Accept(const SecureProtocolMessage& message, long long nowMs, std::string& reason) {
    if (!SecureMessageValidation::Validate(message, reason)) return false;
    if (message.issuedAtMs > nowMs + 5000 || message.expiresAtMs <= nowMs) { reason = "stale_or_future_request"; return false; }
    static std::mutex mutex; static std::unordered_map<std::string, unsigned long long> lastSequence;
    std::lock_guard lock(mutex);
    auto& previous = lastSequence[message.sessionId];
    if (message.sequence <= previous) { reason = "replayed_or_out_of_order_request"; return false; }
    previous = message.sequence; reason = "accepted"; return true;
}

GuardianVerdict ActionGuardian::Evaluate(const GuardianInput& input) const {
    if (!input.measurementsFresh || input.protectedTarget) return GuardianVerdict::Reject;
    if (input.disagreement > 0.20 || input.oodScore > 0.25) return GuardianVerdict::Unknown;
    return input.calibratedVetoRisk <= 0.05 ? GuardianVerdict::SafeToCanary : GuardianVerdict::Reject;
}

bool TrustedSafetyController::ValidateProof(const ProofCarryingAction& proof, std::string& reason) const {
    if (!proof.need.accepted || proof.certificateId.empty() || proof.modelId.empty()) { reason = "reliability_certificate_rejected"; return false; }
    if (proof.evidence.action == ResourceActionType::None || proof.evidence.benefitLowerBound <= 0.0 || proof.evidence.harmUpperBound >= 0.05 || !proof.evidence.mechanismConsistent || !proof.evidence.guardianPassed) { reason = "action_evidence_rejected"; return false; }
    if (!proof.deterministicSafetyPassed || !proof.rollbackSnapshotComplete || proof.context.targetPid == 0 || proof.context.targetPid == proof.context.foregroundPid || proof.target.pid != proof.context.targetPid) { reason = "trusted_safety_or_identity_rejected"; return false; }
    if (!proof.context.userApproved || proof.canaryDurationMs < 100 || proof.canaryDurationMs > 30000 || proof.maximumLeaseMs < proof.canaryDurationMs || proof.maximumLeaseMs > 300000) { reason = "manual_approval_or_lease_rejected"; return false; }
    reason = "proof_valid_for_bounded_canary"; return true;
}

CanaryDecision CanaryController::Evaluate(const CanaryMeasurement& measurement, double minimumBenefit, double maximumHarm) const {
    if (!measurement.valid) return {false, true, "measurement_missing_or_invalid"};
    if (!measurement.targetIdentityStable || measurement.foregroundProtected) return {false, true, "identity_changed_or_foreground_protected"};
    if (!measurement.mechanismObserved) return {false, true, "expected_mechanism_not_observed"};
    if (measurement.protectedHarm > maximumHarm) return {false, true, "protected_harm_threshold_exceeded"};
    if (measurement.primaryBenefit <= minimumBenefit) return {false, true, "neutral_or_insufficient_canary_benefit"};
    return {true, false, "canary_proved_predeclared_benefit"};
}

bool ActionLeaseManager::Grant(const std::string& transactionId, long long expiresAtMs, std::function<void()> restore, std::string& reason) {
    if (HasUnsafeText(transactionId) || !restore || expiresAtMs <= NowMs()) { reason = "invalid_lease"; return false; }
    std::lock_guard lock(mutex_); if (leases_.contains(transactionId)) { reason = "conflicting_transaction"; return false; }
    leases_.emplace(transactionId, Lease{expiresAtMs, std::move(restore)}); reason = "lease_granted"; return true;
}
int ActionLeaseManager::RestoreExpired(long long nowMs) { std::vector<std::function<void()>> callbacks; { std::lock_guard lock(mutex_); for (auto it = leases_.begin(); it != leases_.end();) { if (it->second.expiresAtMs <= nowMs) { callbacks.push_back(std::move(it->second.restore)); it = leases_.erase(it); } else ++it; } } for (auto& callback : callbacks) callback(); return static_cast<int>(callbacks.size()); }
int ActionLeaseManager::RestoreAll() { return RestoreExpired(LLONG_MAX); }
