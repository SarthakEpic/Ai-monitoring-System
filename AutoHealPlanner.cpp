#include "AutoHealPlanner.h"

#include <algorithm>
#include <sstream>

using namespace std;

namespace {
double ClampPercent(double value) {
    return max(0.0, min(100.0, value));
}

string PercentText(double value) {
    return to_string(static_cast<int>(ClampPercent(value))) + "%";
}

string MbText(double value) {
    return to_string(static_cast<int>(max(0.0, value))) + " MB";
}

string BuildPlanId(const SystemSnapshot& snapshot, const DecisionResult& decision) {
    ostringstream oss;
    oss << "HP-" << snapshot.timestamp << "-";
    if (decision.actionTargetPid != 0) {
        oss << decision.actionTargetPid;
    } else {
        oss << decision.rootCause;
    }
    return oss.str();
}

double ComputeReadiness(const DecisionResult& decision, const DecisionPolicy& policy) {
    double readiness = decision.actionConfidence;
    readiness += decision.candidateSafetyScore * 0.20;
    readiness += decision.expectedGainMB > 0.0 ? min(12.0, decision.expectedGainMB / 64.0) : 0.0;

    if (decision.level == RiskLevel::Critical) readiness += 8.0;
    if (decision.level == RiskLevel::Warning) readiness += 3.0;
    if (decision.safetyGate == "READY") readiness += 12.0;
    if (decision.safetyGate == "DRY_RUN_ONLY") readiness -= 8.0;
    if (decision.safetyGate == "NO_SAFE_TARGET") readiness -= 18.0;
    if (decision.safetyGate == "USER_REVIEW_REQUIRED") readiness -= 10.0;
    if (decision.cooldownActive) readiness -= 25.0;
    if (!policy.autoHealEnabled || policy.safeMode || policy.dryRun) readiness -= 10.0;

    return ClampPercent(readiness);
}

void ApplyProcessPlan(HealPlan& plan, const DecisionResult& decision) {
    plan.targetKind = "process";
    plan.targetPid = decision.actionTargetPid;
    plan.targetName = decision.actionTarget;
    plan.expectedGainMB = decision.expectedGainMB;

    if (decision.recommendedAction == "dry_run_lower_priority") {
        plan.actionType = "process_priority";
        plan.actionName = "lower_background_priority";
        plan.executionStep = "Would lower target process priority after confirming it remains background-only.";
        plan.expectedImpact = "Reduce CPU contention without closing the application.";
        plan.postCheck = "Check CPU pressure, UI responsiveness, and target process stability for 60 seconds.";
        plan.rollbackPlan = "Restore original process priority if responsiveness does not improve.";
    } else if (decision.recommendedAction == "dry_run_memory_trim") {
        plan.actionType = "memory_pressure";
        plan.actionName = "trim_background_working_set";
        plan.executionStep = "Would request a memory trim for the background target instead of terminating it.";
        plan.expectedImpact = "Potentially recover about " + MbText(decision.expectedGainMB) + " without closing user work.";
        plan.postCheck = "Check memory percentage, target process health, and foreground app responsiveness.";
        plan.rollbackPlan = "No destructive rollback; observe target for reload or memory regrowth.";
    } else if (decision.recommendedAction == "dry_run_sleep_candidate") {
        plan.actionType = "background_sleep";
        plan.actionName = "sleep_or_suspend_candidate";
        plan.executionStep = "Would pause or sleep a safe background helper only after stronger execution gates pass.";
        plan.expectedImpact = "Reduce background CPU and memory pressure from a non-active process.";
        plan.postCheck = "Verify CPU, memory, and user foreground state improve without app breakage.";
        plan.rollbackPlan = "Resume the paused process or restart the helper if needed.";
    } else {
        plan.actionType = "process_review";
        plan.actionName = "manual_process_review";
        plan.executionStep = "Would show a manual review recommendation for the target process.";
        plan.expectedImpact = "No automatic change until a safer action is selected.";
        plan.postCheck = "Confirm whether the process is actually responsible for pressure.";
        plan.rollbackPlan = "No automatic change performed.";
    }

    plan.preCheck =
        "Confirm target is not foreground, not recently active, not denied by policy, and still causing pressure.";
    plan.rationale =
        "Stage 3 selected " + decision.actionTarget + " with safety " +
        PercentText(decision.candidateSafetyScore) + " and expected gain " + MbText(decision.expectedGainMB) + ".";
}

void ApplyResourcePlan(HealPlan& plan, const SystemSnapshot& snapshot, const DecisionResult& decision) {
    plan.targetKind = decision.rootCause;
    plan.targetName = decision.rootCause;
    plan.requiresUserApproval = true;

    if (decision.rootCause == "disk") {
        plan.actionType = "disk_cleanup";
        plan.actionName = "review_disk_cleanup";
        plan.preCheck = "Confirm disk free remains below the configured safety threshold.";
        plan.executionStep = "Would recommend Windows Storage cleanup, temp-file review, and large-download inspection.";
        plan.postCheck = "Verify disk free percentage increases above the warning threshold.";
        plan.rollbackPlan = "Do not delete user files automatically; restore from recycle bin/backups if user removed the wrong file.";
        plan.expectedImpact = "Recover disk space through user-approved cleanup.";
        plan.rationale = "Disk free is " + PercentText(snapshot.diskFree) + ", which is below the configured safety floor.";
    } else if (decision.rootCause == "network") {
        plan.actionType = "network_review";
        plan.actionName = "review_network_activity";
        plan.preCheck = "Confirm sustained network pressure and identify the top network-related process if available.";
        plan.executionStep = "Would recommend pausing background sync/download tasks, never active calls or foreground work.";
        plan.postCheck = "Verify network throughput normalizes without breaking foreground connectivity.";
        plan.rollbackPlan = "Resume paused sync/download work if the user needs it.";
        plan.expectedImpact = "Reduce background bandwidth contention.";
        plan.rationale = "Network anomaly contributed to the risk score and needs user review before action.";
    } else {
        plan.actionType = "system_review";
        plan.actionName = "increase_observation";
        plan.preCheck = "Collect more samples before selecting a target.";
        plan.executionStep = "Would keep monitoring and wait for a stable root cause.";
        plan.postCheck = "Check whether risk clears or a safer target appears.";
        plan.rollbackPlan = "No automatic change performed.";
        plan.expectedImpact = "Improves confidence before intervention.";
        plan.rationale = "Risk is elevated but no resource action is safe enough yet.";
    }
}
}

