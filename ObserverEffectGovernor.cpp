#include "ObserverEffectGovernor.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <utility>

ObserverEffectGovernor::ObserverEffectGovernor(ObserverEffectConfig config)
    : config_(std::move(config)) {
    config_.maximumCollectorP95Ms = std::max(0.1, config_.maximumCollectorP95Ms);
    config_.maximumInferenceP95Ms = std::max(0.1, config_.maximumInferenceP95Ms);
    config_.maximumProcessCpuPercent = std::max(0.1, config_.maximumProcessCpuPercent);
    config_.maximumProcessIoBytesPerSecond = std::max(1.0, config_.maximumProcessIoBytesPerSecond);
    config_.maximumWakeupsPerSecond = std::max(0.1, config_.maximumWakeupsPerSecond);
    config_.maximumWorkingSetMb = std::max(1.0, config_.maximumWorkingSetMb);
    config_.recoverySamplesRequired = std::max(1, config_.recoverySamplesRequired);
}

ObserverEffectDecision ObserverEffectGovernor::Observe(const ObserverEffectSample& sample) {
    const bool collectorOverhead = sample.collectorP95Ms > config_.maximumCollectorP95Ms;
    const bool inferenceOverhead = sample.inferenceP95Ms > config_.maximumInferenceP95Ms;
    const bool cpuOverhead = sample.processCpuPercent > config_.maximumProcessCpuPercent;
    const bool memoryOverhead = sample.workingSetMb > config_.maximumWorkingSetMb;
    const bool ioOverhead = sample.processIoBytesPerSecond > config_.maximumProcessIoBytesPerSecond;
    const bool wakeupOverhead = sample.wakeupsPerSecond > config_.maximumWakeupsPerSecond;
    const bool queueOverhead = sample.pendingWrites > config_.maximumPendingWrites;
    const bool overBudget = collectorOverhead || inferenceOverhead || cpuOverhead || memoryOverhead || ioOverhead || wakeupOverhead || queueOverhead;

    if (overBudget) {
        consecutiveHealthySamples_ = 0;
        decision_.overBudget = true;
        decision_.slowOptionalSampling = true;
        decision_.disableTier3 = true;
        decision_.maximumOptionalTier = SamplingTier::Tier1Risk;
        decision_.reason = collectorOverhead ? "collector_budget" :
            inferenceOverhead ? "inference_budget" :
            cpuOverhead ? "cpu_budget" : memoryOverhead ? "memory_budget" : ioOverhead ? "io_budget" : wakeupOverhead ? "wakeup_budget" : "storage_queue_budget";
    } else if (decision_.overBudget) {
        ++consecutiveHealthySamples_;
        if (consecutiveHealthySamples_ >= config_.recoverySamplesRequired) {
            decision_ = {};
            decision_.reason = "recovered_within_budget";
        }
    } else {
        decision_ = {};
    }

    // These guards are invariant even while optional evidence is degraded.
    decision_.preserveSafetyMonitoring = true;
    decision_.preserveRollbackWatchdog = true;
    if (!sample.rollbackWatchdogHealthy) {
        decision_.reason = "rollback_watchdog_unhealthy";
    }
    return decision_;
}

EvidenceBudgetScheduler::EvidenceBudgetScheduler(double minimumValuePerCost)
    : minimumValuePerCost_(std::max(0.0, minimumValuePerCost)) {}

EvidenceBudgetDecision EvidenceBudgetScheduler::Decide(
    const EvidenceCandidate& candidate,
    const ObserverEffectDecision& governor
) const {
    if (candidate.safetyRequired) {
        return {true, "safety_required"};
    }
    if (candidate.tier > governor.maximumOptionalTier) {
        return {false, "governor_tier_cap"};
    }
    if (candidate.expectedRiskReduction <= 0.0) {
        return {false, "no_expected_value"};
    }
    const double cost = std::max(0.1, candidate.estimatedCpuCost + candidate.estimatedLatencyCostMs / 10.0);
    return candidate.expectedRiskReduction / cost >= minimumValuePerCost_
        ? EvidenceBudgetDecision{true, "positive_risk_reduction"}
        : EvidenceBudgetDecision{false, "insufficient_risk_reduction"};
}

RollingPercentileWindow::RollingPercentileWindow(std::size_t maximumSamples)
    : maximumSamples_(std::max<std::size_t>(1, maximumSamples)) {}

void RollingPercentileWindow::Record(double valueMs) {
    if (!std::isfinite(valueMs) || valueMs < 0.0) return;
    values_.push_back(valueMs);
    while (values_.size() > maximumSamples_) values_.pop_front();
}

double RollingPercentileWindow::P95() const {
    if (values_.empty()) return 0.0;
    std::vector<double> sorted(values_.begin(), values_.end());
    std::sort(sorted.begin(), sorted.end());
    const std::size_t index = static_cast<std::size_t>(std::ceil(sorted.size() * 0.95)) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

double ProcessCpuUsageTracker::Observe(
    long long timestampMs,
    double cumulativeProcessCpuMs,
    unsigned int logicalProcessorCount
) {
    if (!initialized_ || timestampMs <= previousTimestampMs_ || cumulativeProcessCpuMs < previousProcessCpuMs_) {
        previousTimestampMs_ = timestampMs;
        previousProcessCpuMs_ = cumulativeProcessCpuMs;
        initialized_ = true;
        return 0.0;
    }
    const double elapsedWallMs = static_cast<double>(timestampMs - previousTimestampMs_);
    const double elapsedProcessCpuMs = cumulativeProcessCpuMs - previousProcessCpuMs_;
    previousTimestampMs_ = timestampMs;
    previousProcessCpuMs_ = cumulativeProcessCpuMs;
    const double processors = static_cast<double>(std::max(1u, logicalProcessorCount));
    return std::clamp((elapsedProcessCpuMs / elapsedWallMs) * 100.0 / processors, 0.0, 100.0);
}
double CumulativeRateTracker::Observe(long long timestampMs, double cumulativeValue) {
    if (!initialized_ || timestampMs <= previousTimestampMs_ || cumulativeValue < previousValue_) {
        previousTimestampMs_ = timestampMs;
        previousValue_ = cumulativeValue;
        initialized_ = true;
        return 0.0;
    }
    const double elapsedMs = static_cast<double>(timestampMs - previousTimestampMs_);
    const double rate = (cumulativeValue - previousValue_) * 1000.0 / elapsedMs;
    previousTimestampMs_ = timestampMs;
    previousValue_ = cumulativeValue;
    return std::max(0.0, rate);
}