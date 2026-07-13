#include "AdaptiveSampling.h"

#include <algorithm>
#include <cmath>
#include <utility>

AdaptiveSamplingController::AdaptiveSamplingController(AdaptiveSamplingConfig config)
    : config_(std::move(config)) {
    config_.stableIntervalMs = std::max(1000, config_.stableIntervalMs);
    config_.elevatedRiskIntervalMs = std::max(500, config_.elevatedRiskIntervalMs);
    config_.minimumImmediateIntervalMs = std::max(100, config_.minimumImmediateIntervalMs);
    config_.maximumFocusedBurstMs = std::max(config_.elevatedRiskIntervalMs, config_.maximumFocusedBurstMs);
    config_.overloadBackoffMultiplier = std::max(1, config_.overloadBackoffMultiplier);
}

AdaptiveSamplingDecision AdaptiveSamplingController::Next(long long nowMs, const AdaptiveSamplingInputs& inputs) {
    AdaptiveSamplingDecision decision;
    const bool forcedEscalation = inputs.foregroundChanged || inputs.dataInvalid || inputs.safetyEvent || inputs.severePressure;
    const bool focused = forcedEscalation || inputs.elevatedRisk;
    const int intervalMs = EffectiveIntervalMs(inputs);
    decision.targetIntervalMs = intervalMs;
    decision.tier = focused ? SamplingTier::Tier2Focused : SamplingTier::Tier0Cheap;

    if (lastCaptureMs_ < 0) {
        decision.capture = true;
        decision.reason = "initial_sample";
        return decision;
    }

    if (nowMs < failureBackoffUntilMs_ && !inputs.safetyEvent) {
        decision.reason = "collector_failure_backoff";
        return decision;
    }

    const long long elapsedMs = std::max(0LL, nowMs - lastCaptureMs_);
    if (forcedEscalation && elapsedMs >= config_.minimumImmediateIntervalMs) {
        decision.capture = true;
        decision.immediate = true;
        decision.reason = inputs.foregroundChanged ? "foreground_changed" : "forced_escalation";
        return decision;
    }

    if (focused) {
        if (focusedBurstStartedMs_ < 0) focusedBurstStartedMs_ = nowMs;
        const bool burstExpired = nowMs - focusedBurstStartedMs_ >= config_.maximumFocusedBurstMs;
        if (burstExpired && !inputs.severePressure && !inputs.safetyEvent) {
            decision.tier = SamplingTier::Tier1Risk;
            decision.targetIntervalMs = config_.stableIntervalMs * (overloadBackoff_ ? config_.overloadBackoffMultiplier : 1);
            decision.reason = "focused_burst_capped";
            decision.capture = elapsedMs >= decision.targetIntervalMs;
            return decision;
        }
        decision.reason = "elevated_risk";
        decision.capture = elapsedMs >= intervalMs;
        return decision;
    }

    focusedBurstStartedMs_ = -1;
    decision.reason = overloadBackoff_ ? "overhead_backoff" : "stable_interval";
    decision.capture = elapsedMs >= intervalMs;
    return decision;
}

void AdaptiveSamplingController::RecordCapture(long long nowMs, bool withinOverheadBudget) {
    lastCaptureMs_ = nowMs;
    ++captureCount_;
    overloadBackoff_ = !withinOverheadBudget;
}

void AdaptiveSamplingController::RecordCollectorFailure(long long nowMs) {
    failureBackoffUntilMs_ = nowMs + (config_.stableIntervalMs * config_.overloadBackoffMultiplier);
}

void AdaptiveSamplingController::ClearFailureBackoff() {
    failureBackoffUntilMs_ = 0;
}

int AdaptiveSamplingController::EffectiveIntervalMs(const AdaptiveSamplingInputs& inputs) const {
    int intervalMs = (inputs.elevatedRisk || inputs.severePressure || inputs.foregroundChanged || inputs.dataInvalid || inputs.safetyEvent)
        ? config_.elevatedRiskIntervalMs
        : config_.stableIntervalMs;
    if (overloadBackoff_ && !inputs.safetyEvent && !inputs.severePressure) {
        intervalMs *= config_.overloadBackoffMultiplier;
    }
    return intervalMs;
}

void CollectorCostRegistry::Configure(
    std::string collector,
    SamplingTier tier,
    int minimumIntervalMs,
    int maximumBurstMs) {
    Entry& entry = entries_[std::move(collector)];
    entry.report.tier = tier;
    entry.report.minimumIntervalMs = minimumIntervalMs;
    entry.report.maximumBurstMs = maximumBurstMs;
}

void CollectorCostRegistry::Record(std::string collector, CollectorCostSample sample, long long failureBackoffUntilMs) {
    Entry& entry = entries_[std::move(collector)];
    entry.samples.push_back(sample);
    while (entry.samples.size() > 64) entry.samples.pop_front();
    entry.report.lastDurationMs = sample.durationMs;
    entry.report.p50DurationMs = Percentile(entry.samples, 0.50);
    entry.report.p95DurationMs = Percentile(entry.samples, 0.95);
    entry.report.healthy = sample.succeeded;
    entry.report.failureBackoffUntilMs = failureBackoffUntilMs;
}

CollectorCostReport CollectorCostRegistry::Report(const std::string& collector) const {
    const auto it = entries_.find(collector);
    return it == entries_.end() ? CollectorCostReport{} : it->second.report;
}

double CollectorCostRegistry::Percentile(const std::deque<CollectorCostSample>& samples, double percentile) {
    if (samples.empty()) return 0.0;
    std::vector<double> values;
    values.reserve(samples.size());
    for (const CollectorCostSample& sample : samples) values.push_back(sample.durationMs);
    std::sort(values.begin(), values.end());
    const double position = std::clamp(percentile, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    return values[static_cast<std::size_t>(std::ceil(position))];
}
