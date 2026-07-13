#pragma once

#include <string>

#include "CausalChecksum.h"

enum class SentinelRoute { Stable, PossibleEvent, Escalate };
enum class WorkloadClass { Browser, Gaming, Compile, VideoCall, Media, PostBoot, Unknown };

struct CascadeTelemetry {
    double cpuPercent = 0.0;
    double memoryPercent = 0.0;
    double diskLatencyMs = 0.0;
    double anomalyScore = 0.0;
    bool telemetryComplete = false;
};

struct FailureCriticInput {
    double calibratedEventProbability = 0.0;
    double modelDisagreement = 1.0;
    double oodScore = 1.0;
    double estimatedErrorUpperBound = 1.0;
    bool supportEnvelopeMatch = false;
    bool missingnessAcceptable = false;
    bool causalSupport = false;
};

struct CascadeDecision {
    SentinelRoute route = SentinelRoute::Escalate;
    WorkloadClass workload = WorkloadClass::Unknown;
    RootCauseClass rootCause = RootCauseClass::Unknown;
    bool specialistRequired = true;
    bool temporalModelRequired = true;
    bool acceptedForRecommendation = false;
    std::string reason;
};

// Stage 2A cascade contract. Raw confidence alone never exits the cascade.
class ReliabilityCascade {
public:
    CascadeDecision Evaluate(const CascadeTelemetry& telemetry, const CausalEvidenceInput& causal,
                             WorkloadClass workload, const FailureCriticInput& critic) const;
};
