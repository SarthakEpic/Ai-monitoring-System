#pragma once

#include <windows.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "PerformanceIntelligence.h"

struct sqlite3;

struct BrowserTabState {
    long long tabId = 0;
    std::string url;
    bool active = false;
    bool pinned = false;
    bool audible = false;
    bool muted = false;
    bool discarded = false;
    bool autoDiscardable = true;
    bool sharingMedia = false;
};

struct CooperativeSafetyDecision {
    bool eligible = false;
    std::string reason;
};

struct WorkloadProtectionResult {
    WorkloadPhase workload = WorkloadPhase::Unknown;
    std::vector<DWORD> protectedPids;
    std::vector<std::string> protectedNames;
    std::vector<std::string> reasons;
};

struct BitsJobInfo {
    std::string jobId;
    std::string displayName;
    std::string state;
    bool currentUserOwned = false;
};

struct CooperativeActionTransaction {
    std::string transactionId;
    std::string action;
    std::string target;
    std::string status = "BLOCKED";
    long long startedAtMs = 0;
    long long updatedAtMs = 0;
    bool reversible = false;
    bool restored = false;
    std::string reason;
};

struct IntegrationCapabilities {
    bool browserExtensionInstalled = false;
    bool browserNativeHostInstalled = false;
    bool bitsAvailable = false;
    bool prefetchAvailable = false;
    std::string browserReason;
    std::string bitsReason;
    std::string prefetchReason;
};

class BrowserTabSafetyValidator {
public:
    CooperativeSafetyDecision CanDiscard(const BrowserTabState& tab, bool userApproved) const;
};

class WorkloadProtectionEngine {
public:
    WorkloadProtectionResult Build(
        const SystemSnapshot& snapshot,
        const PerformanceCriticalityGraph& graph,
        WorkloadPhase workload
    ) const;
};

class BitsTransferAdapter {
public:
    BitsTransferAdapter();
    ~BitsTransferAdapter();

    bool Initialize(std::string& error);
    std::vector<BitsJobInfo> ListCurrentUserJobs(std::string& error) const;
    CooperativeActionTransaction PauseApprovedJob(const std::string& jobId, bool userApproved);
    bool Resume(const std::string& transactionId, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PrefetchLease {
public:
    PrefetchLease() = default;
    ~PrefetchLease();
    PrefetchLease(const PrefetchLease&) = delete;
    PrefetchLease& operator=(const PrefetchLease&) = delete;
    PrefetchLease(PrefetchLease&& other) noexcept;
    PrefetchLease& operator=(PrefetchLease&& other) noexcept;

    bool Active() const;
    size_t Bytes() const;
    void Release();

private:
    friend class PredictivePrefetcher;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    void* view_ = nullptr;
    size_t bytes_ = 0;
};

class PredictivePrefetcher {
public:
    struct Result {
        CooperativeActionTransaction transaction;
        std::unique_ptr<PrefetchLease> lease;
    };

    Result PrefetchFile(
        const std::wstring& path,
        double predictionConfidence,
        double historicalBenefit,
        size_t maximumBytes,
        bool userApproved
    ) const;
};

class CooperativeIntegrationJournal {
public:
    CooperativeIntegrationJournal() = default;
    ~CooperativeIntegrationJournal();

    bool Open(const std::string& path, std::string& error);
    void Close();
    bool Save(const CooperativeActionTransaction& transaction, std::string& error);

private:
    bool EnsureSchema(std::string& error);
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

class IntegrationCapabilityDetector {
public:
    IntegrationCapabilities Detect(const std::wstring& executableDirectory) const;
};
