#include "ResourceOrchestrator.h"

#include <processthreadsapi.h>
#include "sqlite3.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>

using namespace std;

namespace {

constexpr ULONG kLowMemoryPriority = 2;

string Lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(tolower(c));
    });
    return value;
}

string NormalizePath(const string& value) {
    string result = value;
    replace(result.begin(), result.end(), '/', '\\');
    return Lower(result);
}

string Basename(const string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == string::npos ? path : path.substr(slash + 1);
}

bool ContainsName(const vector<string>& names, const string& processName) {
    const string needle = Lower(Basename(processName));
    return any_of(names.begin(), names.end(), [&](const string& item) {
        return Lower(Basename(item)) == needle;
    });
}

unsigned long long FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

string WindowsError(const string& operation) {
    return operation + " failed with Windows error " + to_string(GetLastError());
}

bool QueryIdentity(HANDLE process, DWORD pid, ProcessIdentity& identity, string& error) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        error = WindowsError("GetProcessTimes");
        return false;
    }

    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(pid, &sessionId)) {
        error = WindowsError("ProcessIdToSessionId");
        return false;
    }

    wchar_t pathBuffer[32768]{};
    DWORD pathLength = static_cast<DWORD>(size(pathBuffer));
    if (!QueryFullProcessImageNameW(process, 0, pathBuffer, &pathLength)) {
        error = WindowsError("QueryFullProcessImageNameW");
        return false;
    }

    filesystem::path path(wstring(pathBuffer, pathLength));
    identity.pid = pid;
    identity.creationTime = FileTimeValue(created);
    identity.sessionId = sessionId;
    identity.executablePath = path.string();
    identity.processName = path.filename().string();
    return true;
}

bool SameIdentity(const ProcessIdentity& expected, const ProcessIdentity& actual) {
    return expected.pid == actual.pid &&
           expected.creationTime == actual.creationTime &&
           NormalizePath(expected.executablePath) == NormalizePath(actual.executablePath);
}

HANDLE OpenActionProcess(DWORD pid) {
    return OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION | SYNCHRONIZE, FALSE, pid);
}

