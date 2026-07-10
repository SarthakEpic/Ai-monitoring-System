#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "BackgroundAgent.h"
#include "BenchmarkProof.h"
#include "LowEndAutopilot.h"

using namespace std;

namespace {
void Require(bool condition, const string& message) {
    if (!condition) throw runtime_error(message);
}

ProcessSnapshot Candidate(
    unsigned long pid,
    const string& name,
    const string& category,
    double memoryMB,
    double expectedGainMB,
    double cpuPercent
) {
    ProcessSnapshot process;
    process.pid = pid;
    process.name = name;
    process.category = category;
    process.safety = "CANDIDATE";
    process.safetyScore = 88.0;
    process.privateBytesMB = memoryMB;
    process.workingSetMB = memoryMB + 40.0;
    process.expectedGainMB = expectedGainMB;
    process.cpuPercent = cpuPercent;
    process.wasteScore = 72.0;
    process.hasVisibleWindow = false;
    return process;
}

void VerifyLowEndAutopilotContract() {
    SystemSnapshot snapshot;
    snapshot.timestamp = 1'000;
    snapshot.cpuUsage = 76.0;
    snapshot.memoryUsage = 92.0;
    snapshot.totalMemoryMB = 4096.0;
    snapshot.availableMemoryMB = 328.0;
    snapshot.diskFree = 4.0;
    snapshot.intent.foregroundProcess = "video_editor.exe";

    ProcessSnapshot foreground = Candidate(101, "video_editor.exe", "FOREGROUND_APP", 900.0, 450.0, 42.0);
    foreground.isForeground = true;
    foreground.matchesUserIntent = true;
    foreground.hasVisibleWindow = true;
    foreground.safety = "USER_ACTIVE";

    ProcessSnapshot updater = Candidate(202, "cloud_sync.exe", "UPDATER_SYNC", 320.0, 220.0, 11.0);
    ProcessSnapshot browserHelper = Candidate(303, "browser_helper.exe", "BROWSER_CHILD", 260.0, 160.0, 8.0);
    ProcessSnapshot protectedProcess = Candidate(404, "security_service.exe", "BACKGROUND_HELPER", 500.0, 250.0, 15.0);
    protectedProcess.isSystemCritical = true;
    protectedProcess.safety = "PROTECTED";

    snapshot.processGenome = { foreground, updater, browserHelper, protectedProcess };

    DecisionResult decision;
    decision.level = RiskLevel::Critical;
    decision.riskScore = 84.0;
    decision.actionConfidence = 81.0;
    decision.recommendedAction = "lower_background_priority";

    HealPlan plan;
    plan.actionName = "lower_background_priority";
    plan.expectedGainMB = 220.0;

    AdaptiveBaselineResult baseline;
    baseline.status = "READY";
    baseline.ready = true;
    baseline.anomalyScore = 26.0;

    LowEndAutopilotConfig config;
    config.enabled = true;
    config.maxActionsPerCycle = 4;
    config.protectForegroundApp = true;
    config.preferReversibleActions = true;

    LowEndAutopilotEngine engine;
    const LowEndAutopilotResult result = engine.Evaluate(snapshot, decision, plan, baseline, config);

    Require(result.enabled, "autopilot should be enabled");
    Require(result.lowEndDevice, "4 GB device should be detected as low end");
    Require(result.active, "high pressure should activate autopilot");
    Require(result.foregroundProtected, "foreground protection must stay active");
    Require(result.actionsRecommended == 2, "only the two safe background candidates should be recommended");
    Require(result.reversibleActionCount == result.actionsRecommended, "every recommendation must be reversible");
    Require(result.quickRestoreAvailable, "reversible recommendation set should expose dry-run restore readiness");
    Require(result.userAppsTouched == 0, "safe plan must not touch visible user applications");
    Require(result.estimatedRecoveredRamMB > 0.0, "recovery estimate should be positive");

    for (const auto& recommendation : result.recommendations) {
        Require(recommendation.targetName != foreground.name, "foreground process entered recommendation queue");
        Require(recommendation.targetName != protectedProcess.name, "protected process entered recommendation queue");
        Require(recommendation.reversible, "non-reversible action entered recommendation queue");
    }

    BackgroundAgentConfig agentConfig;
    agentConfig.enabled = true;
    agentConfig.trayIconEnabled = true;
    agentConfig.trayIconInstalled = true;
    agentConfig.silentMonitoring = true;
    agentConfig.dashboardVisible = false;

    BackgroundAgentEngine agentEngine;
    const BackgroundAgentResult agent = agentEngine.Evaluate(snapshot, result, agentConfig);
    Require(agent.trayIconReady, "tray agent should report ready");
    Require(agent.silentMonitoring, "silent monitoring should be active");
    Require(agent.quickRestoreAvailable, "agent should expose reversible dry-run reset");
    Require(agent.controlCenter == "tray", "hidden dashboard should move control center to tray");

    BenchmarkProofEngine proofEngine;
    const BenchmarkProofResult proof = proofEngine.Build(snapshot, decision, plan, result);
    Require(proof.status == "PROOF_READY", "safe plan should produce proof-ready estimate");
    Require(proof.actionsRecommended == result.actionsRecommended, "proof must not double-count the decision plan");
    Require(proof.userAppsTouched == 0, "proof must retain zero user-app impact");
    Require(proof.afterCpuEstimate <= proof.beforeCpu, "CPU estimate must not increase after optimization");
    Require(proof.afterMemoryEstimate <= proof.beforeMemory, "memory estimate must not increase after optimization");
    Require(proof.afterRiskEstimate <= proof.beforeRisk, "risk estimate must not increase after optimization");
    Require(proof.recoveredRamMB > 0.0, "proof should report recovered memory");
}

void VerifyDisabledAutopilotContract() {
    SystemSnapshot snapshot;
    snapshot.timestamp = 2'000;
    snapshot.totalMemoryMB = 4096.0;

    DecisionResult decision;
    HealPlan plan;
    AdaptiveBaselineResult baseline;
    LowEndAutopilotConfig config;
    config.enabled = false;

    LowEndAutopilotEngine engine;
    const LowEndAutopilotResult result = engine.Evaluate(snapshot, decision, plan, baseline, config);
    Require(!result.enabled, "disabled config must disable autopilot");
    Require(result.status == "DISABLED", "disabled autopilot must report DISABLED");
    Require(result.actionsRecommended == 0, "disabled autopilot must not recommend actions");
}
}

int main() {
    try {
        VerifyLowEndAutopilotContract();
        VerifyDisabledAutopilotContract();
        cout << "Stage 9-11 autopilot contracts passed.\n";
        return 0;
    } catch (const exception& error) {
        cerr << "Stage 9-11 autopilot contract failure: " << error.what() << "\n";
        return 1;
    }
}
