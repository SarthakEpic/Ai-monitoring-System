#include "MetricsStorage.h"

#include <ctime>
#include <sstream>
#include <string>
#include <vector>

#include "sqlite3.h"

using namespace std;

namespace {
constexpr long long PROCESS_SAMPLE_RETENTION_SECONDS = 7LL * 24LL * 60LL * 60LL;

string JoinStrings(const vector<string>& values) {
    ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) oss << "|";
        oss << values[i];
    }
    return oss.str();
}
}

MetricsStorage::~MetricsStorage() {
    Close();
}

bool MetricsStorage::Open(const string& path) {
    Close();

    lock_guard<mutex> lock(dbMutex_);
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        db_ = nullptr;
        return false;
    }

    return EnsureSchema();
}

void MetricsStorage::Close() {
    lock_guard<mutex> lock(dbMutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool MetricsStorage::IsReady() const {
    lock_guard<mutex> lock(dbMutex_);
    return db_ != nullptr;
}

bool MetricsStorage::EnsureSchema() {
    const char* createSql =
        "CREATE TABLE IF NOT EXISTS metrics ("
        "time INTEGER, "
        "cpu REAL, "
        "mem REAL, "
        "disk REAL, "
        "net_down_kbps REAL DEFAULT 0, "
        "net_up_kbps REAL DEFAULT 0, "
        "process_count INTEGER DEFAULT 0, "
        "scenario_label TEXT DEFAULT 'auto', "
        "top_process TEXT DEFAULT '', "
        "top_process_cpu REAL DEFAULT 0, "
        "top_process_mem REAL DEFAULT 0);";

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, createSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* processSamplesSql =
        "CREATE TABLE IF NOT EXISTS process_samples ("
        "time INTEGER NOT NULL, "
        "pid INTEGER NOT NULL, "
        "parent_pid INTEGER DEFAULT 0, "
        "name TEXT DEFAULT '', "
        "exe_path TEXT DEFAULT '', "
        "category TEXT DEFAULT 'UNKNOWN', "
        "safety TEXT DEFAULT 'UNKNOWN', "
        "intent_role TEXT DEFAULT 'none', "
        "recommendation TEXT DEFAULT 'observe', "
        "reason TEXT DEFAULT '', "
        "cpu REAL DEFAULT 0, "
        "working_set_mb REAL DEFAULT 0, "
        "private_mb REAL DEFAULT 0, "
        "io_read_kbps REAL DEFAULT 0, "
        "io_write_kbps REAL DEFAULT 0, "
        "lifetime_sec REAL DEFAULT 0, "
        "thread_count INTEGER DEFAULT 0, "
        "handle_count INTEGER DEFAULT 0, "
        "priority_class INTEGER DEFAULT 0, "
        "session_id INTEGER DEFAULT 0, "
        "is_foreground INTEGER DEFAULT 0, "
        "is_recently_active INTEGER DEFAULT 0, "
        "matches_user_intent INTEGER DEFAULT 0, "
        "has_visible_window INTEGER DEFAULT 0, "
        "trusted_path INTEGER DEFAULT 0, "
        "signed_trusted INTEGER DEFAULT 0, "
        "signature_status TEXT DEFAULT 'not_checked', "
        "waste_score REAL DEFAULT 0, "
        "importance_score REAL DEFAULT 0, "
        "safety_score REAL DEFAULT 0, "
        "expected_gain_mb REAL DEFAULT 0, "
        "PRIMARY KEY(time, pid));";

    if (sqlite3_exec(db_, processSamplesSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* intentSamplesSql =
        "CREATE TABLE IF NOT EXISTS user_intent_samples ("
        "time INTEGER PRIMARY KEY, "
        "foreground_pid INTEGER DEFAULT 0, "
        "foreground_process TEXT DEFAULT '', "
        "foreground_path TEXT DEFAULT '', "
        "foreground_title TEXT DEFAULT '', "
        "app_kind TEXT DEFAULT 'UNKNOWN', "
        "user_state TEXT DEFAULT 'UNKNOWN', "
        "reason TEXT DEFAULT '', "
        "idle_seconds REAL DEFAULT 0, "
        "focus_duration_seconds REAL DEFAULT 0, "
        "is_fullscreen INTEGER DEFAULT 0, "
        "protect_foreground_family INTEGER DEFAULT 0, "
        "recent_processes TEXT DEFAULT '');";

    if (sqlite3_exec(db_, intentSamplesSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* decisionAuditSql =
        "CREATE TABLE IF NOT EXISTS decision_audits ("
        "time INTEGER PRIMARY KEY, "
        "level TEXT DEFAULT 'NORMAL', "
        "risk_score REAL DEFAULT 0, "
        "anomaly_score REAL DEFAULT 0, "
        "pressure_score REAL DEFAULT 0, "
        "ai_probability REAL DEFAULT 0, "
        "ai_confidence REAL DEFAULT 0, "
        "ai_source TEXT DEFAULT '', "
        "root_cause TEXT DEFAULT 'none', "
        "root_cause_detail TEXT DEFAULT '', "
        "recommended_action TEXT DEFAULT 'monitor_only', "
        "action_target_pid INTEGER DEFAULT 0, "
        "action_target_name TEXT DEFAULT '', "
        "safety_gate TEXT DEFAULT 'OBSERVE_ONLY', "
        "blocked_reason TEXT DEFAULT '', "
        "cooldown_remaining_sec INTEGER DEFAULT 0, "
        "expected_gain_mb REAL DEFAULT 0, "
        "action_confidence REAL DEFAULT 0, "
        "candidate_safety_score REAL DEFAULT 0, "
        "candidate_count INTEGER DEFAULT 0, "
        "safe_to_heal INTEGER DEFAULT 0, "
        "dry_run INTEGER DEFAULT 1, "
        "user_state TEXT DEFAULT '', "
        "foreground_process TEXT DEFAULT '', "
        "top_process TEXT DEFAULT '');";

    if (sqlite3_exec(db_, decisionAuditSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* processIndexSql =
        "CREATE INDEX IF NOT EXISTS idx_process_samples_time ON process_samples(time);"
        "CREATE INDEX IF NOT EXISTS idx_process_samples_category_safety ON process_samples(category, safety);"
        "CREATE INDEX IF NOT EXISTS idx_process_samples_waste ON process_samples(waste_score DESC);"
        "CREATE INDEX IF NOT EXISTS idx_user_intent_samples_state ON user_intent_samples(user_state, app_kind);"
        "CREATE INDEX IF NOT EXISTS idx_decision_audits_level ON decision_audits(level, root_cause, safety_gate);";

    if (sqlite3_exec(db_, processIndexSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* migrationsSql =
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "version INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "applied_at INTEGER NOT NULL);"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(1, 'initial_metrics_schema', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(2, 'network_process_columns', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(3, 'scenario_labels', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(4, 'process_genome_samples', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(5, 'process_sample_indexes_retention', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(6, 'user_intent_samples', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(7, 'decision_audits', strftime('%s','now'));";

    if (sqlite3_exec(db_, migrationsSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    return EnsureColumn("net_down_kbps", "REAL DEFAULT 0") &&
           EnsureColumn("net_up_kbps", "REAL DEFAULT 0") &&
           EnsureColumn("process_count", "INTEGER DEFAULT 0") &&
           EnsureColumn("scenario_label", "TEXT DEFAULT 'auto'") &&
           EnsureColumn("top_process", "TEXT DEFAULT ''") &&
           EnsureColumn("top_process_cpu", "REAL DEFAULT 0") &&
           EnsureColumn("top_process_mem", "REAL DEFAULT 0") &&
           EnsureTableColumn("process_samples", "intent_role", "TEXT DEFAULT 'none'") &&
           EnsureTableColumn("process_samples", "is_recently_active", "INTEGER DEFAULT 0") &&
           EnsureTableColumn("process_samples", "matches_user_intent", "INTEGER DEFAULT 0");
}

bool MetricsStorage::EnsureColumn(const string& columnName, const string& columnDefinition) {
    return EnsureTableColumn("metrics", columnName, columnDefinition);
}

bool MetricsStorage::EnsureTableColumn(const string& tableName, const string& columnName, const string& columnDefinition) {
    const string pragma = "PRAGMA table_info(" + tableName + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, pragma.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool columnExists = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        if (name && columnName == reinterpret_cast<const char*>(name)) {
            columnExists = true;
            break;
        }
    }
    sqlite3_finalize(stmt);

    if (columnExists) return true;

    const string alterSql = "ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + columnDefinition + ";";
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, alterSql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

void MetricsStorage::LogSnapshot(const SystemSnapshot& snapshot) {
    LogBatch(vector<SystemSnapshot>{ snapshot });
}

void MetricsStorage::LogBatch(const vector<SystemSnapshot>& snapshots) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_ || snapshots.empty()) return;
    ExecuteInsertBatch(snapshots);
}

bool MetricsStorage::LogDecisionAudit(
    const SystemSnapshot& snapshot,
    const DecisionResult& decision,
    double aiProbability,
    double aiConfidence,
    const string& aiSource
) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    const char* insertSql =
        "INSERT OR REPLACE INTO decision_audits("
        "time, level, risk_score, anomaly_score, pressure_score, ai_probability, ai_confidence, ai_source, "
        "root_cause, root_cause_detail, recommended_action, action_target_pid, action_target_name, "
        "safety_gate, blocked_reason, cooldown_remaining_sec, expected_gain_mb, action_confidence, "
        "candidate_safety_score, candidate_count, safe_to_heal, dry_run, user_state, foreground_process, top_process"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(snapshot.timestamp));
    sqlite3_bind_text(stmt, 2, DecisionEngine::ToString(decision.level), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, decision.riskScore);
    sqlite3_bind_double(stmt, 4, decision.anomalyScore);
    sqlite3_bind_double(stmt, 5, decision.pressureScore);
    sqlite3_bind_double(stmt, 6, aiProbability);
    sqlite3_bind_double(stmt, 7, aiConfidence);
    sqlite3_bind_text(stmt, 8, aiSource.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, decision.rootCause.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, decision.rootCauseDetail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, decision.recommendedAction.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 12, static_cast<sqlite3_int64>(decision.actionTargetPid));
    sqlite3_bind_text(stmt, 13, decision.actionTarget.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, decision.safetyGate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, decision.blockedReason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 16, decision.cooldownRemainingSeconds);
    sqlite3_bind_double(stmt, 17, decision.expectedGainMB);
    sqlite3_bind_double(stmt, 18, decision.actionConfidence);
    sqlite3_bind_double(stmt, 19, decision.candidateSafetyScore);
    sqlite3_bind_int(stmt, 20, decision.candidateCount);
    sqlite3_bind_int(stmt, 21, decision.safeToHeal ? 1 : 0);
    sqlite3_bind_int(stmt, 22, decision.dryRun ? 1 : 0);
    sqlite3_bind_text(stmt, 23, snapshot.intent.userState.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 24, snapshot.intent.foregroundProcess.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 25, snapshot.topProcess.name.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool MetricsStorage::ExecuteInsertBatch(const vector<SystemSnapshot>& snapshots) {
    const char* insertSql =
        "INSERT INTO metrics(time, cpu, mem, disk, net_down_kbps, net_up_kbps, process_count, scenario_label, top_process, top_process_cpu, top_process_mem) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);";
    const char* processInsertSql =
        "INSERT OR REPLACE INTO process_samples("
        "time, pid, parent_pid, name, exe_path, category, safety, intent_role, recommendation, reason, "
        "cpu, working_set_mb, private_mb, io_read_kbps, io_write_kbps, lifetime_sec, "
        "thread_count, handle_count, priority_class, session_id, is_foreground, is_recently_active, matches_user_intent, has_visible_window, "
        "trusted_path, signed_trusted, signature_status, waste_score, importance_score, safety_score, expected_gain_mb"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, ?31);";
    const char* intentInsertSql =
        "INSERT OR REPLACE INTO user_intent_samples("
        "time, foreground_pid, foreground_process, foreground_path, foreground_title, app_kind, user_state, reason, "
        "idle_seconds, focus_duration_seconds, is_fullscreen, protect_foreground_family, recent_processes"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_stmt* processStmt = nullptr;
    if (sqlite3_prepare_v2(db_, processInsertSql, -1, &processStmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_stmt* intentStmt = nullptr;
    if (sqlite3_prepare_v2(db_, intentInsertSql, -1, &intentStmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_finalize(processStmt);
        return false;
    }

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_finalize(processStmt);
        sqlite3_finalize(intentStmt);
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    bool ok = true;
    for (const auto& snapshot : snapshots) {
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(snapshot.timestamp));
        sqlite3_bind_double(stmt, 2, snapshot.cpuUsage);
        sqlite3_bind_double(stmt, 3, snapshot.memoryUsage);
        sqlite3_bind_double(stmt, 4, snapshot.diskFree);
        sqlite3_bind_double(stmt, 5, snapshot.netDownKBps);
        sqlite3_bind_double(stmt, 6, snapshot.netUpKBps);
        sqlite3_bind_int(stmt, 7, snapshot.processCount);
        sqlite3_bind_text(stmt, 8, snapshot.scenarioLabel.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, snapshot.topProcess.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 10, snapshot.topProcess.cpuPercent);
        sqlite3_bind_double(stmt, 11, snapshot.topProcess.memoryMB);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            ok = false;
            break;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_int64(intentStmt, 1, static_cast<sqlite3_int64>(snapshot.timestamp));
        sqlite3_bind_int64(intentStmt, 2, static_cast<sqlite3_int64>(snapshot.intent.foregroundPid));
        sqlite3_bind_text(intentStmt, 3, snapshot.intent.foregroundProcess.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(intentStmt, 4, snapshot.intent.foregroundPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(intentStmt, 5, snapshot.intent.foregroundTitle.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(intentStmt, 6, snapshot.intent.appKind.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(intentStmt, 7, snapshot.intent.userState.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(intentStmt, 8, snapshot.intent.reason.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(intentStmt, 9, snapshot.intent.idleSeconds);
        sqlite3_bind_double(intentStmt, 10, snapshot.intent.focusDurationSeconds);
        sqlite3_bind_int(intentStmt, 11, snapshot.intent.isFullscreen ? 1 : 0);
        sqlite3_bind_int(intentStmt, 12, snapshot.intent.protectForegroundFamily ? 1 : 0);
        const string recentProcesses = JoinStrings(snapshot.intent.recentProcesses);
        sqlite3_bind_text(intentStmt, 13, recentProcesses.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(intentStmt) != SQLITE_DONE) {
            ok = false;
            break;
        }
        sqlite3_reset(intentStmt);
        sqlite3_clear_bindings(intentStmt);

        for (const auto& process : snapshot.processGenome) {
            sqlite3_bind_int64(processStmt, 1, static_cast<sqlite3_int64>(snapshot.timestamp));
            sqlite3_bind_int64(processStmt, 2, static_cast<sqlite3_int64>(process.pid));
            sqlite3_bind_int64(processStmt, 3, static_cast<sqlite3_int64>(process.parentPid));
            sqlite3_bind_text(processStmt, 4, process.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 5, process.exePath.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 6, process.category.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 7, process.safety.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 8, process.intentRole.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 9, process.recommendation.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 10, process.reason.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(processStmt, 11, process.cpuPercent);
            sqlite3_bind_double(processStmt, 12, process.workingSetMB);
            sqlite3_bind_double(processStmt, 13, process.privateBytesMB);
            sqlite3_bind_double(processStmt, 14, process.ioReadKBps);
            sqlite3_bind_double(processStmt, 15, process.ioWriteKBps);
            sqlite3_bind_double(processStmt, 16, process.lifetimeSeconds);
            sqlite3_bind_int(processStmt, 17, process.threadCount);
            sqlite3_bind_int(processStmt, 18, process.handleCount);
            sqlite3_bind_int(processStmt, 19, static_cast<int>(process.priorityClass));
            sqlite3_bind_int(processStmt, 20, static_cast<int>(process.sessionId));
            sqlite3_bind_int(processStmt, 21, process.isForeground ? 1 : 0);
            sqlite3_bind_int(processStmt, 22, process.isRecentlyActive ? 1 : 0);
            sqlite3_bind_int(processStmt, 23, process.matchesUserIntent ? 1 : 0);
            sqlite3_bind_int(processStmt, 24, process.hasVisibleWindow ? 1 : 0);
            sqlite3_bind_int(processStmt, 25, process.isTrustedPath ? 1 : 0);
            sqlite3_bind_int(processStmt, 26, process.isSignedTrusted ? 1 : 0);
            sqlite3_bind_text(processStmt, 27, process.signatureStatus.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(processStmt, 28, process.wasteScore);
            sqlite3_bind_double(processStmt, 29, process.importanceScore);
            sqlite3_bind_double(processStmt, 30, process.safetyScore);
            sqlite3_bind_double(processStmt, 31, process.expectedGainMB);

            if (sqlite3_step(processStmt) != SQLITE_DONE) {
                ok = false;
                break;
            }

            sqlite3_reset(processStmt);
            sqlite3_clear_bindings(processStmt);
        }

        if (!ok) break;
    }

    sqlite3_finalize(stmt);
    sqlite3_finalize(processStmt);
    sqlite3_finalize(intentStmt);

    if (ok && !snapshots.empty()) {
        sqlite3_stmt* retentionStmt = nullptr;
        const char* retentionSql = "DELETE FROM process_samples WHERE time < ?1;";
        if (sqlite3_prepare_v2(db_, retentionSql, -1, &retentionStmt, nullptr) == SQLITE_OK) {
            const long long cutoff = snapshots.back().timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        } else {
            ok = false;
        }
    }

    if (ok && !snapshots.empty()) {
        sqlite3_stmt* retentionStmt = nullptr;
        const char* retentionSql = "DELETE FROM user_intent_samples WHERE time < ?1;";
        if (sqlite3_prepare_v2(db_, retentionSql, -1, &retentionStmt, nullptr) == SQLITE_OK) {
            const long long cutoff = snapshots.back().timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        } else {
            ok = false;
        }
    }

    const char* endSql = ok ? "COMMIT;" : "ROLLBACK;";
    if (sqlite3_exec(db_, endSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    if (errMsg) sqlite3_free(errMsg);
    return ok;
}