void BindText(sqlite3_stmt* statement, int index, const string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

string ColumnText(sqlite3_stmt* statement, int index) {
    const unsigned char* value = sqlite3_column_text(statement, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}

vector<string> DefaultProtectedNames() {
    return {
        "system", "registry", "smss.exe", "csrss.exe", "wininit.exe", "services.exe",
        "lsass.exe", "winlogon.exe", "svchost.exe", "dwm.exe", "explorer.exe",
        "fontdrvhost.exe", "audiodg.exe", "securityhealthservice.exe", "msmpeng.exe"
    };
}

bool IsProtectedCategory(const string& category, const string& safety) {
    const string normalizedCategory = Lower(category);
    const string normalizedSafety = Lower(safety);
    return normalizedCategory.find("system") != string::npos ||
           normalizedCategory.find("security") != string::npos ||
           normalizedCategory.find("accessibility") != string::npos ||
           normalizedSafety == "protected" || normalizedSafety == "critical" || normalizedSafety == "deny";
}

}  // namespace

const char* ToString(ResourceActionType action) {
    switch (action) {
    case ResourceActionType::LowerPriority: return "LOWER_PRIORITY";
    case ResourceActionType::EnableEcoQos: return "ENABLE_ECO_QOS";
    case ResourceActionType::LowerMemoryPriority: return "LOWER_MEMORY_PRIORITY";
    default: return "NONE";
    }
}

const char* ToString(ActionTransactionStatus status) {
    switch (status) {
    case ActionTransactionStatus::Blocked: return "BLOCKED";
    case ActionTransactionStatus::Planned: return "PLANNED";
    case ActionTransactionStatus::Executed: return "EXECUTED";
    case ActionTransactionStatus::VerifiedHelpful: return "VERIFIED_HELPFUL";
    case ActionTransactionStatus::VerifiedNeutral: return "VERIFIED_NEUTRAL";
    case ActionTransactionStatus::VerifiedHarmful: return "VERIFIED_HARMFUL";
    case ActionTransactionStatus::RolledBack: return "ROLLED_BACK";
    case ActionTransactionStatus::Failed: return "FAILED";
    case ActionTransactionStatus::RollbackFailed: return "ROLLBACK_FAILED";
    case ActionTransactionStatus::TargetExited: return "TARGET_EXITED";
    default: return "BLOCKED";
    }
}

optional<ResourceActionType> ResourceActionTypeFromString(const string& value) {
    const string normalized = Lower(value);
    if (normalized == "lower_priority") return ResourceActionType::LowerPriority;
    if (normalized == "enable_eco_qos") return ResourceActionType::EnableEcoQos;
    if (normalized == "lower_memory_priority") return ResourceActionType::LowerMemoryPriority;
    if (normalized == "none") return ResourceActionType::None;
    return nullopt;
}

optional<ActionTransactionStatus> ActionTransactionStatusFromString(const string& value) {
    const string normalized = Lower(value);
    if (normalized == "blocked") return ActionTransactionStatus::Blocked;
    if (normalized == "planned") return ActionTransactionStatus::Planned;
    if (normalized == "executed") return ActionTransactionStatus::Executed;
    if (normalized == "verified_helpful") return ActionTransactionStatus::VerifiedHelpful;
    if (normalized == "verified_neutral") return ActionTransactionStatus::VerifiedNeutral;
    if (normalized == "verified_harmful") return ActionTransactionStatus::VerifiedHarmful;
    if (normalized == "rolled_back") return ActionTransactionStatus::RolledBack;
    if (normalized == "failed") return ActionTransactionStatus::Failed;
    if (normalized == "rollback_failed") return ActionTransactionStatus::RollbackFailed;
    if (normalized == "target_exited") return ActionTransactionStatus::TargetExited;
    return nullopt;
}

bool WindowsProcessActionExecutor::IsSupported(const ActionContext& context, string& reason) const {
    switch (context.action) {
    case ResourceActionType::LowerPriority:
    case ResourceActionType::EnableEcoQos:
    case ResourceActionType::LowerMemoryPriority:
        reason = "supported native Windows reversible action";
        return true;
    default:
        reason = "unsupported or empty resource action";
        return false;
    }
}

bool WindowsProcessActionExecutor::Capture(
    const ActionContext& context,
    ProcessIdentity& identity,
    OriginalProcessState& original,
    string& error
) const {
    HANDLE process = OpenActionProcess(context.targetPid);
    if (!process) {
        error = WindowsError("OpenProcess");
        return false;
    }

    bool ok = QueryIdentity(process, context.targetPid, identity, error);
    if (ok) {
        original.priorityClass = GetPriorityClass(process);
        original.priorityCaptured = original.priorityClass != 0;
        if (!original.priorityCaptured) {
            error = WindowsError("GetPriorityClass");
            ok = false;
        }
    }

    MEMORY_PRIORITY_INFORMATION memoryInfo{};
    if (ok && GetProcessInformation(process, ProcessMemoryPriority, &memoryInfo, sizeof(memoryInfo))) {
        original.memoryPriority = memoryInfo.MemoryPriority;
        original.memoryPriorityCaptured = true;
    } else if (ok && context.action == ResourceActionType::LowerMemoryPriority) {
        error = WindowsError("GetProcessInformation(ProcessMemoryPriority)");
        ok = false;
    }

    PROCESS_POWER_THROTTLING_STATE powerInfo{};
    powerInfo.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (ok && GetProcessInformation(process, ProcessPowerThrottling, &powerInfo, sizeof(powerInfo))) {
        original.powerControlMask = powerInfo.ControlMask;
        original.powerStateMask = powerInfo.StateMask;
        original.powerThrottlingCaptured = true;
    } else if (ok && context.action == ResourceActionType::EnableEcoQos) {
        error = WindowsError("GetProcessInformation(ProcessPowerThrottling)");
        ok = false;
    }

    CloseHandle(process);
    return ok;
}

bool WindowsProcessActionExecutor::Apply(
    const ActionContext& context,
    const ProcessIdentity& identity,
    const OriginalProcessState&,
    string& error
) const {
    HANDLE process = OpenActionProcess(context.targetPid);
    if (!process) {
        error = WindowsError("OpenProcess");
        return false;
    }

    ProcessIdentity current{};
    bool ok = QueryIdentity(process, context.targetPid, current, error);
    if (ok && !SameIdentity(identity, current)) {
        error = "process identity changed before action; refusing PID reuse risk";
        ok = false;
    }

    if (ok && context.action == ResourceActionType::LowerPriority) {
        const DWORD currentPriority = GetPriorityClass(process);
        const DWORD targetPriority = currentPriority == IDLE_PRIORITY_CLASS ? IDLE_PRIORITY_CLASS : BELOW_NORMAL_PRIORITY_CLASS;
        if (currentPriority == 0 || !SetPriorityClass(process, targetPriority)) {
            error = WindowsError("SetPriorityClass");
            ok = false;
        }
    } else if (ok && context.action == ResourceActionType::EnableEcoQos) {
        PROCESS_POWER_THROTTLING_STATE powerInfo{};
        powerInfo.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        powerInfo.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        powerInfo.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        if (!SetProcessInformation(process, ProcessPowerThrottling, &powerInfo, sizeof(powerInfo))) {
            error = WindowsError("SetProcessInformation(ProcessPowerThrottling)");
            ok = false;
        }
    } else if (ok && context.action == ResourceActionType::LowerMemoryPriority) {
        MEMORY_PRIORITY_INFORMATION memoryInfo{};
        memoryInfo.MemoryPriority = kLowMemoryPriority;
        if (!SetProcessInformation(process, ProcessMemoryPriority, &memoryInfo, sizeof(memoryInfo))) {
            error = WindowsError("SetProcessInformation(ProcessMemoryPriority)");
            ok = false;
        }
    }

    CloseHandle(process);
    return ok;
}

bool WindowsProcessActionExecutor::Rollback(
    const ActionContext& context,
    const ProcessIdentity& identity,
    const OriginalProcessState& original,
    string& error
) const {
    HANDLE process = OpenActionProcess(context.targetPid);
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) {
            error = "target process already exited; no live state remains to restore";
        } else {
            error = WindowsError("OpenProcess during rollback");
        }
        return false;
    }

    ProcessIdentity current{};
    bool ok = QueryIdentity(process, context.targetPid, current, error);
    if (ok && !SameIdentity(identity, current)) {
        error = "process identity changed before rollback; refusing to modify reused PID";
        ok = false;
    }

    if (ok && context.action == ResourceActionType::LowerPriority && original.priorityCaptured) {
        if (!SetPriorityClass(process, original.priorityClass)) {
            error = WindowsError("restore priority class");
            ok = false;
        }
    } else if (ok && context.action == ResourceActionType::EnableEcoQos && original.powerThrottlingCaptured) {
        PROCESS_POWER_THROTTLING_STATE powerInfo{};
        powerInfo.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        powerInfo.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        powerInfo.StateMask = original.powerStateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        if (!SetProcessInformation(process, ProcessPowerThrottling, &powerInfo, sizeof(powerInfo))) {
            error = WindowsError("restore power throttling");
            ok = false;
        }
    } else if (ok && context.action == ResourceActionType::LowerMemoryPriority && original.memoryPriorityCaptured) {
        MEMORY_PRIORITY_INFORMATION memoryInfo{};
        memoryInfo.MemoryPriority = original.memoryPriority;
        if (!SetProcessInformation(process, ProcessMemoryPriority, &memoryInfo, sizeof(memoryInfo))) {
            error = WindowsError("restore memory priority");
            ok = false;
        }
    }

    CloseHandle(process);
    return ok;
}

