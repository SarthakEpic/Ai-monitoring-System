#pragma once

#include <string>

struct RuntimeHealthSample {
    long long timestamp = 0;
    std::string status = "STARTING";
    std::string summary = "Collecting runtime health";
    std::string predictionSource = "WARMING UP";
    std::string predictionPath = "warmup";
    std::string lastFailure = "none";
    double availabilityScore = 0.0;
    double modelSuccessRate = 100.0;
    double fallbackRate = 0.0;
    double storageSuccessRate = 100.0;
    double avgPredictionLatencyMs = 0.0;
    double lastPredictionLatencyMs = 0.0;
    // Monitor self-observation values are separate from user workload metrics.
    double collectorP95Ms = 0.0;
    double inferenceP95Ms = 0.0;
    double processCpuPercent = 0.0;
    double processIoBytesPerSecond = 0.0;
    double wakeupsPerSecond = 0.0;
    double workingSetMb = 0.0;
    double storageBatchLatencyMs = 0.0;
    int pendingWrites = 0;
    std::string observerState = "within_budget";
    int totalCycles = 0;
    int modelAttempts = 0;
    int modelSuccesses = 0;
    int modelFailures = 0;
    int serviceSuccesses = 0;
    int processSuccesses = 0;
    int fallbackPredictions = 0;
    int cachedPredictions = 0;
    int warmupPredictions = 0;
    int storageWrites = 0;
    int storageFailures = 0;
    int alerts = 0;
    bool serviceRunning = false;
    bool storageReady = false;
};

class RuntimeHealthMonitor {
public:
    void RecordPredictionCycle(
        bool hasModelWindow,
        bool shouldRunModel,
        const std::string& finalSource,
        const std::string& predictionPath,
        bool modelSuccess,
        double latencyMs,
        bool serviceRunning,
        const std::string& failureReason
    );

    void RecordStorageWrite(const std::string& name, bool ok);
    void RecordAlert();
    void RecordObserverMetrics(
        double collectorP95Ms,
        double inferenceP95Ms,
        double processCpuPercent,
        double processIoBytesPerSecond,
        double wakeupsPerSecond,
        double workingSetMb,
        int pendingWrites,
        const std::string& observerState
    );
    void RecordStorageBatchLatency(double latencyMs);

    RuntimeHealthSample Snapshot(
        long long timestamp,
        const std::string& currentSource,
        const std::string& predictionPath,
        bool serviceRunning,
        bool storageReady
    ) const;

private:
    int totalCycles_ = 0;
    int modelAttempts_ = 0;
    int modelSuccesses_ = 0;
    int modelFailures_ = 0;
    int serviceSuccesses_ = 0;
    int processSuccesses_ = 0;
    int fallbackPredictions_ = 0;
    int cachedPredictions_ = 0;
    int warmupPredictions_ = 0;
    int storageWrites_ = 0;
    int storageFailures_ = 0;
    int alerts_ = 0;
    double avgPredictionLatencyMs_ = 0.0;
    double lastPredictionLatencyMs_ = 0.0;
    double collectorP95Ms_ = 0.0;
    double inferenceP95Ms_ = 0.0;
    double processCpuPercent_ = 0.0;
    double processIoBytesPerSecond_ = 0.0;
    double wakeupsPerSecond_ = 0.0;
    double workingSetMb_ = 0.0;
    double storageBatchLatencyMs_ = 0.0;
    int pendingWrites_ = 0;
    std::string observerState_ = "within_budget";
    std::string lastFailure_ = "none";
};
