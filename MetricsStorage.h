#pragma once

#include <string>
#include <vector>
#include <mutex>

#include "AdaptiveBaseline.h"
#include "AutoHealPlanner.h"
#include "BackgroundAgent.h"
#include "BenchmarkProof.h"
#include "DecisionEngine.h"
#include "HealingVerifier.h"
#include "LowEndAutopilot.h"
#include "RuntimeHealth.h"
#include "SafetyPolicy.h"
#include "SystemMetrics.h"

struct sqlite3;

class MetricsStorage {
public:
    MetricsStorage() = default;
    ~MetricsStorage();

    bool Open(const std::string& path);
    void Close();
    bool IsReady() const;
    void LogSnapshot(const SystemSnapshot& snapshot);
    void LogBatch(const std::vector<SystemSnapshot>& snapshots);
    bool LogDecisionAudit(
        const SystemSnapshot& snapshot,
        const DecisionResult& decision,
        double aiProbability,
        double aiConfidence,
        const std::string& aiSource
    );
    bool LogHealPlan(
        const SystemSnapshot& snapshot,
        const DecisionResult& decision,
        const HealPlan& plan
    );
    bool LogHealVerification(
        const SystemSnapshot& snapshot,
        const DecisionResult& decision,
        const HealPlan& plan,
        const HealVerification& verification
    );
    bool LogSafetyPolicy(
        const SystemSnapshot& snapshot,
        const DecisionResult& decision,
        const HealPlan& plan,
        const HealVerification& verification,
        const SafetyPolicyResult& policyResult
    );
    bool LogRuntimeHealth(const RuntimeHealthSample& sample);
    bool LogRuntimePerformance(const RuntimeHealthSample& sample);
    bool LogAdaptiveBaseline(const AdaptiveBaselineResult& baseline);
    bool LogLowEndAutopilot(const LowEndAutopilotResult& autopilot);
    bool LogBackgroundAgent(const BackgroundAgentResult& agent);
    bool LogBenchmarkProof(const BenchmarkProofResult& proof);

private:
    bool EnsureSchema();
    bool EnsureColumn(const std::string& columnName, const std::string& columnDefinition);
    bool EnsureTableColumn(const std::string& tableName, const std::string& columnName, const std::string& columnDefinition);
    bool ExecuteInsertBatch(const std::vector<SystemSnapshot>& snapshots);

    sqlite3* db_ = nullptr;
    mutable std::mutex dbMutex_;
};
