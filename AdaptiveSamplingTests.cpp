#include <stdexcept>
#include <string>

#include "AdaptiveSampling.h"

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void TestStableScheduleAvoidsOneSecondCapture() {
    AdaptiveSamplingController controller;
    const AdaptiveSamplingInputs stable{};
    Require(controller.Next(0, stable).capture, "initial sample must capture");
    controller.RecordCapture(0, true);
    for (long long nowMs = 1000; nowMs < 5000; nowMs += 1000) {
        Require(!controller.Next(nowMs, stable).capture, "stable collector captured before five-second interval");
    }
    Require(controller.Next(5000, stable).capture, "stable collector did not capture on schedule");
}

void TestForegroundChangeEscalatesImmediately() {
    AdaptiveSamplingController controller;
    controller.RecordCapture(0, true);
    AdaptiveSamplingInputs changed;
    changed.foregroundChanged = true;
    const AdaptiveSamplingDecision decision = controller.Next(500, changed);
    Require(decision.capture && decision.immediate, "foreground change did not force bounded immediate capture");
}

void TestFocusedBurstIsCapped() {
    AdaptiveSamplingConfig config;
    config.maximumFocusedBurstMs = 3000;
    config.stableIntervalMs = 5000;
    config.elevatedRiskIntervalMs = 1000;
    AdaptiveSamplingController controller(config);
    controller.RecordCapture(0, true);
    AdaptiveSamplingInputs elevated;
    elevated.elevatedRisk = true;
    Require(controller.Next(1000, elevated).capture, "elevated risk should capture at focused interval");
    controller.RecordCapture(1000, true);
    const AdaptiveSamplingDecision capped = controller.Next(4000, elevated);
    Require(capped.reason == "focused_burst_capped", "focused burst was not capped");
    Require(!capped.capture, "capped focused burst captured before stable interval");
}

void TestFailureBackoffAndSafetyOverride() {
    AdaptiveSamplingController controller;
    controller.RecordCapture(0, true);
    controller.RecordCollectorFailure(1000);
    AdaptiveSamplingInputs elevated;
    elevated.elevatedRisk = true;
    Require(!controller.Next(2000, elevated).capture, "failure backoff did not suppress optional capture");
    AdaptiveSamplingInputs safety;
    safety.safetyEvent = true;
    Require(controller.Next(2000, safety).capture, "safety event did not override failure backoff");
}

void TestCostRegistryTracksPercentiles() {
    CollectorCostRegistry registry;
    registry.Configure("qoe", SamplingTier::Tier2Focused, 1000, 30000);
    registry.Record("qoe", {1, 2.0, true});
    registry.Record("qoe", {2, 10.0, true});
    registry.Record("qoe", {3, 6.0, false}, 5000);
    const CollectorCostReport report = registry.Report("qoe");
    Require(report.lastDurationMs == 6.0, "collector cost last duration missing");
    Require(report.p50DurationMs >= 6.0 && report.p95DurationMs == 10.0, "collector cost percentiles incorrect");
    Require(!report.healthy && report.failureBackoffUntilMs == 5000, "collector failure health missing");
}

}  // namespace

int main() {
    try {
        TestStableScheduleAvoidsOneSecondCapture();
        TestForegroundChangeEscalatesImmediately();
        TestFocusedBurstIsCapped();
        TestFailureBackoffAndSafetyOverride();
        TestCostRegistryTracksPercentiles();
        return 0;
    } catch (const std::exception&) {
        return 1;
    }
}
