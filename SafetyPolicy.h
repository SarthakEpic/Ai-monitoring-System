#pragma once

#include <string>

#include "AutoHealPlanner.h"
#include "DecisionEngine.h"
#include "HealingVerifier.h"
#include "SystemMetrics.h"

enum class PolicyLevel {
    Forbidden,
    ReviewRequired,
    SimulationOnly,
    ExecutionEligible,
};

struct SafetyPolicyResult {
    PolicyLevel level = PolicyLevel::SimulationOnly;
    std::string levelName = "SIMULATION_ONLY";
    std::string reasonCode = "NO_ACTION";
    std::string reason = "No executable action is required";
    std::string targetName = "system";
    unsigned long targetPid = 0;
    std::string targetCategory = "UNKNOWN";
    std::string targetSafety = "UNKNOWN";
    double policyScore = 0.0;
    double minimumRequiredScore = 90.0;
    bool hardBlock = false;
    bool requiresApproval = false;
    bool simulationOnly = true;
    bool executionEligible = false;
    bool targetDenied = false;
    bool targetAllowed = false;
    bool targetProtected = false;
    bool userIntentProtected = false;
    bool modelGatePassed = false;
    bool decisionGatePassed = false;
    bool planGatePassed = false;
    bool verificationGatePassed = false;
    bool confidenceGatePassed = false;
    bool safeModeBlock = true;
    bool dryRunBlock = true;
    bool autoHealDisabled = true;
};

class SafetyPolicyEngine {
public:
    SafetyPolicyResult Evaluate(
        const SystemSnapshot& snapshot,
        const DecisionResult& decision,
        const HealPlan& plan,
        const HealVerification& verification,
        const DecisionPolicy& policy
    ) const;

    static const char* ToString(PolicyLevel level);
};
