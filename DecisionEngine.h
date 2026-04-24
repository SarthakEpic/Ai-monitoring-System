#pragma once

#include <deque>
#include <string>

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

struct DecisionResult {
    RiskLevel level = RiskLevel::Normal;
    double riskScore = 0.0;
    double anomalyScore = 0.0;
    double pressureScore = 0.0;
    std::string summary = "System stable";
};

class DecisionEngine {
public:
    DecisionResult Evaluate(
        const SystemSnapshot& snapshot,
        double aiProbability,
        const std::string& aiSource,
        const DecisionThresholds& thresholds,
        const DecisionContext& context
    ) const;

    static const char* ToString(RiskLevel level);
};
