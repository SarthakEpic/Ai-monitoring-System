#include "SafetyPolicy.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

using namespace std;

namespace {
double ClampPercent(double value) {
    return max(0.0, min(100.0, value));
}

string Lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });
    return value;
}

string Trim(const string& value) {
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

vector<string> SplitCsv(const string& csv) {
    vector<string> values;
    string token;
    istringstream stream(csv);
    while (getline(stream, token, ',')) {
        token = Lower(Trim(token));
        if (!token.empty()) values.push_back(token);
    }
    return values;
}

bool ContainsName(const vector<string>& values, const string& name) {
    const string lowered = Lower(name);
    return find(values.begin(), values.end(), lowered) != values.end();
}

bool IsProtectedCategory(const string& category) {
    return category == "SYSTEM_CRITICAL" ||
           category == "SECURITY" ||
           category == "FOREGROUND_FULLSCREEN_APP" ||
           category == "FOREGROUND_APP" ||
           category == "ACTIVE_APP_FAMILY";
}

bool IsProtectedSafety(const string& safety) {
    return safety == "PROTECTED" || safety == "USER_ACTIVE";
}

void SetLevel(SafetyPolicyResult& result, PolicyLevel level, const string& code, const string& reason) {
    result.level = level;
    result.levelName = SafetyPolicyEngine::ToString(level);
    result.reasonCode = code;
    result.reason = reason;
    result.hardBlock = level == PolicyLevel::Forbidden;
    result.requiresApproval = level == PolicyLevel::ReviewRequired;
    result.simulationOnly = level == PolicyLevel::SimulationOnly;
    result.executionEligible = level == PolicyLevel::ExecutionEligible;
}

double ComputePolicyScore(
    const DecisionResult& decision,
    const HealPlan& plan,
    const HealVerification& verification,
    const SafetyPolicyResult& result
) {
    double score = 0.0;
    score += decision.candidateSafetyScore * 0.22;
    score += decision.actionConfidence * 0.20;
    score += plan.readinessScore * 0.22;
    score += verification.confidence * 0.16;
    score += verification.simulatedPass ? 15.0 : 0.0;
    score += decision.level == RiskLevel::Critical ? 5.0 : 0.0;

    if (result.targetProtected) score -= 45.0;
    if (result.userIntentProtected) score -= 35.0;
    if (result.targetDenied) score -= 60.0;
    if (!result.targetAllowed && decision.actionTargetPid != 0) score -= 18.0;
    if (result.autoHealDisabled) score -= 10.0;
    if (result.safeModeBlock) score -= 10.0;
    if (result.dryRunBlock) score -= 10.0;
    if (verification.simulatedFail) score -= 25.0;
    if (verification.blocked) score -= 20.0;

    return ClampPercent(score);
}
}