bool ActionSafetyShield::Evaluate(
    const ActionContext& context,
    const OrchestratorConfig& config,
    ProcessIdentity& identity,
    string& reason
) const {
    if (!config.executionEnabled || config.globalDisable) {
        reason = "real execution is disabled by the global safety configuration";
        return false;
    }
    if (context.targetPid == 0 || context.targetPid == 4 || context.targetPid == GetCurrentProcessId()) {
        reason = "invalid, kernel, or optimizer process target";
        return false;
    }
    if (context.targetPid == context.foregroundPid || context.targetMatchesUserIntent) {
        reason = "foreground or user-intent process is protected";
        return false;
    }
    if (context.targetIsSystemCritical || IsProtectedCategory(context.targetCategory, context.targetSafety)) {
        reason = "target category or safety classification is protected";
        return false;
    }
    if (config.requireUserApproval && !context.userApproved) {
        reason = "explicit user approval is required";
        return false;
    }
    if (config.requireAllowlist && !ContainsName(context.allowlist, context.expectedProcessName)) {
        reason = "target is not present in the action allowlist";
        return false;
    }

    vector<string> protectedNames = DefaultProtectedNames();
    protectedNames.insert(protectedNames.end(), context.protectedNames.begin(), context.protectedNames.end());
    if (ContainsName(protectedNames, context.expectedProcessName)) {
        reason = "target name is protected by the deterministic denylist";
        return false;
    }

    HANDLE process = OpenActionProcess(context.targetPid);
    if (!process) {
        reason = WindowsError("OpenProcess during safety evaluation");
        return false;
    }
    string error;
    const bool identityRead = QueryIdentity(process, context.targetPid, identity, error);
    CloseHandle(process);
    if (!identityRead) {
        reason = error;
        return false;
    }
    if (!context.expectedProcessName.empty() && Lower(identity.processName) != Lower(Basename(context.expectedProcessName))) {
        reason = "target process name does not match the selected candidate";
        return false;
    }
    if (!context.expectedExecutablePath.empty() &&
        NormalizePath(identity.executablePath) != NormalizePath(context.expectedExecutablePath)) {
        reason = "target executable path does not match the selected candidate";
        return false;
    }

    DWORD currentSession = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &currentSession);
    if (identity.sessionId == 0 || identity.sessionId != currentSession) {
        reason = "cross-session and service targets are forbidden";
        return false;
    }

    reason = "all deterministic action safety gates passed";
    return true;
}

