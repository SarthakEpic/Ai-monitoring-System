#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

enum class SamplingTier {
    Tier0Cheap,
    Tier1Risk,
    Tier2Focused,
    Tier3Expensive,
};

struct AdaptiveSamplingInputs {
    bool elevatedRisk = false;
    bool severePressure = false;
    bool foregroundChanged = false;
    bool dataInvalid = false;
    bool safetyEvent = false;
};

struct AdaptiveSamplingDecision {
    bool capture = false;
    bool immediate = false;
    SamplingTier tier = SamplingTier::Tier0Cheap;
    int targetIntervalMs = 5000;
    std::string reason = "stable_interval";
};

struct AdaptiveSamplingConfig {
    int stableIntervalMs = 5000;
    int elevatedRiskIntervalMs = 1000;
    int minimumImmediateIntervalMs = 500;
    int maximumFocusedBurstMs = 30000;
    int overloadBackoffMultiplier = 2;
};

class AdaptiveSamplingController {
public:
    explicit AdaptiveSamplingController(AdaptiveSamplingConfig config = {});

    AdaptiveSamplingDecision Next(long long nowMs, const AdaptiveSamplingInputs& inputs);
    void RecordCapture(long long nowMs, bool withinOverheadBudget);
    void RecordCollectorFailure(long long nowMs);
    void ClearFailureBackoff();

    [[nodiscard]] long long LastCaptureMs() const { return lastCaptureMs_; }
    [[nodiscard]] int CaptureCount() const { return captureCount_; }

private:
    int EffectiveIntervalMs(const AdaptiveSamplingInputs& inputs) const;

    AdaptiveSamplingConfig config_;
    long long lastCaptureMs_ = -1;
    long long focusedBurstStartedMs_ = -1;
    long long failureBackoffUntilMs_ = 0;
    int captureCount_ = 0;
    bool overloadBackoff_ = false;
};

struct CollectorCostSample {
    long long timestampMs = 0;
    double durationMs = 0.0;
    bool succeeded = true;
};

struct CollectorCostReport {
    SamplingTier tier = SamplingTier::Tier0Cheap;
    double lastDurationMs = 0.0;
    double p50DurationMs = 0.0;
    double p95DurationMs = 0.0;
    int minimumIntervalMs = 0;
    int maximumBurstMs = 0;
    long long failureBackoffUntilMs = 0;
    bool healthy = true;
};

class CollectorCostRegistry {
public:
    void Configure(
        std::string collector,
        SamplingTier tier,
        int minimumIntervalMs,
        int maximumBurstMs
    );
    void Record(std::string collector, CollectorCostSample sample, long long failureBackoffUntilMs = 0);
    [[nodiscard]] CollectorCostReport Report(const std::string& collector) const;

private:
    struct Entry {
        CollectorCostReport report;
        std::deque<CollectorCostSample> samples;
    };

    static double Percentile(const std::deque<CollectorCostSample>& samples, double percentile);

    std::unordered_map<std::string, Entry> entries_;
};
