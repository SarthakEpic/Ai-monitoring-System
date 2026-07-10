#pragma once

#include <string>

#include "AutoHealPlanner.h"
#include "DecisionEngine.h"
#include "LowEndAutopilot.h"
#include "SystemMetrics.h"

struct BenchmarkProofResult {
    long long timestamp = 0;
    std::string status = "COLLECTING";
    std::string mode = "ESTIMATE";
    std::string summary = "Collecting proof samples";
    double beforeCpu = 0.0;
    double beforeMemory = 0.0;
    double beforeDiskFree = 0.0;
    double beforeRisk = 0.0;
    double afterCpuEstimate = 0.0;
    double afterMemoryEstimate = 0.0;
    double afterDiskFreeEstimate = 0.0;
    double afterRiskEstimate = 0.0;
    double recoveredRamMB = 0.0;
    double cpuDropPercent = 0.0;
    double diskFreeGainPercent = 0.0;
    double riskDropPercent = 0.0;
    int actionsRecommended = 0;
    int userAppsTouched = 0;
    double confidence = 0.0;
    std::string foregroundProcess = "N/A";
};

class BenchmarkProofEngine {
public:
    BenchmarkProofResult Build(
        const SystemSnapshot& snapshot,
        const DecisionResult& decision,
        const HealPlan& plan,
        const LowEndAutopilotResult& autopilot
    ) const;
};
