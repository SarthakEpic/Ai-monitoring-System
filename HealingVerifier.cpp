#include "HealingVerifier.h"

#include <algorithm>
#include <sstream>

using namespace std;

namespace {
double ClampPercent(double value) {
    return max(0.0, min(100.0, value));
}

double ClampNonNegative(double value) {
    return max(0.0, value);
}

string BuildVerificationId(const SystemSnapshot& snapshot, const HealPlan& plan) {
    return "HV-" + to_string(snapshot.timestamp) + "-" + plan.planId;
}

string PercentText(double value) {
    return to_string(static_cast<int>(ClampPercent(value))) + "%";
}

double EstimateMemoryPercentDrop(const DecisionResult& decision) {
    if (decision.expectedGainMB <= 0.0) return 0.0;
    return min(10.0, max(1.0, decision.expectedGainMB / 128.0));
}

double EstimateCpuPercentDrop(const DecisionResult& decision) {
    return min(18.0, max(0.0, decision.candidate.cpuPercent * 0.35));
}

double EstimateNetworkDropKBps(const SystemSnapshot& snapshot) {
    const double total = snapshot.netDownKBps + snapshot.netUpKBps;
    return min(total * 0.35, 1024.0);
}

double EstimateRiskDelta(const HealPlan& plan, const DecisionResult& decision, double primaryImprovement) {
    double delta = primaryImprovement;
    delta += min(10.0, decision.actionConfidence / 12.0);
    delta += min(8.0, plan.readinessScore / 14.0);
    if (decision.level == RiskLevel::Critical) delta += 4.0;
    if (plan.status == "BLOCKED" || plan.status == "COOLDOWN") delta *= 0.35;
    if (plan.status == "REVIEW_REQUIRED") delta *= 0.55;
    return ClampPercent(delta);
}

void ClassifyOutcome(HealVerification& verification, const HealPlan& plan) {
    if (plan.status == "OBSERVE") {
        verification.status = "NOT_NEEDED";
        verification.outcomeLabel = "NO_ACTION";
        verification.summary = "No verification needed";
        verification.reason = "Risk is below action threshold";
        return;
    }

    if (plan.status == "BLOCKED" || plan.status == "COOLDOWN") {
        verification.status = "SIMULATED_BLOCKED";
        verification.outcomeLabel = "BLOCKED_BY_SAFETY";
        verification.blocked = true;
        verification.summary = "Verification blocked by safety gate";
        verification.reason = plan.blockedReason;
        return;
    }

    if (verification.riskDeltaEstimate >= 7.0 && plan.readinessScore >= 45.0) {
        verification.status = "SIMULATED_PASS";
        verification.outcomeLabel = "LIKELY_HELPFUL";
        verification.simulatedPass = true;
        verification.summary = "Simulation predicts meaningful improvement";
    } else if (verification.riskDeltaEstimate >= 3.0 && plan.readinessScore >= 30.0) {
        verification.status = "SIMULATED_WEAK";
        verification.outcomeLabel = "POSSIBLY_HELPFUL";
        verification.simulatedWeak = true;
        verification.summary = "Simulation predicts small improvement";
    } else {
        verification.status = "SIMULATED_FAIL";
        verification.outcomeLabel = "UNLIKELY_TO_HELP";
        verification.simulatedFail = true;
        verification.summary = "Simulation does not predict enough improvement";
    }

    verification.reason =
        "Estimated risk change " + PercentText(verification.riskDeltaEstimate) +
        " with readiness " + PercentText(plan.readinessScore) + ".";
}
}