ActionJournal::~ActionJournal() {
    Close();
}

bool ActionJournal::Open(const string& path, string& error) {
    lock_guard lock(mutex_);
    if (db_) return true;
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "sqlite open failed";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    char* message = nullptr;
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &message);
    if (message) sqlite3_free(message);
    return EnsureSchema(error);
}

void ActionJournal::Close() {
    lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool ActionJournal::EnsureSchema(string& error) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS action_transactions ("
        "transaction_id TEXT PRIMARY KEY, target_pid INTEGER NOT NULL, target_name TEXT NOT NULL, "
        "target_path TEXT NOT NULL, creation_time INTEGER NOT NULL, session_id INTEGER NOT NULL, "
        "action_type TEXT NOT NULL, status TEXT NOT NULL, started_at_ms INTEGER NOT NULL, "
        "expires_at_ms INTEGER NOT NULL, updated_at_ms INTEGER NOT NULL, original_priority INTEGER NOT NULL, "
        "original_memory_priority INTEGER NOT NULL, original_power_control INTEGER NOT NULL, "
        "original_power_state INTEGER NOT NULL, priority_captured INTEGER NOT NULL, memory_captured INTEGER NOT NULL, "
        "power_captured INTEGER NOT NULL, reason TEXT NOT NULL, error TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_action_transactions_status ON action_transactions(status, updated_at_ms);";
    char* message = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        error = message ? message : sqlite3_errmsg(db_);
        if (message) sqlite3_free(message);
        return false;
    }
    return true;
}

