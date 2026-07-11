#include "ActionVerification.h"

#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "sqlite3.h"

using namespace std;

namespace {

long long NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

unsigned long long FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

bool IsProcessHung(DWORD pid) {
    struct SearchState {
        DWORD pid = 0;
        bool found = false;
        bool hung = false;
    } state{pid, false, false};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto* state = reinterpret_cast<SearchState*>(parameter);
        DWORD windowPid = 0;
        GetWindowThreadProcessId(window, &windowPid);
        if (windowPid == state->pid && IsWindowVisible(window)) {
            state->found = true;
            state->hung = IsHungAppWindow(window) != FALSE;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
    return state.found && state.hung;
}

bool ReadProcessMetrics(DWORD pid, ActionMetricSnapshot& snapshot, bool foreground) {
    if (pid == 0) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;

    FILETIME created{}, exited{}, kernel{}, user{};
    const bool timesOk = GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE;
    const unsigned long long cpuTime = timesOk ? FileTimeValue(kernel) + FileTimeValue(user) : 0;
    if (foreground) {
        snapshot.foregroundAlive = alive;
        snapshot.foregroundCpuTime100ns = cpuTime;
        snapshot.foregroundHung = alive && IsProcessHung(pid);
    } else {
        snapshot.targetAlive = alive;
        snapshot.targetCpuTime100ns = cpuTime;
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
            snapshot.targetPageFaults = memory.PageFaultCount;
            snapshot.targetWorkingSetMB = static_cast<double>(memory.WorkingSetSize) / (1024.0 * 1024.0);
            snapshot.targetPrivateMB = static_cast<double>(memory.PrivateUsage) / (1024.0 * 1024.0);
        }
        IO_COUNTERS io{};
        if (GetProcessIoCounters(process, &io)) {
            snapshot.targetReadBytes = io.ReadTransferCount;
            snapshot.targetWriteBytes = io.WriteTransferCount;
        }
    }
    CloseHandle(process);
    return alive && timesOk;
}

void BindText(sqlite3_stmt* statement, int index, const string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace

const char* ToString(ImpactStatus status) {
    switch (status) {
    case ImpactStatus::Estimated: return "ESTIMATED";
    case ImpactStatus::Planned: return "PLANNED";
    case ImpactStatus::Executed: return "EXECUTED";
    case ImpactStatus::VerifiedHelpful: return "VERIFIED_HELPFUL";
    case ImpactStatus::VerifiedNeutral: return "VERIFIED_NEUTRAL";
    case ImpactStatus::VerifiedHarmful: return "VERIFIED_HARMFUL";
    case ImpactStatus::RolledBack: return "ROLLED_BACK";
    default: return "PLANNED";
    }
}

ActionMetricSnapshot ActionMetricCollector::Capture(const SystemSnapshot& system, DWORD foregroundPid, DWORD targetPid) const {
    ActionMetricSnapshot snapshot;
    snapshot.timestampMs = NowMs();
    snapshot.foregroundPid = foregroundPid;
    snapshot.targetPid = targetPid;
    snapshot.systemCpuPercent = system.cpuUsage;
    snapshot.memoryUsagePercent = system.memoryUsage;
    snapshot.availableMemoryMB = system.availableMemoryMB;
    snapshot.diskFreePercent = system.diskFree;
    snapshot.networkKBps = system.netDownKBps + system.netUpKBps;
    const bool targetOk = ReadProcessMetrics(targetPid, snapshot, false);
    const bool foregroundOk = foregroundPid == 0 || foregroundPid == targetPid
        ? targetOk
        : ReadProcessMetrics(foregroundPid, snapshot, true);
    if (foregroundPid == targetPid) {
        snapshot.foregroundAlive = snapshot.targetAlive;
        snapshot.foregroundCpuTime100ns = snapshot.targetCpuTime100ns;
        snapshot.foregroundHung = IsProcessHung(foregroundPid);
    } else if (foregroundPid == 0) {
        snapshot.foregroundAlive = true;
    }
    snapshot.measured = targetOk && foregroundOk;
    if (!snapshot.measured) snapshot.error = "one or more process-level measurements were unavailable";
    return snapshot;
}

ActionImpactResult ActionOutcomeVerifier::Evaluate(
    const ActionTransaction& transaction,
    const ActionMetricSnapshot& before,
    const ActionMetricSnapshot& after,
    const OutcomePolicy& policy
) const {
    ActionImpactResult result;
    result.transactionId = transaction.transactionId;
    result.measuredAtMs = after.timestampMs;
    result.observationSeconds = max(0.001, static_cast<double>(after.timestampMs - before.timestampMs) / 1000.0);
    if (transaction.status != ActionTransactionStatus::Executed || !before.measured || !after.measured) {
        result.status = ImpactStatus::Planned;
        result.reason = "measured verification requires an executed transaction and two valid snapshots";
        return result;
    }

    result.measured = true;
    result.systemCpuDelta = before.systemCpuPercent - after.systemCpuPercent;
    result.availableMemoryDeltaMB = after.availableMemoryMB - before.availableMemoryMB;
    result.targetWorkingSetDeltaMB = before.targetWorkingSetMB - after.targetWorkingSetMB;
    result.targetCpuMilliseconds = max(0.0, static_cast<double>(after.targetCpuTime100ns - before.targetCpuTime100ns) / 10000.0);
    result.foregroundCpuMilliseconds = max(0.0, static_cast<double>(after.foregroundCpuTime100ns - before.foregroundCpuTime100ns) / 10000.0);
    result.targetReadKBps = max(0.0, static_cast<double>(after.targetReadBytes - before.targetReadBytes) / 1024.0 / result.observationSeconds);
    result.targetWriteKBps = max(0.0, static_cast<double>(after.targetWriteBytes - before.targetWriteBytes) / 1024.0 / result.observationSeconds);
    result.targetHardFaultsPerSecond = max(0.0, static_cast<double>(after.targetPageFaults - before.targetPageFaults) / result.observationSeconds);

    const double cpuBenefit = clamp(result.systemCpuDelta / max(1.0, policy.helpfulCpuDropPercent), -2.0, 2.0);
    const double memoryBenefit = clamp(result.availableMemoryDeltaMB / max(1.0, policy.helpfulAvailableMemoryGainMB), -2.0, 2.0);
    const double faultPenalty = clamp(result.targetHardFaultsPerSecond / max(1.0, policy.harmfulHardFaultRate), 0.0, 2.0);
    result.reward = 0.45 * cpuBenefit + 0.45 * memoryBenefit - 0.10 * faultPenalty;
    result.confidence = clamp(35.0 + min(45.0, result.observationSeconds * 3.0) + (before.measured && after.measured ? 20.0 : 0.0), 0.0, 100.0);

    const bool foregroundRegression = before.foregroundAlive && (!after.foregroundAlive || after.foregroundHung);
    const bool resourceRegression = result.systemCpuDelta <= -policy.harmfulCpuIncreasePercent ||
                                    result.availableMemoryDeltaMB <= -policy.harmfulAvailableMemoryLossMB;
    const bool faultRegression = result.targetHardFaultsPerSecond >= policy.harmfulHardFaultRate;
    if (foregroundRegression || resourceRegression || faultRegression || result.reward <= policy.harmfulScore) {
        result.status = ImpactStatus::VerifiedHarmful;
        result.rollbackRequired = true;
        result.reason = foregroundRegression ? "foreground process regressed or stopped responding" :
                        faultRegression ? "page-fault rate exceeded the safety guardrail" :
                        "measured resource outcome crossed the harmful threshold";
    } else if (result.reward >= policy.minimumHelpfulScore &&
               (result.systemCpuDelta >= policy.helpfulCpuDropPercent ||
                result.availableMemoryDeltaMB >= policy.helpfulAvailableMemoryGainMB)) {
        result.status = ImpactStatus::VerifiedHelpful;
        result.reason = "measured resource improvement exceeded the helpful threshold";
    } else {
        result.status = ImpactStatus::VerifiedNeutral;
        result.reason = "measured effect was safe but too small to claim improvement";
    }
    return result;
}

ActionVerificationJournal::~ActionVerificationJournal() { Close(); }

bool ActionVerificationJournal::Open(const string& path, string& error) {
    lock_guard lock(mutex_);
    if (db_) return true;
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "sqlite open failed";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    return EnsureSchema(error);
}

void ActionVerificationJournal::Close() {
    lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool ActionVerificationJournal::EnsureSchema(string& error) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS action_metric_snapshots ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, transaction_id TEXT NOT NULL, phase TEXT NOT NULL, timestamp_ms INTEGER NOT NULL, "
        "foreground_pid INTEGER NOT NULL, target_pid INTEGER NOT NULL, system_cpu REAL NOT NULL, memory_usage REAL NOT NULL, "
        "available_memory_mb REAL NOT NULL, disk_free REAL NOT NULL, network_kbps REAL NOT NULL, target_cpu_100ns INTEGER NOT NULL, "
        "foreground_cpu_100ns INTEGER NOT NULL, target_read_bytes INTEGER NOT NULL, target_write_bytes INTEGER NOT NULL, "
        "target_page_faults INTEGER NOT NULL, target_working_set_mb REAL NOT NULL, target_private_mb REAL NOT NULL, "
        "foreground_alive INTEGER NOT NULL, foreground_hung INTEGER NOT NULL, target_alive INTEGER NOT NULL, measured INTEGER NOT NULL, error TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS action_outcomes ("
        "transaction_id TEXT PRIMARY KEY, status TEXT NOT NULL, measured_at_ms INTEGER NOT NULL, observation_seconds REAL NOT NULL, "
        "system_cpu_delta REAL NOT NULL, available_memory_delta_mb REAL NOT NULL, target_working_set_delta_mb REAL NOT NULL, "
        "target_cpu_ms REAL NOT NULL, foreground_cpu_ms REAL NOT NULL, target_read_kbps REAL NOT NULL, target_write_kbps REAL NOT NULL, "
        "target_faults_per_sec REAL NOT NULL, reward REAL NOT NULL, confidence REAL NOT NULL, measured INTEGER NOT NULL, "
        "rollback_required INTEGER NOT NULL, rollback_succeeded INTEGER NOT NULL, reason TEXT NOT NULL);";
    char* message = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        error = message ? message : sqlite3_errmsg(db_);
        if (message) sqlite3_free(message);
        return false;
    }
    return true;
}

bool ActionVerificationJournal::SaveSnapshot(
    const string& transactionId,
    const string& phase,
    const ActionMetricSnapshot& value,
    string& error
) {
    lock_guard lock(mutex_);
    const char* sql = "INSERT INTO action_metric_snapshots VALUES(NULL,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "verification journal is not open";
        return false;
    }
    BindText(statement, 1, transactionId); BindText(statement, 2, phase);
    sqlite3_bind_int64(statement, 3, value.timestampMs); sqlite3_bind_int64(statement, 4, value.foregroundPid);
    sqlite3_bind_int64(statement, 5, value.targetPid); sqlite3_bind_double(statement, 6, value.systemCpuPercent);
    sqlite3_bind_double(statement, 7, value.memoryUsagePercent); sqlite3_bind_double(statement, 8, value.availableMemoryMB);
    sqlite3_bind_double(statement, 9, value.diskFreePercent); sqlite3_bind_double(statement, 10, value.networkKBps);
    sqlite3_bind_int64(statement, 11, static_cast<sqlite3_int64>(value.targetCpuTime100ns));
    sqlite3_bind_int64(statement, 12, static_cast<sqlite3_int64>(value.foregroundCpuTime100ns));
    sqlite3_bind_int64(statement, 13, static_cast<sqlite3_int64>(value.targetReadBytes));
    sqlite3_bind_int64(statement, 14, static_cast<sqlite3_int64>(value.targetWriteBytes));
    sqlite3_bind_int64(statement, 15, value.targetPageFaults); sqlite3_bind_double(statement, 16, value.targetWorkingSetMB);
    sqlite3_bind_double(statement, 17, value.targetPrivateMB); sqlite3_bind_int(statement, 18, value.foregroundAlive ? 1 : 0);
    sqlite3_bind_int(statement, 19, value.foregroundHung ? 1 : 0); sqlite3_bind_int(statement, 20, value.targetAlive ? 1 : 0);
    sqlite3_bind_int(statement, 21, value.measured ? 1 : 0); BindText(statement, 22, value.error);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) error = sqlite3_errmsg(db_);
    sqlite3_finalize(statement);
    return ok;
}

