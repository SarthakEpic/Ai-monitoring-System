#include "SafeOnlinePolicy.h"

#include <algorithm>
#include <chrono>

using namespace std;

SafeOnlinePolicyController::SafeOnlinePolicyController(
    ActionCoordinator& coordinator,
    ContextualImpactModel& impactModel,
    LearningJournal& learningJournal
) : coordinator_(coordinator), measuredSession_(coordinator), impactModel_(impactModel), learningJournal_(learningJournal) {}

long long SafeOnlinePolicyController::NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

string SafeOnlinePolicyController::CooldownKey(ResourceActionType action, DWORD pid) {
    return string(ToString(action)) + ':' + to_string(pid);
}

void SafeOnlinePolicyController::Configure(const OnlinePolicyConfig& config) {
    lock_guard lock(mutex_);
    config_ = config;
    config_.maximumActionsPerHour = clamp(config_.maximumActionsPerHour, 1, 20);
    config_.cooldownSeconds = clamp(config_.cooldownSeconds, 10, 86400);
    config_.maximumActionSeconds = clamp(config_.maximumActionSeconds, 1, 300);
    config_.observationSeconds = clamp(config_.observationSeconds, 1, config_.maximumActionSeconds);
    OrchestratorConfig actuator;
    actuator.executionEnabled = config_.onlineEnabled;
    actuator.globalDisable = config_.globalDisable;
    actuator.requireAllowlist = true;
    actuator.requireUserApproval = config_.requireApproval;
    actuator.maximumActionSeconds = config_.maximumActionSeconds;
    coordinator_.Configure(actuator);
}

bool SafeOnlinePolicyController::Initialize(const string& databasePath, vector<string>& recoveryErrors) {
    string error;
    const bool actionJournalReady = coordinator_.OpenJournal(databasePath, error);
    if (!actionJournalReady) recoveryErrors.push_back(error);
    error.clear();
    const bool verificationReady = measuredSession_.OpenJournal(databasePath, error);
    if (!verificationReady) recoveryErrors.push_back(error);
    if (actionJournalReady) coordinator_.RecoverUnfinished(recoveryErrors);
    return actionJournalReady && verificationReady;
}

bool SafeOnlinePolicyController::KillSwitchPresent() const {
    lock_guard lock(mutex_);
    return !config_.killSwitchFile.empty() && filesystem::exists(config_.killSwitchFile);
}

void SafeOnlinePolicyController::PruneBudget(long long nowMs) const {
    while (!actionHistory_.empty() && nowMs - actionHistory_.front() >= 3600000) actionHistory_.pop_front();
}

OnlinePolicyDecision SafeOnlinePolicyController::Evaluate(const OnlinePolicyInput& input) const {
    OnlinePolicyDecision decision;
    decision.timestampMs = NowMs();
    OnlinePolicyConfig config;
    {
        lock_guard lock(mutex_);
        config = config_;
        PruneBudget(decision.timestampMs);
    }
    if (!config.onlineEnabled || config.globalDisable) {
        decision.reason = "online execution is disabled by configuration";
        return decision;
    }
    if (KillSwitchPresent()) {
        decision.reason = "emergency kill-switch file is present";
        return decision;
    }
    if (!config.policyPromoted) {
        decision.reason = "shadow policy has not passed offline promotion gates";
        return decision;
    }
    if (!input.shadow.wouldAct || input.shadow.selectedAction == ResourceActionType::None) {
        decision.reason = "shadow policy selected the no-intervention baseline";
        return decision;
    }
    if (input.shadow.lowerConfidenceBound <= config.minimumLowerConfidenceBenefit) {
        decision.reason = "action lower-confidence benefit does not exceed the online gate";
        return decision;
    }
    if (!input.deterministicPolicy.executionEligible || input.deterministicPolicy.hardBlock) {
        decision.reason = "deterministic safety policy did not grant execution eligibility";
        return decision;
    }
    if (input.context.targetSafety < config.minimumTargetSafety ||
        input.context.targetCriticality > config.maximumTargetCriticality) {
        decision.reason = "target safety or performance-criticality is outside the online policy boundary";
        return decision;
    }
    if (input.shadow.targetPid == 0 || input.shadow.targetPid == input.foregroundPid ||
        input.targetSystemCritical || input.targetMatchesUserIntent) {
        decision.reason = "foreground, system-critical, or intent-matching target is protected";
        return decision;
    }
    if (config.requireApproval && !input.userApproved) {
        decision.reason = "online action requires explicit approval";
        return decision;
    }
    {
        lock_guard lock(mutex_);
        PruneBudget(decision.timestampMs);
        if (static_cast<int>(actionHistory_.size()) >= config.maximumActionsPerHour) {
            decision.reason = "hourly action budget is exhausted";
            return decision;
        }
        const auto cooldown = cooldownUntil_.find(CooldownKey(input.shadow.selectedAction, input.shadow.targetPid));
        if (cooldown != cooldownUntil_.end() && cooldown->second > decision.timestampMs) {
            decision.reason = "per-target action cooldown is active";
            return decision;
        }
    }

    decision.actionContext.targetPid = input.shadow.targetPid;
    decision.actionContext.foregroundPid = input.foregroundPid;
    decision.actionContext.action = input.shadow.selectedAction;
    decision.actionContext.expectedProcessName = input.shadow.targetName;
    decision.actionContext.expectedExecutablePath = input.targetExecutablePath;
    decision.actionContext.targetCategory = input.targetCategory;
    decision.actionContext.targetSafety = input.targetSafety;
    decision.actionContext.allowlist = input.allowlist;
    decision.actionContext.protectedNames = input.protectedNames;
    decision.actionContext.userApproved = input.userApproved;
    decision.actionContext.targetIsSystemCritical = input.targetSystemCritical;
    decision.actionContext.targetMatchesUserIntent = input.targetMatchesUserIntent;
    decision.actionContext.maxDuration = chrono::seconds(config.maximumActionSeconds);
    decision.eligible = true;
    decision.status = "EXECUTION_ELIGIBLE";
    decision.reason = "online action passed learning, uncertainty, deterministic safety, budget, cooldown, and approval gates";
    return decision;
}

