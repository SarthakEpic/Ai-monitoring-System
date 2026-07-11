#include "PerformanceIntelligence.h"

#include <iostream>
#include <stdexcept>

using namespace std;

namespace {

void Require(bool condition, const string& message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

SystemSnapshot BaseSnapshot() {
    SystemSnapshot snapshot;
    snapshot.intent.foregroundPid = 10;
    snapshot.intent.foregroundProcess = "code.exe";
    snapshot.intent.appKind = "IDE";
    snapshot.intent.userState = "ACTIVE";
    snapshot.intent.focusDurationSeconds = 30.0;

    ProcessSnapshot foreground;
    foreground.pid = 10;
    foreground.parentPid = 1;
    foreground.name = "code.exe";
    foreground.isForeground = true;
    foreground.matchesUserIntent = true;

    ProcessSnapshot child;
    child.pid = 11;
    child.parentPid = 10;
    child.name = "language-server.exe";

    ProcessSnapshot background;
    background.pid = 20;
    background.parentPid = 1;
    background.name = "updater.exe";
    background.safety = "SAFE";
    background.safetyScore = 90.0;

    snapshot.processGenome = {foreground, child, background};
    return snapshot;
}

void VerifyCriticalityGraph() {
    PerformanceCriticalityEngine engine;
    const PerformanceCriticalityGraph graph = engine.Build(
        BaseSnapshot(),
        WorkloadPhase::ActiveInteraction
    );
    Require(graph.nodes.size() == 3, "criticality graph lost processes");

    const auto findNode = [&](DWORD pid) -> const CriticalityNode& {
        for (const auto& node : graph.nodes) {
            if (node.pid == pid) {
                return node;
            }
        }
        throw runtime_error("node missing");
    };

    Require(
        findNode(10).criticality == 100.0 && findNode(10).protectedFromIntervention,
        "foreground was not maximally protected"
    );
    Require(
        findNode(11).criticality >= 90.0 && findNode(11).onForegroundPath,
        "foreground child dependency was not protected"
    );
    Require(
        findNode(20).criticality < 70.0 && !findNode(20).protectedFromIntervention,
        "unrelated background process was over-protected"
    );
}

void VerifyWorkloadPhases() {
    WorkloadPhaseDetector detector;
    SystemSnapshot snapshot = BaseSnapshot();
    snapshot.intent.appKind = "COMMUNICATION";
    Require(
        detector.Detect(snapshot) == WorkloadPhase::VideoMeeting,
        "communication workload not detected"
    );

    snapshot.intent.appKind = "GAME";
    Require(detector.Detect(snapshot) == WorkloadPhase::Gaming, "game workload not detected");

    snapshot.intent.appKind = "IDE";
    snapshot.intent.userState = "AWAY";
    snapshot.intent.idleSeconds = 600.0;
    Require(detector.Detect(snapshot) == WorkloadPhase::UserIdle, "idle workload not detected");
}

void VerifyCollectorCapability() {
    QoeTelemetryCollector collector;
    string error;
    const bool initialized = collector.Initialize(error);
    Require(initialized || !error.empty(), "collector failure did not expose capability reason");

    if (initialized) {
        const SystemSnapshot snapshot = BaseSnapshot();
        const auto sample = collector.Capture(snapshot, WorkloadPhase::ActiveInteraction);
        Require(
            sample.recommendedSampleIntervalMs == 1000 ||
                sample.recommendedSampleIntervalMs == 5000,
            "invalid adaptive interval"
        );
    }
}

}  // namespace

int main() {
    try {
        VerifyCriticalityGraph();
        VerifyWorkloadPhases();
        VerifyCollectorCapability();
        cout << "PerformanceIntelligenceTests passed\n";
        return 0;
    } catch (const exception& ex) {
        cerr << "PerformanceIntelligenceTests failed: " << ex.what() << '\n';
        return 1;
    }
}
