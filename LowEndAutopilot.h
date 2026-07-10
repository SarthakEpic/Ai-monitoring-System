#pragma once

#include <string>
#include <vector>

#include "AdaptiveBaseline.h"
#include "AutoHealPlanner.h"
#include "DecisionEngine.h"
#include "SystemMetrics.h"

struct LowEndAutopilotConfig {
    bool enabled = true;
    bool forceLowEndDevice = false;
    bool protectForegroundApp = true;
    bool delayUpdaterAndSyncApps = true;
    bool sleepUnusedBrowserHelpers = true;
    bool preferReversibleActions = true;
    int maxActionsPerCycle = 4;
    double weakDeviceMemoryMB = 4608.0;
    double memoryPressureThreshold = 78.0;
    double cpuPressureThreshold = 70.0;
    double diskFreePressureThreshold = 8.0;
    double minCandidateSafetyScore = 55.0;
};

struct AutopilotRecommendation {
    int rank = 0;
    std::string actionType = "observe";
    std::string actionName = "monitor_only";
    std::string targetName = "system";
    unsigned long targetPid = 0;
    std::string category = "UNKNOWN";
    std::string safety = "UNKNOWN";
    std::string reason = "No action needed";
    std::string reversibility = "none";
    double expectedRecoveredRamMB = 0.0;
    double expectedCpuDropPercent = 0.0;
    double safetyScore = 0.0;
    bool foregroundProtected = true;
    bool userAppTouched = false;
    bool reversible = true;
};

struct LowEndAutopilotResult {
    long long timestamp = 0;
    bool enabled = false;
    bool lowEndDevice = false;
    bool active = false;
    bool foregroundProtected = true;
    bool quickRestoreAvailable = false;
    int reversibleActionCount = 0;
    int actionsRecommended = 0;
    int userAppsTouched = 0;
    double pressureScore = 0.0;
    double memoryPressure = 0.0;
    double cpuPressure = 0.0;
    double diskPressure = 0.0;
    double estimatedRecoveredRamMB = 0.0;
    double estimatedCpuDropPercent = 0.0;
    std::string mode = "STANDARD";
    std::string status = "DISABLED";
    std::string summary = "Low-end autopilot disabled";
    std::string primaryAction = "monitor_only";
    std::string primaryTarget = "system";
    std::string safetyNotes = "No optimization action selected";
    std::vector<AutopilotRecommendation> recommendations;
};

class LowEndAutopilotEngine {
public:
    LowEndAutopilotResult Evaluate(
        const SystemSnapshot& snapshot,
        const DecisionResult& decision,
        const HealPlan& plan,
        const AdaptiveBaselineResult& baseline,
        const LowEndAutopilotConfig& config
    ) const;
};
