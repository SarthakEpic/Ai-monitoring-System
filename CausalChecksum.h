#pragma once

#include <string>
#include <vector>

enum class RootCauseClass {
    CpuScheduling,
    MemoryPaging,
    StorageLatency,
    GpuContention,
    NetworkContention,
    ProcessStorm,
    ThermalPower,
    Unknown,
};

struct CausalEvidenceInput {
    bool telemetryComplete = false;
    bool foregroundQoeDegraded = false;
    bool interventionReversible = false;
    double cpuUtilizationPercent = 0.0;
    double runnablePressure = 0.0;
    double memoryUtilizationPercent = 0.0;
    double hardFaultsPerSecond = 0.0;
    double storageLatencyMs = 0.0;
    double storageQueueDepth = 0.0;
    double networkCongestionPercent = 0.0;
    double processStormScore = 0.0;
    double thermalThrottlePercent = 0.0;
};

struct CausalChecksumResult {
    RootCauseClass rootCause = RootCauseClass::Unknown;
    double supportScore = 0.0;
    bool sufficientEvidence = false;
    bool actionSupportAllowed = false;
    std::vector<std::string> evidence;
    std::string reason;
};

// Requires a measurable mechanism, healthy telemetry, and a reversible action
// before learned policy evidence can support a recommendation.
class CausalChecksum {
public:
    CausalChecksumResult Evaluate(const CausalEvidenceInput& input) const;
    static const char* ToString(RootCauseClass rootCause);
};
