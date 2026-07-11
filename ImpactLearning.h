#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ActionVerification.h"
#include "PerformanceIntelligence.h"
#include "ResourceOrchestrator.h"

struct sqlite3;

struct WorkloadContextFeatures {
    std::vector<double> values;
    std::string workload;
    DWORD targetPid = 0;
    double targetCriticality = 100.0;
    double targetSafety = 0.0;
};

struct ImpactCandidate {
    ResourceActionType action = ResourceActionType::None;
    DWORD targetPid = 0;
    std::string targetName;
    bool reversible = false;
    bool deterministicSafetyPassed = false;
};

struct ImpactPrediction {
    ResourceActionType action = ResourceActionType::None;
    double expectedReward = 0.0;
    double uncertainty = 1.0;
    double lowerConfidenceBound = -1.0;
    int observations = 0;
    bool sufficientlyKnown = false;
};

struct ShadowPolicyDecision {
    long long timestampMs = 0;
    std::string policyVersion = "impact-bandit-v1";
    std::string mode = "SHADOW_ONLY";
    ResourceActionType selectedAction = ResourceActionType::None;
    DWORD targetPid = 0;
    std::string targetName = "system";
    double expectedReward = 0.0;
    double uncertainty = 1.0;
    double lowerConfidenceBound = -1.0;
    double baselineReward = 0.0;
    bool wouldAct = false;
    std::string reason;
};

struct LoggedPolicyOutcome {
    ResourceActionType loggedAction = ResourceActionType::None;
    ResourceActionType candidatePolicyAction = ResourceActionType::None;
    double reward = 0.0;
    double baselineReward = 0.0;
    double loggingPropensity = 1.0;
};

struct OfflineEvaluationResult {
    int totalSamples = 0;
    int matchedSamples = 0;
    double estimatedPolicyReward = 0.0;
    double baselineReward = 0.0;
    double standardError = 0.0;
    double lowerConfidenceBenefit = 0.0;
    bool sufficientEvidence = false;
    bool promotionEligible = false;
    std::string reason;
};

struct PrivateFederatedUpdate {
    std::vector<double> protectedDelta;
    double originalNorm = 0.0;
    double clippedNorm = 0.0;
    double clipLimit = 0.0;
    double noiseStdDev = 0.0;
    std::string categoryToken;
    bool containsRawProcessIdentity = false;
};

class WorkloadContextEncoder {
public:
    WorkloadContextFeatures Encode(
        const SystemSnapshot& system,
        const QoeTelemetrySample& qoe,
        WorkloadPhase workload,
        DWORD targetPid,
        double targetCriticality,
        double targetSafety
    ) const;
    static constexpr int Dimension() { return 22; }
};

class ContextualImpactModel {
public:
    explicit ContextualImpactModel(double ridge = 1.0, double explorationScale = 1.25);
    ImpactPrediction Predict(ResourceActionType action, const WorkloadContextFeatures& context) const;
    bool Update(ResourceActionType action, const WorkloadContextFeatures& context, double measuredReward);

private:
    struct ActionModel {
        std::vector<std::vector<double>> covariance;
        std::vector<double> rewardVector;
        int observations = 0;
    };

    ActionModel& EnsureModel(ResourceActionType action);
    static bool Invert(const std::vector<std::vector<double>>& matrix, std::vector<std::vector<double>>& inverse);
    static double Dot(const std::vector<double>& left, const std::vector<double>& right);
    static std::vector<double> Multiply(const std::vector<std::vector<double>>& matrix, const std::vector<double>& input);

    double ridge_ = 1.0;
    double explorationScale_ = 1.25;
    mutable std::mutex mutex_;
    std::unordered_map<int, ActionModel> models_;
};

class ShadowContextualPolicy {
public:
    ShadowPolicyDecision Select(
        const WorkloadContextFeatures& context,
        const std::vector<ImpactCandidate>& candidates,
        const ContextualImpactModel& model,
        double requiredLowerBound = 0.05,
        int minimumObservations = 20
    ) const;
};

class OfflinePolicyEvaluator {
public:
    OfflineEvaluationResult Evaluate(
        const std::vector<LoggedPolicyOutcome>& outcomes,
        int minimumMatchedSamples = 30,
        double confidenceZ = 1.96
    ) const;
};

class PrivacyPreservingUpdateBuilder {
public:
    PrivateFederatedUpdate Build(
        const std::vector<double>& localDelta,
        double clipLimit,
        double noiseStdDev,
        const std::string& localCategory,
        const std::string& deviceSalt
    ) const;

private:
    static std::string Sha256Token(const std::string& value);
    static double SecureNormal();
};

class LearningJournal {
public:
    LearningJournal() = default;
    ~LearningJournal();

    bool Open(const std::string& path, std::string& error);
    void Close();
    bool RegisterPolicyVersion(const std::string& version, const std::string& featureContract, bool promoted, std::string& error);
    bool RecordOfflineEvaluation(const std::string& version, const OfflineEvaluationResult& evaluation, std::string& error);
    bool IsPolicyPromoted(const std::string& version, bool& promoted, std::string& error);
    bool SaveShadowDecision(const ShadowPolicyDecision& decision, const WorkloadContextFeatures& context, std::string& error);
    bool SaveReward(const std::string& transactionId, ResourceActionType action, const ActionImpactResult& outcome, std::string& error);
    bool SaveFederatedAudit(const PrivateFederatedUpdate& update, std::string& error);

private:
    bool EnsureSchema(std::string& error);

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};
