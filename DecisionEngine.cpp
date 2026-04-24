#include "DecisionEngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <vector>

using namespace std;

namespace {
double ClampPercent(double value) {
    return max(0.0, min(100.0, value));
}

double ComputeThresholdPressure(double cpu, double mem, double disk, int cpuTh, int memTh, int diskTh) {
    double cpuScore = ClampPercent((cpu / max(1, cpuTh)) * 100.0);
    double memScore = ClampPercent((mem / max(1, memTh)) * 100.0);
    double diskScore = 0.0;
    if (disk < diskTh) {
        diskScore = ClampPercent(((diskTh - disk) / max(1, diskTh)) * 100.0);
    }
    return ClampPercent((cpuScore * 0.42) + (memScore * 0.42) + (diskScore * 0.16));
}

double ComputeDeviationScore(const deque<double>& history) {
    if (history.size() < 5) return 0.0;

    vector<double> values(history.begin(), history.end() - 1);
    if (values.empty()) return 0.0;

    const double mean = accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    double variance = 0.0;
    for (double value : values) {
        double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size());
    const double stddev = sqrt(max(variance, 0.0));
    const double current = history.back();
    const double deviation = abs(current - mean);

    if (stddev < 1e-6) {
        return deviation > 0.0 ? 50.0 : 0.0;
    }

    const double normalized = deviation / max(1.0, stddev * 2.0);
    return ClampPercent(normalized * 100.0);
}

double ComputeAnomalyScore(const SystemSnapshot& snapshot, const DecisionContext& context) {
    const double cpuDeviation = ComputeDeviationScore(context.cpuHistory);
    const double memDeviation = ComputeDeviationScore(context.memHistory);
    const double diskDeviation = ComputeDeviationScore(context.diskHistory);
    const double netDeviation = ComputeDeviationScore(context.netHistory);
    const double procDeviation = ComputeDeviationScore(context.processHistory);

    double topProcessBoost = 0.0;
    if (snapshot.topProcess.cpuPercent >= 45.0) topProcessBoost += 15.0;
    if (snapshot.topProcess.memoryMB >= 500.0) topProcessBoost += 10.0;

    return ClampPercent(
        (cpuDeviation * 0.28) +
        (memDeviation * 0.24) +
        (diskDeviation * 0.18) +
        (netDeviation * 0.15) +
        (procDeviation * 0.15) +
        topProcessBoost
    );
}

string BuildSummary(const SystemSnapshot& snapshot, double anomalyScore, double pressureScore, double aiProbability, RiskLevel level) {
    ostringstream oss;
    if (level == RiskLevel::Critical) {
        oss << "Critical risk";
    } else if (level == RiskLevel::Warning) {
        oss << "Warning risk";
    } else {
        oss << "System stable";
    }

    if (snapshot.diskFree <= 5.0) {
        oss << ": low disk space";
    } else if (snapshot.memoryUsage >= 90.0) {
        oss << ": high memory pressure";
    } else if (snapshot.topProcess.cpuPercent >= 50.0) {
        oss << ": hot process " << snapshot.topProcess.name;
    } else if (anomalyScore >= max(pressureScore, aiProbability)) {
        oss << ": anomalous behavior spike";
    } else if (aiProbability >= pressureScore) {
        oss << ": forecast trend elevated";
    } else {
        oss << ": threshold pressure elevated";
    }

    return oss.str();
}
}

DecisionResult DecisionEngine::Evaluate(
    const SystemSnapshot& snapshot,
    double aiProbability,
    const string& aiSource,
    const DecisionThresholds& thresholds,
    const DecisionContext& context
) const {
    DecisionResult result;

    result.pressureScore = ComputeThresholdPressure(
        snapshot.cpuUsage,
        snapshot.memoryUsage,
        snapshot.diskFree,
        thresholds.cpuThreshold,
        thresholds.memThreshold,
        thresholds.diskThreshold
    );
    result.anomalyScore = ComputeAnomalyScore(snapshot, context);

    double aiWeight = 0.20;
    if (aiSource == "MODEL") aiWeight = 0.55;
    else if (aiSource == "FALLBACK") aiWeight = 0.35;

    const double anomalyWeight = 0.25;
    const double pressureWeight = max(0.0, 1.0 - aiWeight - anomalyWeight);

    result.riskScore =
        ClampPercent((aiProbability * aiWeight) + (result.anomalyScore * anomalyWeight) + (result.pressureScore * pressureWeight));

    const bool emergency =
        snapshot.cpuUsage >= 97.0 ||
        snapshot.memoryUsage >= 97.0 ||
        snapshot.diskFree <= 3.0;

    if (emergency || result.riskScore >= thresholds.criticalRiskThreshold) {
        result.level = RiskLevel::Critical;
    } else if (result.riskScore >= thresholds.warningRiskThreshold) {
        result.level = RiskLevel::Warning;
    } else {
        result.level = RiskLevel::Normal;
    }

    result.summary = BuildSummary(snapshot, result.anomalyScore, result.pressureScore, aiProbability, result.level);
    return result;
}

const char* DecisionEngine::ToString(RiskLevel level) {
    switch (level) {
    case RiskLevel::Normal:
        return "NORMAL";
    case RiskLevel::Warning:
        return "WARNING";
    case RiskLevel::Critical:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}
