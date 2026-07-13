#include <cassert>

#include "DecisionEngine.h"

namespace {
SystemSnapshot SafeCandidateSnapshot() {
    SystemSnapshot snapshot;
    snapshot.timestamp = 1000;
    snapshot.cpuUsage = 92.0;
    snapshot.memoryUsage = 90.0;
    snapshot.diskFree = 30.0;
    snapshot.topProcess.pid = 4242;
    snapshot.topProcess.name = "background-worker.exe";
    snapshot.topProcess.category = "BACKGROUND";
    snapshot.topProcess.safety = "SAFE";
    snapshot.topProcess.cpuPercent = 40.0;
    snapshot.topProcess.memoryMB = 900.0;
    snapshot.topProcess.privateMemoryMB = 900.0;
    snapshot.topProcess.wasteScore = 90.0;
    snapshot.topProcess.safetyScore = 95.0;
    snapshot.topProcess.expectedGainMB = 400.0;
    ProcessSnapshot candidate;
    candidate.pid = 4242;
    candidate.name = "background-worker.exe";
    candidate.category = "BACKGROUND";
    candidate.safety = "CANDIDATE";
    candidate.cpuPercent = 40.0;
    candidate.workingSetMB = 900.0;
    candidate.privateBytesMB = 900.0;
    candidate.wasteScore = 90.0;
    candidate.safetyScore = 95.0;
    candidate.expectedGainMB = 400.0;
    snapshot.processGenome.push_back(candidate);
    return snapshot;
}

void TestLegacyConfidenceCannotAuthorizeHealing() {
    DecisionEngine engine;
    DecisionThresholds thresholds;
    thresholds.warningRiskThreshold = 40.0;
    thresholds.criticalRiskThreshold = 60.0;
    DecisionPolicy policy;
    policy.autoHealEnabled = true;
    policy.dryRun = false;
    policy.safeMode = false;
    policy.allowlistCsv = "background-worker.exe";

    const DecisionResult result = engine.Evaluate(
        SafeCandidateSnapshot(), 99.0, 100.0, "MODEL", "legacy model", thresholds, {}, policy
    );

    assert(result.level == RiskLevel::Critical);
    assert(!result.safeToHeal);
    assert(result.dryRun);
    assert(result.safetyGate == "MODEL_CALIBRATION_REQUIRED");
}
}  // namespace

int main() {
    TestLegacyConfidenceCannotAuthorizeHealing();
    return 0;
}