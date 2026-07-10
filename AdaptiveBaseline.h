#pragma once

#include <string>

#include "SystemMetrics.h"

struct BaselineMetricState {
    double mean = 0.0;
    double variance = 16.0;
    int samples = 0;
};

struct AdaptiveBaselineResult {
    long long timestamp = 0;
    std::string status = "WARMING_UP";
    std::string summary = "Learning this device baseline";
    std::string dominantMetric = "none";
    int sampleCount = 0;
    bool ready = false;
    double confidence = 0.0;
    double anomalyScore = 0.0;
    double riskHint = 0.0;
    double riskAdjustment = 0.0;
    double cpuMean = 0.0;
    double memoryMean = 0.0;
    double diskFreeMean = 0.0;
    double networkMean = 0.0;
    double processCountMean = 0.0;
    double topCpuMean = 0.0;
    double topMemoryMean = 0.0;
    double cpuDeviation = 0.0;
    double memoryDeviation = 0.0;
    double diskDeviation = 0.0;
    double networkDeviation = 0.0;
    double processDeviation = 0.0;
    double topCpuDeviation = 0.0;
    double topMemoryDeviation = 0.0;
};

class AdaptiveBaselineEngine {
public:
    AdaptiveBaselineResult EvaluateAndUpdate(
        const SystemSnapshot& snapshot,
        int minSamples,
        double sensitivity
    );

private:
    struct MetricEvaluation {
        double mean = 0.0;
        double deviation = 0.0;
        double pressure = 0.0;
    };

    MetricEvaluation EvaluateMetric(
        BaselineMetricState& state,
        double value,
        bool lowerIsRisk,
        double minStdDev,
        double sensitivity
    );

    BaselineMetricState cpu_;
    BaselineMetricState memory_;
    BaselineMetricState diskFree_;
    BaselineMetricState network_;
    BaselineMetricState processCount_;
    BaselineMetricState topCpu_;
    BaselineMetricState topMemory_;
};
