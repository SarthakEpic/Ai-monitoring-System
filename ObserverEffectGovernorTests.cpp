#include <stdexcept>
#include <string>

#include "ObserverEffectGovernor.h"

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void TestOverheadDegradesOnlyOptionalEvidence() {
    ObserverEffectGovernor governor;
    ObserverEffectSample sample;
    sample.collectorP95Ms = 20.0;
    const ObserverEffectDecision decision = governor.Observe(sample);
    Require(decision.overBudget && decision.slowOptionalSampling && decision.disableTier3, "overhead did not degrade optional evidence");
    Require(decision.preserveSafetyMonitoring && decision.preserveRollbackWatchdog, "overhead degradation weakened safety monitoring");
    Require(decision.maximumOptionalTier == SamplingTier::Tier1Risk, "governor did not cap optional tier");
}

void TestRecoveryRequiresStableSamples() {
    ObserverEffectConfig config;
    config.recoverySamplesRequired = 2;
    ObserverEffectGovernor governor(config);
    ObserverEffectSample overloaded;
    overloaded.pendingWrites = 100;
    governor.Observe(overloaded);
    ObserverEffectSample healthy;
    Require(governor.Observe(healthy).overBudget, "governor recovered too quickly");
    Require(!governor.Observe(healthy).overBudget, "governor did not recover after healthy samples");
}

void TestEvidenceBudgetHonorsSafetyAndGovernor() {
    ObserverEffectGovernor governor;
    ObserverEffectSample overloaded;
    overloaded.workingSetMb = 200.0;
    const ObserverEffectDecision constrained = governor.Observe(overloaded);
    EvidenceBudgetScheduler scheduler;
    EvidenceCandidate expensive;
    expensive.tier = SamplingTier::Tier3Expensive;
    expensive.expectedRiskReduction = 100.0;
    Require(!scheduler.Decide(expensive, constrained).collect, "governor allowed tier3 while over budget");
    expensive.safetyRequired = true;
    Require(scheduler.Decide(expensive, constrained).collect, "safety-required evidence was blocked");
}

void TestMeasuredOverheadUtilities() {
    RollingPercentileWindow latencies(4);
    latencies.Record(1.0);
    latencies.Record(2.0);
    latencies.Record(3.0);
    latencies.Record(20.0);
    Require(latencies.P95() == 20.0, "rolling p95 did not retain tail latency");

    ProcessCpuUsageTracker cpu;
    Require(cpu.Observe(1000, 100.0, 4) == 0.0, "first CPU sample must initialize only");
    Require(cpu.Observe(2000, 300.0, 4) == 5.0, "process CPU normalization is incorrect");

    CumulativeRateTracker rate;
    Require(rate.Observe(1000, 100.0) == 0.0, "first rate sample must initialize only");
    Require(rate.Observe(2000, 600.0) == 500.0, "cumulative rate calculation is incorrect");
    Require(rate.Observe(1500, 700.0) == 0.0, "time regression must reset the rate tracker");
}
}  // namespace

int main() {
    try {
        TestOverheadDegradesOnlyOptionalEvidence();
        TestRecoveryRequiresStableSamples();
        TestEvidenceBudgetHonorsSafetyAndGovernor();
        TestMeasuredOverheadUtilities();
        return 0;
    } catch (const std::exception&) {
        return 1;
    }
}