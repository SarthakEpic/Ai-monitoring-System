#include "RuntimeHealth.h"

#include <algorithm>
#include <sstream>

using namespace std;

namespace {
double Percent(int numerator, int denominator, double fallback) {
    if (denominator <= 0) return fallback;
    return (static_cast<double>(numerator) * 100.0) / static_cast<double>(denominator);
}

double Clamp(double value, double low, double high) {
    return max(low, min(value, high));
}
}

void RuntimeHealthMonitor::RecordPredictionCycle(
    bool hasModelWindow,
    bool shouldRunModel,
    const string& finalSource,
    const string& predictionPath,
    bool modelSuccess,
    double latencyMs,
    bool,
    const string& failureReason
) {
    ++totalCycles_;

    if (!hasModelWindow || predictionPath == "warmup") {
        ++warmupPredictions_;
    } else if (predictionPath == "cache") {
        ++cachedPredictions_;
    } else if (finalSource == "FALLBACK" || predictionPath == "fallback") {
        ++fallbackPredictions_;
    }

    if (shouldRunModel) {
        ++modelAttempts_;
        if (modelSuccess) {
            ++modelSuccesses_;
            if (predictionPath == "service") {
                ++serviceSuccesses_;
            } else if (predictionPath == "process") {
                ++processSuccesses_;
            }
        } else {
            ++modelFailures_;
            lastFailure_ = failureReason.empty() ? "model unavailable" : failureReason;
        }
    }

    if (latencyMs >= 0.0) {
        lastPredictionLatencyMs_ = latencyMs;
        if (avgPredictionLatencyMs_ <= 0.0) {
            avgPredictionLatencyMs_ = latencyMs;
        } else {
            avgPredictionLatencyMs_ = (avgPredictionLatencyMs_ * 0.85) + (latencyMs * 0.15);
        }
    }
}

void RuntimeHealthMonitor::RecordStorageWrite(const string& name, bool ok) {
    ++storageWrites_;
    if (!ok) {
        ++storageFailures_;
        lastFailure_ = name.empty() ? "sqlite write failed" : ("sqlite write failed: " + name);
    }
}

void RuntimeHealthMonitor::RecordAlert() {
    ++alerts_;
}

RuntimeHealthSample RuntimeHealthMonitor::Snapshot(
    long long timestamp,
    const string& currentSource,
    const string& predictionPath,
    bool serviceRunning,
    bool storageReady
) const {
    RuntimeHealthSample sample;
    sample.timestamp = timestamp;
    sample.predictionSource = currentSource;
    sample.predictionPath = predictionPath;
    sample.lastFailure = lastFailure_;
    sample.totalCycles = totalCycles_;
    sample.modelAttempts = modelAttempts_;
    sample.modelSuccesses = modelSuccesses_;
    sample.modelFailures = modelFailures_;
    sample.serviceSuccesses = serviceSuccesses_;
    sample.processSuccesses = processSuccesses_;
    sample.fallbackPredictions = fallbackPredictions_;
    sample.cachedPredictions = cachedPredictions_;
    sample.warmupPredictions = warmupPredictions_;
    sample.storageWrites = storageWrites_;
    sample.storageFailures = storageFailures_;
    sample.alerts = alerts_;
    sample.serviceRunning = serviceRunning;
    sample.storageReady = storageReady;
    sample.avgPredictionLatencyMs = avgPredictionLatencyMs_;
    sample.lastPredictionLatencyMs = lastPredictionLatencyMs_;
    sample.modelSuccessRate = Percent(modelSuccesses_, modelAttempts_, 100.0);
    sample.fallbackRate = Percent(fallbackPredictions_, max(1, totalCycles_ - warmupPredictions_), 0.0);
    sample.storageSuccessRate = Percent(storageWrites_ - storageFailures_, storageWrites_, storageReady ? 100.0 : 0.0);

    const double modelFailureRate = 100.0 - sample.modelSuccessRate;
    sample.availabilityScore = Clamp(
        100.0 - (sample.fallbackRate * 0.45) - (modelFailureRate * 0.35) - ((100.0 - sample.storageSuccessRate) * 0.20),
        0.0,
        100.0
    );

    if (!storageReady || sample.storageSuccessRate < 80.0 || modelFailureRate >= 70.0) {
        sample.status = "CRITICAL";
    } else if (totalCycles_ < 10) {
        sample.status = "STARTING";
    } else if (sample.storageSuccessRate < 95.0 || sample.fallbackRate > 35.0 || sample.avgPredictionLatencyMs > 3500.0 || modelFailureRate > 35.0) {
        sample.status = "DEGRADED";
    } else {
        sample.status = "HEALTHY";
    }

    ostringstream summary;
    if (sample.status == "HEALTHY") {
        summary << "Model and SQLite are stable";
    } else if (sample.status == "STARTING") {
        summary << "Collecting enough runtime cycles";
    } else if (!storageReady || sample.storageSuccessRate < 95.0) {
        summary << "SQLite reliability needs attention";
    } else if (sample.fallbackRate > 35.0 || modelFailureRate > 35.0) {
        summary << "Inference is falling back too often";
    } else {
        summary << "Runtime latency or reliability is degraded";
    }
    sample.summary = summary.str();

    return sample;
}