SafetyPolicyResult SafetyPolicyEngine::Evaluate(
    const SystemSnapshot& snapshot,
    const DecisionResult& decision,
    const HealPlan& plan,
    const HealVerification& verification,
    const DecisionPolicy& policy
) const {
    SafetyPolicyResult result;
    result.targetName = plan.targetName.empty() ? decision.actionTarget : plan.targetName;
    result.targetPid = plan.targetPid != 0 ? plan.targetPid : decision.actionTargetPid;
    result.targetCategory = decision.candidate.category;
    result.targetSafety = decision.candidate.safety;
    result.minimumRequiredScore = 90.0;
    result.autoHealDisabled = !policy.autoHealEnabled;
    result.safeModeBlock = policy.safeMode;
    result.dryRunBlock = policy.dryRun;
    result.modelGatePassed = decision.safetyGate != "MODEL_CONFIDENCE_BLOCKED" && decision.actionConfidence >= 85.0;
    result.decisionGatePassed = decision.safeToHeal && decision.level == RiskLevel::Critical;
    result.planGatePassed = plan.wouldExecute && plan.status == "READY";
    result.verificationGatePassed = verification.simulatedPass && !verification.blocked;
    result.confidenceGatePassed = decision.actionConfidence >= 85.0 && plan.readinessScore >= 75.0 && verification.confidence >= 70.0;

    const vector<string> allowlist = SplitCsv(policy.allowlistCsv);
    const vector<string> denylist = SplitCsv(policy.denylistCsv);
    result.targetDenied = ContainsName(denylist, result.targetName) || decision.candidate.deniedByPolicy;
    result.targetAllowed = !allowlist.empty() && ContainsName(allowlist, result.targetName);
    result.targetProtected =
        IsProtectedCategory(decision.candidate.category) ||
        IsProtectedSafety(decision.candidate.safety) ||
        decision.candidate.protectedByUserIntent;
    result.userIntentProtected =
        result.targetPid != 0 &&
        (result.targetPid == snapshot.intent.foregroundPid ||
         Lower(result.targetName) == Lower(snapshot.intent.foregroundProcess));

    result.policyScore = ComputePolicyScore(decision, plan, verification, result);

    if (decision.recommendedAction == "monitor_only" || plan.status == "OBSERVE") {
        SetLevel(result, PolicyLevel::SimulationOnly, "NO_ACTION", "No action is required while risk is below intervention threshold");
    } else if (result.targetDenied) {
        SetLevel(result, PolicyLevel::Forbidden, "DENYLIST_TARGET", "Target is blocked by denylist policy");
    } else if (result.targetProtected) {
        SetLevel(result, PolicyLevel::Forbidden, "PROTECTED_TARGET", "Target is protected by process category or safety classification");
    } else if (result.userIntentProtected) {
        SetLevel(result, PolicyLevel::Forbidden, "USER_ACTIVE_TARGET", "Target matches the active foreground user context");
    } else if (decision.safetyGate == "NO_SAFE_TARGET") {
        SetLevel(result, PolicyLevel::Forbidden, "NO_SAFE_TARGET", "No safe target is available for this risk");
    } else if (decision.safetyGate == "COOLDOWN" || plan.status == "COOLDOWN") {
        SetLevel(result, PolicyLevel::SimulationOnly, "COOLDOWN_ACTIVE", "Cooldown prevents repeating this recommendation");
    } else if (decision.rootCause == "disk" || decision.rootCause == "network" || plan.requiresUserApproval) {
        SetLevel(result, PolicyLevel::ReviewRequired, "USER_REVIEW_REQUIRED", "Resource-level actions require user approval");
    } else if (verification.simulatedFail) {
        SetLevel(result, PolicyLevel::Forbidden, "VERIFICATION_FAILED", "Simulation predicts the action is unlikely to help");
    } else if (!verification.simulatedPass) {
        SetLevel(result, PolicyLevel::SimulationOnly, "VERIFICATION_NOT_READY", "Simulation is not strong enough for execution eligibility");
    } else if (result.autoHealDisabled) {
        SetLevel(result, PolicyLevel::SimulationOnly, "AUTO_HEAL_DISABLED", "AUTO_HEAL_ENABLED is disabled in config");
    } else if (result.safeModeBlock) {
        SetLevel(result, PolicyLevel::SimulationOnly, "SAFE_MODE_ENABLED", "SAFE_MODE prevents real execution");
    } else if (result.dryRunBlock) {
        SetLevel(result, PolicyLevel::SimulationOnly, "DRY_RUN_ENABLED", "AUTO_HEAL_DRY_RUN keeps the plan in simulation mode");
    } else if (!result.targetAllowed) {
        SetLevel(result, PolicyLevel::ReviewRequired, "ALLOWLIST_REQUIRED", "Target must be explicitly allowlisted before execution");
    } else if (!result.modelGatePassed) {
        SetLevel(result, PolicyLevel::SimulationOnly, "MODEL_GATE_FAILED", "Model/action confidence gate is not high enough");
    } else if (!result.decisionGatePassed || !result.planGatePassed || !result.verificationGatePassed || !result.confidenceGatePassed) {
        SetLevel(result, PolicyLevel::SimulationOnly, "READINESS_GATE_FAILED", "Decision, plan, verification, or confidence gate is not ready");
    } else if (result.policyScore < result.minimumRequiredScore) {
        SetLevel(result, PolicyLevel::SimulationOnly, "POLICY_SCORE_LOW", "Policy score is below execution threshold");
    } else {
        SetLevel(result, PolicyLevel::ExecutionEligible, "EXECUTION_ELIGIBLE", "All policy gates passed");
    }

    return result;
}

const char* SafetyPolicyEngine::ToString(PolicyLevel level) {
    switch (level) {
    case PolicyLevel::Forbidden:
        return "FORBIDDEN";
    case PolicyLevel::ReviewRequired:
        return "REVIEW_REQUIRED";
    case PolicyLevel::SimulationOnly:
        return "SIMULATION_ONLY";
    case PolicyLevel::ExecutionEligible:
        return "EXECUTION_ELIGIBLE";
    default:
        return "UNKNOWN";
    }
}
