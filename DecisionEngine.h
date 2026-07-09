#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "SystemMetrics.h"

enum class RiskLevel {
    Normal,
    Warning,
    Critical,
};

struct DecisionContext {
    std::deque<double> cpuHistory;
    std::deque<double> memHistory;
    std::deque<double> diskHistory;
    std::deque<double> netHistory;
    std::deque<double> processHistory;
};

struct DecisionThresholds {
    int cpuThreshold = 80;
    int memThreshold = 85;
    int diskThreshold = 10;
    double warningRiskThreshold = 55.0;
    double criticalRiskThreshold = 75.0;
};

struct DecisionPolicy {
    bool autoHealEnabled = false;
    bool dryRun = true;
    bool safeMode = true;
    int cooldownSeconds = 300;
    std::string allowlistCsv;
    std::string denylistCsv;
};

struct OptimizationCandidate {
    unsigned long pid = 0;
    std::string name = "N/A";
    std::string category = "UNKNOWN";
    std::string safety = "UNKNOWN";
    std::string intentRole = "none";
    std::string reason = "no candidate selected";
    double cpuPercent = 0.0;
    double memoryMB = 0.0;
    double privateMemoryMB = 0.0;
    double wasteScore = 0.0;
    double safetyScore = 0.0;
    double expectedGainMB = 0.0;
    double rank = 0.0;
    bool protectedByUserIntent = false;
    bool deniedByPolicy = false;
    bool allowedByPolicy = false;
};

struct DecisionResult {
    RiskLevel level = RiskLevel::Normal;
    double riskScore = 0.0;
    double anomalyScore = 0.0;
    double pressureScore = 0.0;
    double actionConfidence = 0.0;
    double expectedGainMB = 0.0;
    double candidateSafetyScore = 0.0;
    std::string summary = "System stable";
    std::string reason = "No risk detected";
    std::string rootCause = "none";
    std::string rootCauseDetail = "No dominant pressure source";
    std::string recommendedAction = "monitor_only";
    std::string actionTarget = "system";
    unsigned long actionTargetPid = 0;
    std::string safetyGate = "OBSERVE_ONLY";
    std::string blockedReason = "Auto-heal execution disabled";
    bool safeToHeal = false;
    bool dryRun = true;
    bool cooldownActive = false;
    int cooldownRemainingSeconds = 0;
    int candidateCount = 0;
    OptimizationCandidate candidate;
};

class DecisionEngine {
public:
    DecisionResult Evaluate(
        const SystemSnapshot& snapshot,
        double aiProbability,
        double aiConfidence,
        const std::string& aiSource,
        const std::string& aiReason,
        const DecisionThresholds& thresholds,
        const DecisionContext& context,
        const DecisionPolicy& policy
    );

    static const char* ToString(RiskLevel level);

private:
    std::unordered_map<std::string, long long> lastRecommendationAt_;
};