bool ActionJournal::Save(const ActionTransaction& transaction, string& error) {
    lock_guard lock(mutex_);
    if (!db_) {
        error = "action journal is not open";
        return false;
    }
    const char* sql =
        "INSERT INTO action_transactions VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(transaction_id) DO UPDATE SET status=excluded.status, updated_at_ms=excluded.updated_at_ms, "
        "reason=excluded.reason, error=excluded.error;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    BindText(statement, 1, transaction.transactionId);
    sqlite3_bind_int64(statement, 2, transaction.identity.pid);
    BindText(statement, 3, transaction.identity.processName);
    BindText(statement, 4, transaction.identity.executablePath);
    sqlite3_bind_int64(statement, 5, static_cast<sqlite3_int64>(transaction.identity.creationTime));
    sqlite3_bind_int64(statement, 6, transaction.identity.sessionId);
    BindText(statement, 7, ToString(transaction.context.action));
    BindText(statement, 8, ToString(transaction.status));
    sqlite3_bind_int64(statement, 9, transaction.startedAtMs);
    sqlite3_bind_int64(statement, 10, transaction.expiresAtMs);
    sqlite3_bind_int64(statement, 11, transaction.updatedAtMs);
    sqlite3_bind_int64(statement, 12, transaction.originalState.priorityClass);
    sqlite3_bind_int64(statement, 13, transaction.originalState.memoryPriority);
    sqlite3_bind_int64(statement, 14, transaction.originalState.powerControlMask);
    sqlite3_bind_int64(statement, 15, transaction.originalState.powerStateMask);
    sqlite3_bind_int(statement, 16, transaction.originalState.priorityCaptured ? 1 : 0);
    sqlite3_bind_int(statement, 17, transaction.originalState.memoryPriorityCaptured ? 1 : 0);
    sqlite3_bind_int(statement, 18, transaction.originalState.powerThrottlingCaptured ? 1 : 0);
    BindText(statement, 19, transaction.reason);
    BindText(statement, 20, transaction.error);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) error = sqlite3_errmsg(db_);
    sqlite3_finalize(statement);
    return ok;
}

vector<ActionTransaction> ActionJournal::LoadUnfinished(string& error) {
    lock_guard lock(mutex_);
    vector<ActionTransaction> result;
    if (!db_) {
        error = "action journal is not open";
        return result;
    }
    const char* sql =
        "SELECT transaction_id,target_pid,target_name,target_path,creation_time,session_id,action_type,status,"
        "started_at_ms,expires_at_ms,updated_at_ms,original_priority,original_memory_priority,"
        "original_power_control,original_power_state,priority_captured,memory_captured,power_captured,reason,error "
        "FROM action_transactions WHERE status IN ('PLANNED','EXECUTED','VERIFIED_HELPFUL','VERIFIED_NEUTRAL','VERIFIED_HARMFUL');";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db_);
        return result;
    }
    while (sqlite3_step(statement) == SQLITE_ROW) {
        ActionTransaction tx;
        tx.transactionId = ColumnText(statement, 0);
        tx.identity.pid = static_cast<DWORD>(sqlite3_column_int64(statement, 1));
        tx.context.targetPid = tx.identity.pid;
        tx.identity.processName = ColumnText(statement, 2);
        tx.context.expectedProcessName = tx.identity.processName;
        tx.identity.executablePath = ColumnText(statement, 3);
        tx.context.expectedExecutablePath = tx.identity.executablePath;
        tx.identity.creationTime = static_cast<unsigned long long>(sqlite3_column_int64(statement, 4));
        tx.identity.sessionId = static_cast<DWORD>(sqlite3_column_int64(statement, 5));
        tx.context.action = ResourceActionTypeFromString(ColumnText(statement, 6)).value_or(ResourceActionType::None);
        tx.status = ActionTransactionStatusFromString(ColumnText(statement, 7)).value_or(ActionTransactionStatus::Failed);
        tx.startedAtMs = sqlite3_column_int64(statement, 8);
        tx.expiresAtMs = sqlite3_column_int64(statement, 9);
        tx.updatedAtMs = sqlite3_column_int64(statement, 10);
        tx.originalState.priorityClass = static_cast<DWORD>(sqlite3_column_int64(statement, 11));
        tx.originalState.memoryPriority = static_cast<ULONG>(sqlite3_column_int64(statement, 12));
        tx.originalState.powerControlMask = static_cast<ULONG>(sqlite3_column_int64(statement, 13));
        tx.originalState.powerStateMask = static_cast<ULONG>(sqlite3_column_int64(statement, 14));
        tx.originalState.priorityCaptured = sqlite3_column_int(statement, 15) != 0;
        tx.originalState.memoryPriorityCaptured = sqlite3_column_int(statement, 16) != 0;
        tx.originalState.powerThrottlingCaptured = sqlite3_column_int(statement, 17) != 0;
        tx.reason = ColumnText(statement, 18);
        tx.error = ColumnText(statement, 19);
        result.push_back(move(tx));
    }
    sqlite3_finalize(statement);
    return result;
}

