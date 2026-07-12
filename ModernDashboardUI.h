#pragma once

#include <windows.h>

#include <deque>
#include <string>
#include <vector>

#include "AdaptiveBaseline.h"
#include "AutoHealPlanner.h"
#include "BackgroundAgent.h"
#include "BenchmarkProof.h"
#include "HealingVerifier.h"
#include "ImpactLearning.h"
#include "LowEndAutopilot.h"
#include "PerformanceIntelligence.h"
#include "RuntimeHealth.h"
#include "SafeOnlinePolicy.h"
#include "SafetyPolicy.h"
#include "SystemMetrics.h"

enum class DashboardView {
    Overview,
    Processes,
    Intelligence,
    Safety,
    Experiments,
    Settings,
};

struct DashboardUiFonts {
    HFONT title = nullptr;
    HFONT section = nullptr;
    HFONT value = nullptr;
    HFONT body = nullptr;
};

struct DashboardUiState {
    SystemSnapshot system;
    std::vector<ProcessSnapshot> processes;
    QoeTelemetrySample qoe;
    PerformanceCriticalityGraph criticality;
    ShadowPolicyDecision shadowPolicy;
    OnlinePolicyDecision onlinePolicy;
    RuntimeHealthSample runtime;
    AdaptiveBaselineResult baseline;
    LowEndAutopilotResult autopilot;
    BackgroundAgentResult agent;
    BenchmarkProofResult benchmark;
    HealPlan healPlan;
    HealVerification healVerification;
    SafetyPolicyResult safetyPolicy;

    std::deque<double> cpuHistory;
    std::deque<double> memoryHistory;
    std::deque<double> diskHistory;
    std::deque<double> latencyHistory;

    double aiProbability = 0.0;
    double aiConfidence = 0.0;
    double riskScore = 0.0;
    double anomalyScore = 0.0;
    double pressureScore = 0.0;
    double actionConfidence = 0.0;
    double expectedGainMB = 0.0;
    int modelFeatureCount = 0;
    int candidateCount = 0;
    int cooldownSeconds = 0;
    int cpuThreshold = 80;
    int memoryThreshold = 85;
    int diskThreshold = 8;
    int warningThreshold = 45;
    int criticalThreshold = 65;
    int predictIntervalSeconds = 10;
    int cacheSeconds = 30;
    size_t pendingWrites = 0;

    bool storageReady = false;
    bool alertActive = false;
    bool dryRun = true;
    bool actionExecutionEnabled = false;
    bool actionGlobalDisable = true;
    bool onlinePolicyEnabled = false;
    bool onlinePolicyPromoted = false;
    bool browserIntegrationEnabled = false;
    bool bitsIntegrationEnabled = false;
    bool prefetchEnabled = false;

    std::string riskLevel = "NORMAL";
    std::string aiSource = "WARMING UP";
    std::string aiClass = "UNKNOWN";
    std::string aiReason = "Collecting baseline";
    std::string modelReadiness = "unknown";
    std::string modelGeneratedAt = "unknown";
    std::string decisionSummary = "System stable";
    std::string decisionReason = "Collecting baseline";
    std::string rootCause = "unknown";
    std::string rootCauseDetail = "No dominant pressure source";
    std::string recommendedAction = "monitor_only";
    std::string actionTarget = "system";
    std::string safetyGate = "OBSERVE_ONLY";
    std::string blockedReason = "Actions are locked";
    std::string performanceMode = "LOW END";
    std::string policyMode = "SHADOW";
    std::string modelVersion = "unknown";
};

void DrawModernDashboard(
    HDC hdc,
    const RECT& client,
    const DashboardUiState& state,
    DashboardView view,
    const DashboardUiFonts& fonts
);

DashboardView HitTestDashboardNavigation(const RECT& client, int x, int y, DashboardView current);

