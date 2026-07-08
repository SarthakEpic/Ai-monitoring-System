#include "MetricsStorage.h"

#include <ctime>
#include <string>
#include <vector>

#include "sqlite3.h"

using namespace std;

namespace {
constexpr long long PROCESS_SAMPLE_RETENTION_SECONDS = 7LL * 24LL * 60LL * 60LL;
}

MetricsStorage::~MetricsStorage() {
    Close();
}

bool MetricsStorage::Open(const string& path) {
    Close();

    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        db_ = nullptr;
        return false;
    }

    return EnsureSchema();
}

void MetricsStorage::Close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool MetricsStorage::IsReady() const {
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

    const char* processIndexSql =
        "CREATE INDEX IF NOT EXISTS idx_process_samples_time ON process_samples(time);"
        "CREATE INDEX IF NOT EXISTS idx_process_samples_category_safety ON process_samples(category, safety);"
        "CREATE INDEX IF NOT EXISTS idx_process_samples_waste ON process_samples(waste_score DESC);";

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
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(5, 'process_sample_indexes_retention', strftime('%s','now'));";

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
           EnsureColumn("top_process_mem", "REAL DEFAULT 0");
}

bool MetricsStorage::EnsureColumn(const string& columnName, const string& columnDefinition) {
    const string pragma = "PRAGMA table_info(metrics);";
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

    const string alterSql = "ALTER TABLE metrics ADD COLUMN " + columnName + " " + columnDefinition + ";";
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
    if (!db_ || snapshots.empty()) return;
    ExecuteInsertBatch(snapshots);
}

bool MetricsStorage::ExecuteInsertBatch(const vector<SystemSnapshot>& snapshots) {
    const char* insertSql =
        "INSERT INTO metrics(time, cpu, mem, disk, net_down_kbps, net_up_kbps, process_count, scenario_label, top_process, top_process_cpu, top_process_mem) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);";
    const char* processInsertSql =
        "INSERT OR REPLACE INTO process_samples("
        "time, pid, parent_pid, name, exe_path, category, safety, recommendation, reason, "
        "cpu, working_set_mb, private_mb, io_read_kbps, io_write_kbps, lifetime_sec, "
        "thread_count, handle_count, priority_class, session_id, is_foreground, has_visible_window, "
        "trusted_path, signed_trusted, signature_status, waste_score, importance_score, safety_score, expected_gain_mb"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_stmt* processStmt = nullptr;
    if (sqlite3_prepare_v2(db_, processInsertSql, -1, &processStmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_finalize(processStmt);
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

        for (const auto& process : snapshot.processGenome) {
            sqlite3_bind_int64(processStmt, 1, static_cast<sqlite3_int64>(snapshot.timestamp));
            sqlite3_bind_int64(processStmt, 2, static_cast<sqlite3_int64>(process.pid));
            sqlite3_bind_int64(processStmt, 3, static_cast<sqlite3_int64>(process.parentPid));
            sqlite3_bind_text(processStmt, 4, process.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 5, process.exePath.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 6, process.category.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 7, process.safety.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 8, process.recommendation.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(processStmt, 9, process.reason.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(processStmt, 10, process.cpuPercent);
            sqlite3_bind_double(processStmt, 11, process.workingSetMB);
            sqlite3_bind_double(processStmt, 12, process.privateBytesMB);
            sqlite3_bind_double(processStmt, 13, process.ioReadKBps);
            sqlite3_bind_double(processStmt, 14, process.ioWriteKBps);
            sqlite3_bind_double(processStmt, 15, process.lifetimeSeconds);
            sqlite3_bind_int(processStmt, 16, process.threadCount);
            sqlite3_bind_int(processStmt, 17, process.handleCount);
            sqlite3_bind_int(processStmt, 18, static_cast<int>(process.priorityClass));
            sqlite3_bind_int(processStmt, 19, static_cast<int>(process.sessionId));
            sqlite3_bind_int(processStmt, 20, process.isForeground ? 1 : 0);
            sqlite3_bind_int(processStmt, 21, process.hasVisibleWindow ? 1 : 0);
            sqlite3_bind_int(processStmt, 22, process.isTrustedPath ? 1 : 0);
            sqlite3_bind_int(processStmt, 23, process.isSignedTrusted ? 1 : 0);
            sqlite3_bind_text(processStmt, 24, process.signatureStatus.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(processStmt, 25, process.wasteScore);
            sqlite3_bind_double(processStmt, 26, process.importanceScore);
            sqlite3_bind_double(processStmt, 27, process.safetyScore);
            sqlite3_bind_double(processStmt, 28, process.expectedGainMB);

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

    const char* endSql = ok ? "COMMIT;" : "ROLLBACK;";
    if (sqlite3_exec(db_, endSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    if (errMsg) sqlite3_free(errMsg);
    return ok;
}