ActionCoordinator::ActionCoordinator(shared_ptr<IActionExecutor> executor) : executor_(move(executor)) {}

void ActionCoordinator::Configure(const OrchestratorConfig& config) {
    lock_guard lock(mutex_);
    config_ = config;
    config_.maximumActionSeconds = clamp(config_.maximumActionSeconds, 1, 300);
}

bool ActionCoordinator::OpenJournal(const string& path, string& error) {
    return journal_.Open(path, error);
}

ActionTransaction ActionCoordinator::Begin(const ActionContext& requestedContext) {
    ActionTransaction tx;
    tx.transactionId = NewTransactionId();
    tx.context = requestedContext;
    tx.startedAtMs = NowMs();
    tx.updatedAtMs = tx.startedAtMs;
    tx.status = ActionTransactionStatus::Blocked;

    OrchestratorConfig config;
    {
        lock_guard lock(mutex_);
        config = config_;
    }
    const auto durationLimit = chrono::seconds(config.maximumActionSeconds);
    tx.context.maxDuration = max(chrono::milliseconds(1), min(tx.context.maxDuration, chrono::duration_cast<chrono::milliseconds>(durationLimit)));
    tx.expiresAtMs = tx.startedAtMs + tx.context.maxDuration.count();

    string supportReason;
    if (!executor_ || !executor_->IsSupported(tx.context, supportReason)) {
        tx.reason = supportReason.empty() ? "no action executor available" : supportReason;
        tx.status = ActionTransactionStatus::Blocked;
        string journalError;
        journal_.Save(tx, journalError);
        return tx;
    }

    ProcessIdentity safetyIdentity{};
    if (!safetyShield_.Evaluate(tx.context, config, safetyIdentity, tx.reason)) {
        string journalError;
        journal_.Save(tx, journalError);
        return tx;
    }

    if (!executor_->Capture(tx.context, tx.identity, tx.originalState, tx.error)) {
        tx.status = ActionTransactionStatus::Failed;
        tx.reason = "failed to capture original process state";
        string journalError;
        journal_.Save(tx, journalError);
        return tx;
    }
    if (!SameIdentity(safetyIdentity, tx.identity)) {
        tx.status = ActionTransactionStatus::Blocked;
        tx.reason = "process identity changed between safety evaluation and state capture";
        string journalError;
        journal_.Save(tx, journalError);
        return tx;
    }

    tx.status = ActionTransactionStatus::Planned;
    tx.reason = "original process state captured; action transaction planned";
    string journalError;
    if (!journal_.Save(tx, journalError)) {
        tx.status = ActionTransactionStatus::Failed;
        tx.error = "cannot persist pre-action transaction: " + journalError;
        return tx;
    }

    if (!executor_->Apply(tx.context, tx.identity, tx.originalState, tx.error)) {
        tx.status = ActionTransactionStatus::Failed;
        tx.reason = "native resource action failed before execution could be confirmed";
        tx.updatedAtMs = NowMs();
        journal_.Save(tx, journalError);
        return tx;
    }

    tx.status = ActionTransactionStatus::Executed;
    tx.reason = "reversible native resource action executed";
    tx.updatedAtMs = NowMs();
    if (!journal_.Save(tx, journalError)) {
        string rollbackError;
        executor_->Rollback(tx.context, tx.identity, tx.originalState, rollbackError);
        tx.status = rollbackError.empty() ? ActionTransactionStatus::RolledBack : ActionTransactionStatus::RollbackFailed;
        tx.error = "post-action journal write failed; rollback: " + rollbackError;
        return tx;
    }

    {
        lock_guard lock(mutex_);
        active_[tx.transactionId] = tx;
    }
    return tx;
}