HealPlan AutoHealPlanner::BuildPlan(
    const SystemSnapshot& snapshot,
    const DecisionResult& decision,
    const DecisionPolicy& policy
) const {
    HealPlan plan;
    plan.planId = BuildPlanId(snapshot, decision);
    plan.gate = decision.safetyGate;
    plan.blockedReason = decision.blockedReason;
    plan.riskBefore = decision.riskScore;
    plan.confidence = decision.actionConfidence;
    plan.readinessScore = ComputeReadiness(decision, policy);
    plan.expectedGainMB = decision.expectedGainMB;
    plan.targetName = decision.actionTarget;
    plan.targetPid = decision.actionTargetPid;
    plan.executionMode = (!policy.autoHealEnabled || policy.safeMode || policy.dryRun) ? "SIMULATION_ONLY" : "EXECUTION_ALLOWED";
    plan.wouldExecute = decision.safeToHeal && plan.executionMode == "EXECUTION_ALLOWED";
    plan.blocked = !plan.wouldExecute;

    if (decision.level == RiskLevel::Normal || decision.recommendedAction == "monitor_only") {
        plan.status = "OBSERVE";
        plan.actionType = "none";
        plan.actionName = "monitor_only";
        plan.summary = "No healing plan needed";
        plan.safetyNotes = "Risk is below action threshold.";
        return plan;
    }

    if (decision.recommendedAction == "review_disk_cleanup" || decision.recommendedAction == "review_network_activity") {
        ApplyResourcePlan(plan, snapshot, decision);
    } else if (decision.actionTargetPid != 0) {
        ApplyProcessPlan(plan, decision);
    } else {
        ApplyResourcePlan(plan, snapshot, decision);
    }

    plan.requiresUserApproval = plan.requiresUserApproval || decision.safetyGate == "USER_REVIEW_REQUIRED";

    if (decision.cooldownActive) {
        plan.status = "COOLDOWN";
        plan.summary = "Plan paused by cooldown for " + to_string(decision.cooldownRemainingSeconds) + " seconds";
    } else if (plan.wouldExecute) {
        plan.status = "READY";
        plan.summary = "Execution gates passed for " + plan.actionName + " on " + plan.targetName;
    } else if (plan.requiresUserApproval) {
        plan.status = "REVIEW_REQUIRED";
        plan.summary = "User review required before " + plan.actionName;
    } else if (decision.safetyGate == "NO_SAFE_TARGET" || decision.safetyGate == "DENYLIST_BLOCKED" ||
               decision.safetyGate == "ALLOWLIST_REQUIRED" || decision.safetyGate == "MODEL_CONFIDENCE_BLOCKED" ||
               decision.safetyGate == "PROCESS_SAFETY_BLOCKED") {
        plan.status = "BLOCKED";
        plan.summary = "Healing blocked: " + decision.blockedReason;
    } else {
        plan.status = "DRY_RUN_READY";
        plan.summary = "Dry-run plan ready for " + plan.actionName + " on " + plan.targetName;
    }

    if (plan.executionMode == "SIMULATION_ONLY") {
        plan.safetyNotes =
            "Simulation only. AUTO_HEAL_ENABLED, AUTO_HEAL_DRY_RUN, or SAFE_MODE prevents real execution.";
    } else if (plan.wouldExecute) {
        plan.safetyNotes = "Execution would be allowed only after pre-checks and post-heal verification.";
    } else {
        plan.safetyNotes = "Execution is blocked by safety gate: " + decision.safetyGate + ".";
    }

    return plan;
}
