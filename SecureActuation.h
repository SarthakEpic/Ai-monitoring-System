#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ActionVerification.h"
#include "ReliabilityGate.h"
#include "ResourceOrchestrator.h"

enum class ActuatorCommand { BeginCanary, Rollback, WatchdogRestore };
enum class GuardianVerdict { SafeToCanary, Reject, Unknown };

struct SecureProtocolMessage {
    int version = 1;
    std::string requestId;
    std::string sessionId;
    unsigned long long sequence = 0;
    long long issuedAtMs = 0;
    long long expiresAtMs = 0;
    ActuatorCommand command = ActuatorCommand::BeginCanary;
    std::string proofId;
};

struct ActionEvidence {
    ResourceActionType action = ResourceActionType::None;
    double noOpOutcome = 0.0;
    double expectedBenefit = 0.0;
    double benefitLowerBound = -1.0;
    double harmUpperBound = 1.0;
    double rollbackProbabilityUpperBound = 1.0;
    bool mechanismConsistent = false;
    bool guardianPassed = false;
};

struct ProofCarryingAction {
    std::string requestId;
    std::string modelId;
    std::string certificateId;
    ReliabilityGateDecision need;
    ActionEvidence evidence;
    ProcessIdentity target;
    ActionContext context;
    bool deterministicSafetyPassed = false;
    bool rollbackSnapshotComplete = false;
    int canaryDurationMs = 0;
    int maximumLeaseMs = 0;
};

struct GuardianInput {
    double calibratedVetoRisk = 1.0;
    double disagreement = 1.0;
    double oodScore = 1.0;
    bool measurementsFresh = false;
    bool protectedTarget = true;
};

struct CanaryMeasurement {
    bool valid = false;
    bool targetIdentityStable = false;
    bool foregroundProtected = false;
    bool mechanismObserved = false;
    double primaryBenefit = 0.0;
    double protectedHarm = 1.0;
};

struct CanaryDecision {
    bool commit = false;
    bool rollback = true;
    std::string reason;
};

class ReplayProtection {
public:
    bool Accept(const SecureProtocolMessage& message, long long nowMs, std::string& reason);
};

class SecureMessageValidation {
public:
    static bool Validate(const SecureProtocolMessage& message, std::string& reason);
    static bool Parse(const std::string& wire, SecureProtocolMessage& message, std::string& reason);
};

class ActionGuardian {
public:
    GuardianVerdict Evaluate(const GuardianInput& input) const;
};

class TrustedSafetyController {
public:
    bool ValidateProof(const ProofCarryingAction& proof, std::string& reason) const;
};

class CanaryController {
public:
    CanaryDecision Evaluate(const CanaryMeasurement& measurement, double minimumBenefit, double maximumHarm) const;
};

class ActionLeaseManager {
public:
    bool Grant(const std::string& transactionId, long long expiresAtMs, std::function<void()> restore, std::string& reason);
    int RestoreExpired(long long nowMs);
    int RestoreAll();

private:
    struct Lease { long long expiresAtMs = 0; std::function<void()> restore; };
    std::mutex mutex_;
    std::unordered_map<std::string, Lease> leases_;
};

const char* ToString(ActuatorCommand command);
const char* ToString(GuardianVerdict verdict);
