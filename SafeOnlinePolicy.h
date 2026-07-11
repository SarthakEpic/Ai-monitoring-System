#pragma once

#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ImpactLearning.h"
#include "SafetyPolicy.h"

struct OnlinePolicyConfig {
    bool onlineEnabled = false;
    bool policyPromoted = false;
    bool globalDisable = true;
    bool requireApproval = true;
    int maximumActionsPerHour = 3;
    int cooldownSeconds = 600;
    int maximumActionSeconds = 20;
    int observationSeconds = 8;
    double minimumLowerConfidenceBenefit = 0.05;
    double minimumTargetSafety = 90.0;
    double maximumTargetCriticality = 30.0;
    std::filesystem::path killSwitchFile;
};

struct OnlinePolicyInput {
    ShadowPolicyDecision shadow;
    SafetyPolicyResult deterministicPolicy;
    WorkloadContextFeatures context;
    DWORD foregroundPid = 0;
    std::string targetExecutablePath;
    std::string targetCategory = "UNKNOWN";
    std::string targetSafety = "UNKNOWN";
    std::vector<std::string> allowlist;
    std::vector<std::string> protectedNames;
    bool userApproved = false;
    bool targetSystemCritical = false;
    bool targetMatchesUserIntent = false;
};

struct OnlinePolicyDecision {
    long long timestampMs = 0;
    bool eligible = false;
    std::string status = "BLOCKED";
    std::string reason;
    ActionContext actionContext;
};

struct ActiveOnlineAction {
    std::string transactionId;
    ResourceActionType action = ResourceActionType::None;
    DWORD targetPid = 0;
    WorkloadContextFeatures context;
    long long verifyAfterMs = 0;
};

class SafeOnlinePolicyController {
public:
    SafeOnlinePolicyController(
        ActionCoordinator& coordinator,
        ContextualImpactModel& impactModel,
        LearningJournal& learningJournal
    );

    void Configure(const OnlinePolicyConfig& config);
    bool Initialize(const std::string& databasePath, std::vector<std::string>& recoveryErrors);
    OnlinePolicyDecision Evaluate(const OnlinePolicyInput& input) const;
    ActionTransaction Execute(const OnlinePolicyInput& input, const SystemSnapshot& system);
    std::vector<ActionImpactResult> Tick(const SystemSnapshot& system, const OutcomePolicy& outcomePolicy);
    int EmergencyStop(const std::string& reason, std::vector<std::string>& errors);
    std::vector<ActiveOnlineAction> ActiveActions() const;

private:
    static long long NowMs();
    static std::string CooldownKey(ResourceActionType action, DWORD pid);
    bool KillSwitchPresent() const;
    void PruneBudget(long long nowMs) const;

    ActionCoordinator& coordinator_;
    MeasuredActionSession measuredSession_;
    ContextualImpactModel& impactModel_;
    LearningJournal& learningJournal_;
    mutable std::mutex mutex_;
    OnlinePolicyConfig config_;
    mutable std::deque<long long> actionHistory_;
    mutable std::unordered_map<std::string, long long> cooldownUntil_;
    std::unordered_map<std::string, ActiveOnlineAction> active_;
};
