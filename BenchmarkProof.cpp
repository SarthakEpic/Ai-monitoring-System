#include "BenchmarkProof.h"

#include <algorithm>
#include <sstream>

using namespace std;

namespace {
double ClampPercent(double value) {
    return max(0.0, min(100.0, value));
}
}

BenchmarkProofResult BenchmarkProofEngine::Build(
    const SystemSnapshot& snapshot,
    const DecisionResult& decision,
    const HealPlan& plan,
    const LowEndAutopilotResult& autopilot
) const {
    BenchmarkProofResult result;
    result.timestamp = snapshot.timestamp;
    result.beforeCpu = ClampPercent(snapshot.cpuUsage);
    result.beforeMemory = ClampPercent(snapshot.memoryUsage);
    result.beforeDiskFree = ClampPercent(snapshot.diskFree);
    result.beforeRisk = ClampPercent(decision.riskScore);
    result.foregroundProcess = snapshot.intent.foregroundProcess;
    result.actionsRecommended = max(autopilot.actionsRecommended, plan.actionName != "monitor_only" ? 1 : 0);
    result.userAppsTouched = autopilot.userAppsTouched;
    result.recoveredRamMB = max(autopilot.estimatedRecoveredRamMB, plan.expectedGainMB);
    result.cpuDropPercent = ClampPercent(autopilot.estimatedCpuDropPercent);

    double memoryDropPercent = 0.0;
    if (snapshot.totalMemoryMB > 0.0) {
        memoryDropPercent = ClampPercent((result.recoveredRamMB / snapshot.totalMemoryMB) * 100.0);
    } else {
        memoryDropPercent = min(18.0, result.recoveredRamMB / 96.0);
    }

    if (decision.rootCause == "disk" || snapshot.diskFree <= 8.0) {
        result.diskFreeGainPercent = plan.actionName == "review_disk_cleanup" ? 3.0 : 1.0;
    }

    result.afterCpuEstimate = ClampPercent(result.beforeCpu - result.cpuDropPercent);
    result.afterMemoryEstimate = ClampPercent(result.beforeMemory - memoryDropPercent);
    result.afterDiskFreeEstimate = ClampPercent(result.beforeDiskFree + result.diskFreeGainPercent);
    result.riskDropPercent = ClampPercent(
        (result.cpuDropPercent * 0.35) +
        (memoryDropPercent * 0.45) +
        (result.diskFreeGainPercent * 2.0) +
        (result.actionsRecommended > 0 ? 4.0 : 0.0)
    );
    result.afterRiskEstimate = ClampPercent(result.beforeRisk - result.riskDropPercent);
    result.confidence = ClampPercent(
        (autopilot.quickRestoreAvailable ? 18.0 : 0.0) +
        (autopilot.foregroundProtected ? 22.0 : 0.0) +
        (decision.actionConfidence * 0.35) +
        (autopilot.pressureScore * 0.25)
    );

    if (result.actionsRecommended == 0) {
        result.status = "COLLECTING";
        result.summary = "No impact estimate yet; collect pressure samples.";
    } else if (result.userAppsTouched == 0) {
        result.status = "PROOF_READY";
        ostringstream oss;
        oss << "Before/after estimate ready: RAM -" << static_cast<int>(memoryDropPercent)
            << "%, CPU -" << static_cast<int>(result.cpuDropPercent)
            << "%, recovered " << static_cast<int>(result.recoveredRamMB)
            << " MB, user apps touched 0.";
        result.summary = oss.str();
    } else {
        result.status = "REVIEW_REQUIRED";
        result.summary = "Simulation has candidates, but a visible user app requires review.";
    }

    return result;
}
