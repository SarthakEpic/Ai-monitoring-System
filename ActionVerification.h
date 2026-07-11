#pragma once

#include <windows.h>

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "ResourceOrchestrator.h"
#include "SystemMetrics.h"

struct sqlite3;

enum class ImpactStatus {
    Estimated,
    Planned,
    Executed,
    VerifiedHelpful,
    VerifiedNeutral,
    VerifiedHarmful,
    RolledBack,
};

struct ActionMetricSnapshot {
    long long timestampMs = 0;
    DWORD foregroundPid = 0;
    DWORD targetPid = 0;
    double systemCpuPercent = 0.0;
    double memoryUsagePercent = 0.0;
    double availableMemoryMB = 0.0;
    double diskFreePercent = 0.0;
    double networkKBps = 0.0;
    unsigned long long targetCpuTime100ns = 0;
    unsigned long long foregroundCpuTime100ns = 0;
    unsigned long long targetReadBytes = 0;
    unsigned long long targetWriteBytes = 0;
    unsigned long targetPageFaults = 0;
    double targetWorkingSetMB = 0.0;
    double targetPrivateMB = 0.0;
    bool foregroundAlive = false;
    bool foregroundHung = false;
    bool targetAlive = false;
    bool measured = false;
    std::string error;
};

struct OutcomePolicy {
    double helpfulCpuDropPercent = 5.0;
    double helpfulAvailableMemoryGainMB = 48.0;
    double harmfulCpuIncreasePercent = 12.0;
    double harmfulAvailableMemoryLossMB = 128.0;
    double harmfulHardFaultRate = 150.0;
    double minimumHelpfulScore = 0.20;
    double harmfulScore = -0.20;
};

struct ActionImpactResult {
    std::string transactionId;
    ImpactStatus status = ImpactStatus::Planned;
    long long measuredAtMs = 0;
    double observationSeconds = 0.0;
    double systemCpuDelta = 0.0;
    double availableMemoryDeltaMB = 0.0;
    double targetWorkingSetDeltaMB = 0.0;
    double targetCpuMilliseconds = 0.0;
    double foregroundCpuMilliseconds = 0.0;
    double targetReadKBps = 0.0;
    double targetWriteKBps = 0.0;
    double targetHardFaultsPerSecond = 0.0;
    double reward = 0.0;
    double confidence = 0.0;
    bool measured = false;
    bool rollbackRequired = false;
    bool rollbackSucceeded = false;
    std::string reason;
};

class ActionMetricCollector {
public:
    ActionMetricSnapshot Capture(const SystemSnapshot& system, DWORD foregroundPid, DWORD targetPid) const;
};

class ActionOutcomeVerifier {
public:
    ActionImpactResult Evaluate(
        const ActionTransaction& transaction,
        const ActionMetricSnapshot& before,
        const ActionMetricSnapshot& after,
        const OutcomePolicy& policy
    ) const;
};

class ActionVerificationJournal {
public:
    ActionVerificationJournal() = default;
    ~ActionVerificationJournal();

    bool Open(const std::string& path, std::string& error);
    void Close();
    bool SaveSnapshot(const std::string& transactionId, const std::string& phase, const ActionMetricSnapshot& snapshot, std::string& error);
    bool SaveOutcome(const ActionImpactResult& outcome, std::string& error);

private:
    bool EnsureSchema(std::string& error);

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

class MeasuredActionSession {
public:
    explicit MeasuredActionSession(ActionCoordinator& coordinator);

    bool OpenJournal(const std::string& path, std::string& error);
    ActionTransaction Begin(const ActionContext& context, const SystemSnapshot& system);
    ActionImpactResult Verify(const std::string& transactionId, const SystemSnapshot& system, const OutcomePolicy& policy);
    ActionImpactResult VerifyWithSnapshots(
        const std::string& transactionId,
        const ActionMetricSnapshot& before,
        const ActionMetricSnapshot& after,
        const OutcomePolicy& policy
    );

private:
    ActionCoordinator& coordinator_;
    ActionMetricCollector collector_;
    ActionOutcomeVerifier verifier_;
    ActionVerificationJournal journal_;
    std::mutex mutex_;
    std::unordered_map<std::string, ActionMetricSnapshot> beforeByTransaction_;
};

const char* ToString(ImpactStatus status);