ActionTransaction SafeOnlinePolicyController::Execute(const OnlinePolicyInput& input, const SystemSnapshot& system) {
    const OnlinePolicyDecision policy = Evaluate(input);
    if (!policy.eligible) {
        ActionTransaction blocked;
        blocked.status = ActionTransactionStatus::Blocked;
        blocked.reason = policy.reason;
        return blocked;
    }
    ActionTransaction transaction = measuredSession_.Begin(policy.actionContext, system);
    if (transaction.status == ActionTransactionStatus::Executed) {
        lock_guard lock(mutex_);
        const long long now = NowMs();
        actionHistory_.push_back(now);
        cooldownUntil_[CooldownKey(input.shadow.selectedAction, input.shadow.targetPid)] = now + static_cast<long long>(config_.cooldownSeconds) * 1000;
        active_[transaction.transactionId] = {
            transaction.transactionId,
            input.shadow.selectedAction,
            input.shadow.targetPid,
            input.context,
            now + static_cast<long long>(config_.observationSeconds) * 1000
        };
    }
    return transaction;
}

vector<ActionImpactResult> SafeOnlinePolicyController::Tick(const SystemSnapshot& system, const OutcomePolicy& outcomePolicy) {
    coordinator_.Tick();
    const long long now = NowMs();
    vector<ActiveOnlineAction> due;
    {
        lock_guard lock(mutex_);
        for (const auto& [_, action] : active_) if (action.verifyAfterMs <= now) due.push_back(action);
    }
    vector<ActionImpactResult> outcomes;
    for (const ActiveOnlineAction& action : due) {
        ActionImpactResult outcome = measuredSession_.Verify(action.transactionId, system, outcomePolicy);
        if (outcome.measured) {
            impactModel_.Update(action.action, action.context, outcome.reward);
            string journalError;
            learningJournal_.SaveReward(action.transactionId, action.action, outcome, journalError);
        }
        outcomes.push_back(outcome);
        lock_guard lock(mutex_);
        active_.erase(action.transactionId);
    }
    return outcomes;
}

int SafeOnlinePolicyController::EmergencyStop(const string& reason, vector<string>& errors) {
    vector<ActiveOnlineAction> active = ActiveActions();
    int restored = 0;
    for (const ActiveOnlineAction& action : active) {
        string error;
        if (coordinator_.Rollback(action.transactionId, reason, error)) ++restored;
        else errors.push_back(action.transactionId + ": " + error);
        lock_guard lock(mutex_);
        active_.erase(action.transactionId);
    }
    return restored;
}

vector<ActiveOnlineAction> SafeOnlinePolicyController::ActiveActions() const {
    lock_guard lock(mutex_);
    vector<ActiveOnlineAction> result;
    result.reserve(active_.size());
    for (const auto& [_, action] : active_) result.push_back(action);
    return result;
}
