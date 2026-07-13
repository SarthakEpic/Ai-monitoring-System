#include "ReliabilityGate.h"

#include <algorithm>

bool ReliabilityGate::Contains(const std::vector<std::string>& values, const std::string& value) {
    return !value.empty() && std::find(values.begin(), values.end(), value) != values.end();
}

ReliabilityGateDecision ReliabilityGate::Evaluate(
    const ReliabilityCertificate* certificate,
    const ReliabilityGateInput& input
) const {
    ReliabilityGateDecision decision;
    if (!certificate || certificate->certificateId.empty()) {
        decision.reason = "certificate_missing";
        return decision;
    }
    if (certificate->expiresAtMs <= input.nowMs || certificate->expiresAtMs <= certificate->issuedAtMs) {
        decision.reason = "certificate_expired";
        return decision;
    }
    if (certificate->modelManifestHash != input.modelManifestHash ||
        certificate->featureSchemaHash != input.featureSchemaHash) {
        decision.reason = "artifact_hash_mismatch";
        return decision;
    }
    const SupportedEnvelope& envelope = certificate->envelope;
    if (!Contains(envelope.windowsBuildFamilies, input.windowsBuildFamily) ||
        !Contains(envelope.cpuCoreTiers, input.cpuCoreTier) ||
        !Contains(envelope.ramTiers, input.ramTier) ||
        !Contains(envelope.storageTiers, input.storageTier) ||
        !Contains(envelope.workloadClasses, input.workloadClass)) {
        decision.reason = "outside_supported_envelope";
        return decision;
    }
    if (!input.dataQuality.telemetryFresh || !input.dataQuality.collectorsHealthy || !input.dataQuality.missingnessAcceptable) {
        decision.reason = "data_quality_rejected";
        return decision;
    }
    if (input.drift >= DriftLevel::DeterministicMonitoringOnly) {
        decision.reason = input.drift == DriftLevel::CertificateInvalid ? "certificate_invalidated_by_drift" : "drift_requires_monitor_only";
        return decision;
    }
    if (input.oodScore > 0.25) {
        decision.reason = "ood_rejected";
        return decision;
    }
    if (input.estimatedErrorUpperBound > certificate->maximumAcceptedErrorUpperBound) {
        decision.reason = "error_budget_exceeded";
        return decision;
    }
    if (input.coverageLowerBound < certificate->minimumCoverageLowerBound) {
        decision.reason = "coverage_lower_bound_failed";
        return decision;
    }

    decision.accepted = true;
    decision.requiredMode = RuntimeMode::RecommendationOnly;
    decision.reason = certificate->issuedForResearch ? "research_certificate_recommendation_only" : "valid_certificate_recommendation_only";
    return decision;
}

const char* ReliabilityGate::ToString(DriftLevel level) {
    switch (level) {
    case DriftLevel::Normal: return "NORMAL";
    case DriftLevel::CollectMoreEvidence: return "COLLECT_MORE_EVIDENCE";
    case DriftLevel::RecommendationOnly: return "RECOMMENDATION_ONLY";
    case DriftLevel::DeterministicMonitoringOnly: return "DETERMINISTIC_MONITOR_ONLY";
    case DriftLevel::CertificateInvalid: return "CERTIFICATE_INVALID";
    }
    return "CERTIFICATE_INVALID";
}