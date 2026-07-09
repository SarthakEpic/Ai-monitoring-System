#pragma once

#include <string>

#include "AutoHealPlanner.h"
#include "DecisionEngine.h"
#include "SystemMetrics.h"

struct HealVerification {
    std::string verificationId = "HV-0";
    std::string status = "NOT_NEEDED";
    std::string mode = "SIMULATION_ONLY";
    std::string outcomeLabel = "NO_ACTION";
    std::string summary = "No verification needed";
    std::string reason = "No healing plan is active";
    std::string successCriteria = "Risk remains below warning threshold";
    std::string failureCriteria = "Risk increases or user-facing app is affected";
    std::string evidence = "No action proposed";
    int observationWindowSeconds = 60;
    double confidence = 0.0;
    double riskBefore = 0.0;
    double riskAfterEstimate = 0.0;
    double riskDeltaEstimate = 0.0;
    double cpuBefore = 0.0;
    double cpuAfterEstimate = 0.0;
    double memoryBefore = 0.0;
    double memoryAfterEstimate = 0.0;
    double diskBefore = 0.0;
    double diskAfterEstimate = 0.0;
    double networkBeforeKBps = 0.0;
    double networkAfterEstimateKBps = 0.0;
    double expectedGainMB = 0.0;
    bool simulatedPass = false;
    bool simulatedWeak = false;
    bool simulatedFail = false;
    bool blocked = false;
};

class HealingVerifier {
public:
    HealVerification Evaluate(
        const SystemSnapshot& snapshot,
        const DecisionResult& decision,
        const HealPlan& plan
    ) const;
};