bool ActionVerificationJournal::SaveOutcome(const ActionImpactResult& value, string& error) {
    lock_guard lock(mutex_);
    const char* sql =
        "INSERT OR REPLACE INTO action_outcomes VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "verification journal is not open";
        return false;
    }
    BindText(statement, 1, value.transactionId); BindText(statement, 2, ToString(value.status));
    sqlite3_bind_int64(statement, 3, value.measuredAtMs); sqlite3_bind_double(statement, 4, value.observationSeconds);
    sqlite3_bind_double(statement, 5, value.systemCpuDelta); sqlite3_bind_double(statement, 6, value.availableMemoryDeltaMB);
    sqlite3_bind_double(statement, 7, value.targetWorkingSetDeltaMB); sqlite3_bind_double(statement, 8, value.targetCpuMilliseconds);
    sqlite3_bind_double(statement, 9, value.foregroundCpuMilliseconds); sqlite3_bind_double(statement, 10, value.targetReadKBps);
    sqlite3_bind_double(statement, 11, value.targetWriteKBps); sqlite3_bind_double(statement, 12, value.targetHardFaultsPerSecond);
    sqlite3_bind_double(statement, 13, value.reward); sqlite3_bind_double(statement, 14, value.confidence);
    sqlite3_bind_int(statement, 15, value.measured ? 1 : 0); sqlite3_bind_int(statement, 16, value.rollbackRequired ? 1 : 0);
    sqlite3_bind_int(statement, 17, value.rollbackSucceeded ? 1 : 0); BindText(statement, 18, value.reason);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) error = sqlite3_errmsg(db_);
    sqlite3_finalize(statement);
    return ok;
}

