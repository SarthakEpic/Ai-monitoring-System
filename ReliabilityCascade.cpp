#include "ReliabilityCascade.h"

CascadeDecision ReliabilityCascade::Evaluate(const CascadeTelemetry& telemetry, const CausalEvidenceInput& causal,
                                             WorkloadClass workload, const FailureCriticInput& critic) const {
    CascadeDecision decision;
    decision.workload = workload;
    const auto causalResult = CausalChecksum().Evaluate(causal);
    decision.rootCause = causalResult.rootCause;
    if (!telemetry.telemetryComplete) {
        decision.reason = "telemetry_incomplete_escalate";
        return decision;
    }
    if (telemetry.anomalyScore < 0.15 && telemetry.cpuPercent < 70.0 && telemetry.memoryPercent < 80.0 && telemetry.diskLatencyMs < 20.0) {
        decision.route = SentinelRoute::Stable;
        decision.specialistRequired = false;
        decision.temporalModelRequired = false;
        decision.reason = "sentinel_stable_monitoring_only";
        return decision;
    }
    decision.route = telemetry.anomalyScore < 0.55 ? SentinelRoute::PossibleEvent : SentinelRoute::Escalate;
    decision.specialistRequired = true;
    decision.temporalModelRequired = decision.route == SentinelRoute::Escalate;
    if (critic.modelDisagreement > 0.20 || critic.oodScore > 0.25 || critic.estimatedErrorUpperBound > 0.10 ||
        !critic.supportEnvelopeMatch || !critic.missingnessAcceptable || !critic.causalSupport || !causalResult.sufficientEvidence) {
        decision.reason = "failure_critic_abstained";
        return decision;
    }
    decision.acceptedForRecommendation = critic.calibratedEventProbability >= 0.50;
    decision.reason = decision.acceptedForRecommendation ? "specialist_and_critic_support_recommendation" : "specialist_event_below_recommendation_threshold";
    return decision;
}
