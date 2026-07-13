#include "CausalChecksum.h"

#include <algorithm>

namespace {
void Add(CausalChecksumResult& result, const char* evidence, double score) {
    result.evidence.emplace_back(evidence);
    result.supportScore += score;
}
}

CausalChecksumResult CausalChecksum::Evaluate(const CausalEvidenceInput& input) const {
    CausalChecksumResult result;
    if (!input.telemetryComplete) {
        result.reason = "telemetry_incomplete";
        return result;
    }
    if (!input.foregroundQoeDegraded) {
        result.reason = "no_user_impact_to_explain";
        return result;
    }

    if (input.memoryUtilizationPercent >= 85.0 && input.hardFaultsPerSecond >= 20.0) {
        result.rootCause = RootCauseClass::MemoryPaging;
        Add(result, "high_memory_utilization", 0.45);
        Add(result, "elevated_hard_fault_rate", 0.45);
    } else if (input.storageLatencyMs >= 30.0 && input.storageQueueDepth >= 1.0) {
        result.rootCause = RootCauseClass::StorageLatency;
        Add(result, "high_storage_latency", 0.45);
        Add(result, "storage_queue_pressure", 0.45);
    } else if (input.cpuUtilizationPercent >= 80.0 && input.runnablePressure >= 0.70) {
        result.rootCause = RootCauseClass::CpuScheduling;
        Add(result, "high_cpu_utilization", 0.40);
        Add(result, "runnable_queue_pressure", 0.40);
    } else if (input.processStormScore >= 0.75) {
        result.rootCause = RootCauseClass::ProcessStorm;
        Add(result, "process_creation_or_wakeup_storm", 0.80);
    } else if (input.thermalThrottlePercent >= 25.0) {
        result.rootCause = RootCauseClass::ThermalPower;
        Add(result, "thermal_throttling", 0.80);
    } else if (input.networkCongestionPercent >= 80.0) {
        result.rootCause = RootCauseClass::NetworkContention;
        Add(result, "network_congestion", 0.80);
    } else {
        result.reason = "no_supported_mechanism";
        return result;
    }

    result.sufficientEvidence = result.supportScore >= 0.75;
    result.actionSupportAllowed = result.sufficientEvidence && input.interventionReversible;
    result.reason = result.actionSupportAllowed ? "mechanism_supported_reversible_recommendation" :
        (result.sufficientEvidence ? "mechanism_supported_but_action_not_reversible" : "insufficient_mechanistic_evidence");
    return result;
}

const char* CausalChecksum::ToString(RootCauseClass rootCause) {
    switch (rootCause) {
    case RootCauseClass::CpuScheduling: return "CPU_SCHEDULING";
    case RootCauseClass::MemoryPaging: return "MEMORY_PAGING";
    case RootCauseClass::StorageLatency: return "STORAGE_LATENCY";
    case RootCauseClass::GpuContention: return "GPU_CONTENTION";
    case RootCauseClass::NetworkContention: return "NETWORK_CONTENTION";
    case RootCauseClass::ProcessStorm: return "PROCESS_STORM";
    case RootCauseClass::ThermalPower: return "THERMAL_POWER";
    case RootCauseClass::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}