MeasuredActionSession::MeasuredActionSession(ActionCoordinator& coordinator) : coordinator_(coordinator) {}

bool MeasuredActionSession::OpenJournal(const string& path, string& error) { return journal_.Open(path, error); }

ActionTransaction MeasuredActionSession::Begin(const ActionContext& context, const SystemSnapshot& system) {
    ActionMetricSnapshot before = collector_.Capture(system, context.foregroundPid, context.targetPid);
    ActionTransaction transaction = coordinator_.Begin(context);
    if (transaction.status == ActionTransactionStatus::Executed) {
        lock_guard lock(mutex_);
        beforeByTransaction_[transaction.transactionId] = before;
        string error;
        journal_.SaveSnapshot(transaction.transactionId, "PRE_ACTION", before, error);
    }
    return transaction;
}

ActionImpactResult MeasuredActionSession::Verify(
    const string& transactionId,
    const SystemSnapshot& system,
    const OutcomePolicy& policy
) {
    ActionMetricSnapshot before;
    ActionTransaction transaction;
    {
        lock_guard lock(mutex_);
        const auto beforeFound = beforeByTransaction_.find(transactionId);
        if (beforeFound == beforeByTransaction_.end()) {
            ActionImpactResult missing;
            missing.transactionId = transactionId;
            missing.reason = "pre-action measurement was not found";
            return missing;
        }
        before = beforeFound->second;
    }
    const auto active = coordinator_.ActiveTransactions();
    const auto found = find_if(active.begin(), active.end(), [&](const ActionTransaction& item) {
        return item.transactionId == transactionId;
    });
    if (found == active.end()) {
        ActionImpactResult missing;
        missing.transactionId = transactionId;
        missing.reason = "active action transaction was not found";
        return missing;
    }
    transaction = *found;
    ActionMetricSnapshot after = collector_.Capture(system, transaction.context.foregroundPid, transaction.context.targetPid);
    return VerifyWithSnapshots(transactionId, before, after, policy);
}

ActionImpactResult MeasuredActionSession::VerifyWithSnapshots(
    const string& transactionId,
    const ActionMetricSnapshot& before,
    const ActionMetricSnapshot& after,
    const OutcomePolicy& policy
) {
    const auto active = coordinator_.ActiveTransactions();
    const auto found = find_if(active.begin(), active.end(), [&](const ActionTransaction& item) {
        return item.transactionId == transactionId;
    });
    if (found == active.end()) {
        ActionImpactResult missing;
        missing.transactionId = transactionId;
        missing.reason = "active action transaction was not found";
        return missing;
    }
    ActionImpactResult result = verifier_.Evaluate(*found, before, after, policy);
    string error;
    journal_.SaveSnapshot(transactionId, "POST_ACTION", after, error);
    if (result.rollbackRequired) {
        string rollbackError;
        result.rollbackSucceeded = coordinator_.Rollback(transactionId, result.reason, rollbackError);
        if (!result.rollbackSucceeded) result.reason += "; rollback failed: " + rollbackError;
    }
    journal_.SaveOutcome(result, error);
    if (result.measured) {
        lock_guard lock(mutex_);
        beforeByTransaction_.erase(transactionId);
    }
    return result;
}
