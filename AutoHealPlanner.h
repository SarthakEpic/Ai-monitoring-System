#pragma once

#include <string>

#include "DecisionEngine.h"
#include "SystemMetrics.h"

struct HealPlan {
    std::string planId = "HP-0";
    std::string status = "OBSERVE";
    std::string executionMode = "SIMULATION_ONLY";
    std::string actionType = "none";
    std::string actionName = "monitor_only";
    std::string targetKind = "system";
    std::string targetName = "system";
    unsigned long targetPid = 0;
    std::string gate = "OBSERVE_ONLY";
    std::string blockedReason = "No action required";
    std::string summary = "No healing plan needed";
    std::string rationale = "System is below intervention threshold";
    std::string preCheck = "Continue baseline monitoring";
    std::string executionStep = "No execution";
    std::string postCheck = "Verify metrics remain stable";
    std::string rollbackPlan = "No rollback required";
    std::string safetyNotes = "Auto-heal execution is disabled";
    std::string expectedImpact = "No resource change expected";
    double readinessScore = 0.0;
    double confidence = 0.0;
    double expectedGainMB = 0.0;
    double riskBefore = 0.0;
    bool wouldExecute = false;
    bool requiresUserApproval = false;
    bool blocked = true;
};

class AutoHealPlanner {
public:
    HealPlan BuildPlan(
        const SystemSnapshot& snapshot,
        const DecisionResult& decision,
        const DecisionPolicy& policy
    ) const;
};