HealVerification HealingVerifier::Evaluate(
    const SystemSnapshot& snapshot,
    const DecisionResult& decision,
    const HealPlan& plan
) const {
    HealVerification verification;
    verification.verificationId = BuildVerificationId(snapshot, plan);
    verification.mode = plan.executionMode;
    verification.confidence = min(plan.confidence, plan.readinessScore);
    verification.riskBefore = decision.riskScore;
    verification.riskAfterEstimate = decision.riskScore;
    verification.cpuBefore = snapshot.cpuUsage;
    verification.cpuAfterEstimate = snapshot.cpuUsage;
    verification.memoryBefore = snapshot.memoryUsage;
    verification.memoryAfterEstimate = snapshot.memoryUsage;
    verification.diskBefore = snapshot.diskFree;
    verification.diskAfterEstimate = snapshot.diskFree;
    verification.networkBeforeKBps = snapshot.netDownKBps + snapshot.netUpKBps;
    verification.networkAfterEstimateKBps = verification.networkBeforeKBps;
    verification.expectedGainMB = plan.expectedGainMB;

    double primaryImprovement = 0.0;

    if (plan.actionType == "process_priority") {
        const double cpuDrop = EstimateCpuPercentDrop(decision);
        verification.cpuAfterEstimate = ClampPercent(snapshot.cpuUsage - cpuDrop);
        primaryImprovement = cpuDrop;
        verification.successCriteria = "CPU drops by at least 5% and foreground responsiveness is not harmed";
        verification.failureCriteria = "CPU remains high, target becomes foreground, or user activity is affected";
        verification.evidence =
            "Target CPU " + PercentText(decision.candidate.cpuPercent) +
            ", safety " + PercentText(decision.candidateSafetyScore) + ".";
    } else if (plan.actionType == "memory_pressure") {
        const double memDrop = EstimateMemoryPercentDrop(decision);
        verification.memoryAfterEstimate = ClampPercent(snapshot.memoryUsage - memDrop);
        primaryImprovement = memDrop;
        verification.successCriteria = "Memory drops by at least 3% without closing foreground work";
        verification.failureCriteria = "Memory pressure returns quickly or target reload causes worse pressure";
        verification.evidence =
            "Expected reclaim " + to_string(static_cast<int>(decision.expectedGainMB)) +
            " MB from " + decision.actionTarget + ".";
    } else if (plan.actionType == "background_sleep") {
        const double memDrop = EstimateMemoryPercentDrop(decision) * 0.8;
        const double cpuDrop = EstimateCpuPercentDrop(decision) * 0.8;
        verification.memoryAfterEstimate = ClampPercent(snapshot.memoryUsage - memDrop);
        verification.cpuAfterEstimate = ClampPercent(snapshot.cpuUsage - cpuDrop);
        primaryImprovement = memDrop + cpuDrop;
        verification.successCriteria = "CPU or memory improves while active user app remains protected";
        verification.failureCriteria = "Target was needed by foreground work or pressure does not improve";
        verification.evidence = "Candidate is background-only with readiness " + PercentText(plan.readinessScore) + ".";
    } else if (plan.actionType == "disk_cleanup") {
        const double diskGain = min(8.0, max(1.0, decision.riskScore / 14.0));
        verification.diskAfterEstimate = ClampPercent(snapshot.diskFree + diskGain);
        primaryImprovement = diskGain;
        verification.successCriteria = "Disk free rises above the configured disk threshold";
        verification.failureCriteria = "Disk stays below threshold or cleanup risks user files";
        verification.evidence = "Disk free is " + PercentText(snapshot.diskFree) + " before cleanup review.";
    } else if (plan.actionType == "network_review") {
        const double networkDrop = EstimateNetworkDropKBps(snapshot);
        verification.networkAfterEstimateKBps = ClampNonNegative(verification.networkBeforeKBps - networkDrop);
        primaryImprovement = min(10.0, networkDrop / 128.0);
        verification.successCriteria = "Background bandwidth drops without breaking foreground connectivity";
        verification.failureCriteria = "Network remains saturated or foreground work is interrupted";
        verification.evidence = "Network before estimate " + to_string(static_cast<int>(verification.networkBeforeKBps)) + " KB/s.";
    } else if (plan.actionType == "system_review") {
        primaryImprovement = 1.0;
        verification.successCriteria = "More samples produce a clear safe target";
        verification.failureCriteria = "Risk remains high with no safe action target";
        verification.evidence = "No safe target selected yet.";
    }

    verification.riskDeltaEstimate = EstimateRiskDelta(plan, decision, primaryImprovement);
    verification.riskAfterEstimate = ClampPercent(decision.riskScore - verification.riskDeltaEstimate);

    ClassifyOutcome(verification, plan);
    return verification;
}
