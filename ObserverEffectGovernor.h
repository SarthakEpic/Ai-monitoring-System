#pragma once

#include <deque>
#include <string>

#include "AdaptiveSampling.h"

struct ObserverEffectConfig {
    double maximumCollectorP95Ms = 15.0;
    double maximumInferenceP95Ms = 20.0;
    double maximumProcessCpuPercent = 1.0;
    double maximumProcessIoBytesPerSecond = 1048576.0;
    double maximumWakeupsPerSecond = 2.0;
    double maximumWorkingSetMb = 100.0;
    std::size_t maximumPendingWrites = 64;
    int recoverySamplesRequired = 3;
};

struct ObserverEffectSample {
    double collectorP95Ms = 0.0;
    double inferenceP95Ms = 0.0;
    double processCpuPercent = 0.0;
    double workingSetMb = 0.0;
    double processIoBytesPerSecond = 0.0;
    double wakeupsPerSecond = 0.0;
    std::size_t pendingWrites = 0;
    bool rollbackWatchdogHealthy = true;
};

struct ObserverEffectDecision {
    bool overBudget = false;
    bool slowOptionalSampling = false;
    bool disableTier3 = false;
    bool preserveSafetyMonitoring = true;
    bool preserveRollbackWatchdog = true;
    SamplingTier maximumOptionalTier = SamplingTier::Tier3Expensive;
    std::string reason = "within_budget";
};

// Prevents the monitor itself from creating sustained device pressure. It can
// reduce optional evidence collection, but never disables safety or rollback paths.
class ObserverEffectGovernor {
public:
    explicit ObserverEffectGovernor(ObserverEffectConfig config = {});

    ObserverEffectDecision Observe(const ObserverEffectSample& sample);
    [[nodiscard]] ObserverEffectDecision CurrentDecision() const { return decision_; }

private:
    ObserverEffectConfig config_;
    ObserverEffectDecision decision_;
    int consecutiveHealthySamples_ = 0;
};

struct EvidenceCandidate {
    SamplingTier tier = SamplingTier::Tier0Cheap;
    double expectedRiskReduction = 0.0;
    double estimatedCpuCost = 0.0;
    double estimatedLatencyCostMs = 0.0;
    bool safetyRequired = false;
};

struct EvidenceBudgetDecision {
    bool collect = false;
    std::string reason = "no_expected_value";
};

// Chooses optional evidence only when its estimated decision-risk reduction
// justifies the measured CPU/latency cost. Safety-required evidence bypasses it.
class EvidenceBudgetScheduler {
public:
    explicit EvidenceBudgetScheduler(double minimumValuePerCost = 0.20);

    EvidenceBudgetDecision Decide(const EvidenceCandidate& candidate, const ObserverEffectDecision& governor) const;

private:
    double minimumValuePerCost_ = 0.20;
};

// Bounded measurement utilities used by the runtime observer governor. They are
// platform-neutral so percentile and CPU arithmetic can be verified in unit tests.
class RollingPercentileWindow {
public:
    explicit RollingPercentileWindow(std::size_t maximumSamples = 60);
    void Record(double valueMs);
    [[nodiscard]] double P95() const;

private:
    std::size_t maximumSamples_ = 60;
    std::deque<double> values_;
};

class ProcessCpuUsageTracker {
public:
    // cumulativeProcessCpuMs is process user+kernel time; logicalProcessorCount
    // normalizes the result to total machine CPU percentage.
    double Observe(long long timestampMs, double cumulativeProcessCpuMs, unsigned int logicalProcessorCount);

private:
    long long previousTimestampMs_ = 0;
    double previousProcessCpuMs_ = 0.0;
    bool initialized_ = false;
};
class CumulativeRateTracker {
public:
    double Observe(long long timestampMs, double cumulativeValue);
private:
    long long previousTimestampMs_ = 0;
    double previousValue_ = 0.0;
    bool initialized_ = false;
};