bool ActionCoordinator::Rollback(const string& transactionId, const string& reason, string& error) {
    ActionTransaction tx;
    {
        lock_guard lock(mutex_);
        const auto found = active_.find(transactionId);
        if (found == active_.end()) {
            error = "active transaction was not found";
            return false;
        }
        tx = found->second;
    }

    string rollbackError;
    const bool restored = executor_->Rollback(tx.context, tx.identity, tx.originalState, rollbackError);
    tx.updatedAtMs = NowMs();
    tx.reason = reason;
    if (restored) {
        tx.status = ActionTransactionStatus::RolledBack;
    } else if (rollbackError.find("already exited") != string::npos) {
        tx.status = ActionTransactionStatus::TargetExited;
    } else {
        tx.status = ActionTransactionStatus::RollbackFailed;
        tx.error = rollbackError;
    }
    string journalError;
    const bool persisted = journal_.Save(tx, journalError);
    {
        lock_guard lock(mutex_);
        active_.erase(transactionId);
    }
    if (!restored && tx.status != ActionTransactionStatus::TargetExited) error = rollbackError;
    if (!persisted) {
        if (!error.empty()) error += "; ";
        error += "journal update failed: " + journalError;
    }
    return (restored || tx.status == ActionTransactionStatus::TargetExited) && persisted;
}

void ActionCoordinator::Tick() {
    const long long now = NowMs();
    vector<string> expired;
    {
        lock_guard lock(mutex_);
        for (const auto& [id, tx] : active_) {
            if (tx.expiresAtMs <= now) expired.push_back(id);
        }
    }
    for (const string& id : expired) {
        string error;
        Rollback(id, "maximum action duration reached", error);
    }
}

int ActionCoordinator::RecoverUnfinished(vector<string>& recoveryErrors) {
    string loadError;
    vector<ActionTransaction> unfinished = journal_.LoadUnfinished(loadError);
    if (!loadError.empty()) recoveryErrors.push_back(loadError);
    int recovered = 0;
    for (ActionTransaction& tx : unfinished) {
        string rollbackError;
        const bool restored = executor_->Rollback(tx.context, tx.identity, tx.originalState, rollbackError);
        tx.updatedAtMs = NowMs();
        tx.reason = "startup recovery of unfinished action transaction";
        if (restored) {
            tx.status = ActionTransactionStatus::RolledBack;
            ++recovered;
        } else if (rollbackError.find("already exited") != string::npos) {
            tx.status = ActionTransactionStatus::TargetExited;
            ++recovered;
        } else {
            tx.status = ActionTransactionStatus::RollbackFailed;
            tx.error = rollbackError;
            recoveryErrors.push_back(tx.transactionId + ": " + rollbackError);
        }
        string saveError;
        if (!journal_.Save(tx, saveError)) recoveryErrors.push_back(tx.transactionId + ": " + saveError);
    }
    return recovered;
}

vector<ActionTransaction> ActionCoordinator::ActiveTransactions() const {
    lock_guard lock(mutex_);
    vector<ActionTransaction> result;
    result.reserve(active_.size());
    for (const auto& [_, tx] : active_) result.push_back(tx);
    return result;
}

long long ActionCoordinator::NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

string ActionCoordinator::NewTransactionId() {
    static atomic<unsigned long long> sequence{0};
    ostringstream stream;
    stream << "ACT-" << NowMs() << '-' << GetCurrentProcessId() << '-' << ++sequence;
    return stream.str();
}
