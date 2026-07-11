#pragma once

#include <windows.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

enum class ResourceActionType {
    None,
    LowerPriority,
    EnableEcoQos,
    LowerMemoryPriority,
};

enum class ActionTransactionStatus {
    Blocked,
    Planned,
    Executed,
    VerifiedHelpful,
    VerifiedNeutral,
    VerifiedHarmful,
    RolledBack,
    Failed,
    RollbackFailed,
    TargetExited,
};

struct ProcessIdentity {
    DWORD pid = 0;
    unsigned long long creationTime = 0;
    DWORD sessionId = 0;
    std::string processName;
    std::string executablePath;
};

struct OriginalProcessState {
    DWORD priorityClass = 0;
    ULONG memoryPriority = 0;
    ULONG powerControlMask = 0;
    ULONG powerStateMask = 0;
    bool priorityCaptured = false;
    bool memoryPriorityCaptured = false;
    bool powerThrottlingCaptured = false;
};

struct ActionContext {
    DWORD targetPid = 0;
    DWORD foregroundPid = 0;
    ResourceActionType action = ResourceActionType::None;
    std::string expectedProcessName;
    std::string expectedExecutablePath;
    std::string targetCategory = "UNKNOWN";
    std::string targetSafety = "UNKNOWN";
    std::vector<std::string> allowlist;
    std::vector<std::string> protectedNames;
    bool userApproved = false;
    bool targetIsSystemCritical = false;
    bool targetMatchesUserIntent = false;
    std::chrono::milliseconds maxDuration{20000};
};

struct ActionTransaction {
    std::string transactionId;
    ActionContext context;
    ProcessIdentity identity;
    OriginalProcessState originalState;
    ActionTransactionStatus status = ActionTransactionStatus::Blocked;
    long long startedAtMs = 0;
    long long expiresAtMs = 0;
    long long updatedAtMs = 0;
    std::string reason;
    std::string error;
};

struct OrchestratorConfig {
    bool executionEnabled = false;
    bool globalDisable = true;
    bool requireAllowlist = true;
    bool requireUserApproval = true;
    int maximumActionSeconds = 30;
};

class IActionExecutor {
public:
    virtual ~IActionExecutor() = default;
    virtual bool IsSupported(const ActionContext& context, std::string& reason) const = 0;
    virtual bool Capture(
        const ActionContext& context,
        ProcessIdentity& identity,
        OriginalProcessState& original,
        std::string& error
    ) const = 0;
    virtual bool Apply(
        const ActionContext& context,
        const ProcessIdentity& identity,
        const OriginalProcessState& original,
        std::string& error
    ) const = 0;
    virtual bool Rollback(
        const ActionContext& context,
        const ProcessIdentity& identity,
        const OriginalProcessState& original,
        std::string& error
    ) const = 0;
};

class WindowsProcessActionExecutor final : public IActionExecutor {
public:
    bool IsSupported(const ActionContext& context, std::string& reason) const override;
    bool Capture(
        const ActionContext& context,
        ProcessIdentity& identity,
        OriginalProcessState& original,
        std::string& error
    ) const override;
    bool Apply(
        const ActionContext& context,
        const ProcessIdentity& identity,
        const OriginalProcessState& original,
        std::string& error
    ) const override;
    bool Rollback(
        const ActionContext& context,
        const ProcessIdentity& identity,
        const OriginalProcessState& original,
        std::string& error
    ) const override;
};

class ActionSafetyShield {
public:
    bool Evaluate(
        const ActionContext& context,
        const OrchestratorConfig& config,
        ProcessIdentity& identity,
        std::string& reason
    ) const;
};

class ActionJournal {
public:
    ActionJournal() = default;
    ~ActionJournal();

    bool Open(const std::string& path, std::string& error);
    void Close();
    bool Save(const ActionTransaction& transaction, std::string& error);
    std::vector<ActionTransaction> LoadUnfinished(std::string& error);

private:
    bool EnsureSchema(std::string& error);

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

class ActionCoordinator {
public:
    explicit ActionCoordinator(std::shared_ptr<IActionExecutor> executor);

    void Configure(const OrchestratorConfig& config);
    bool OpenJournal(const std::string& path, std::string& error);
    ActionTransaction Begin(const ActionContext& context);
    bool Rollback(const std::string& transactionId, const std::string& reason, std::string& error);
    void Tick();
    int RecoverUnfinished(std::vector<std::string>& recoveryErrors);
    std::vector<ActionTransaction> ActiveTransactions() const;

private:
    static long long NowMs();
    static std::string NewTransactionId();

    std::shared_ptr<IActionExecutor> executor_;
    OrchestratorConfig config_;
    ActionSafetyShield safetyShield_;
    ActionJournal journal_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ActionTransaction> active_;
};

const char* ToString(ResourceActionType action);
const char* ToString(ActionTransactionStatus status);
std::optional<ResourceActionType> ResourceActionTypeFromString(const std::string& value);
std::optional<ActionTransactionStatus> ActionTransactionStatusFromString(const std::string& value);
