#include "MetricsStorage.h"

#include <ctime>
#include <string>
#include <vector>

#include "sqlite3.h"

using namespace std;

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
        "top_process TEXT DEFAULT '', "
        "top_process_cpu REAL DEFAULT 0, "
        "top_process_mem REAL DEFAULT 0);";

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, createSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    return EnsureColumn("net_down_kbps", "REAL DEFAULT 0") &&
           EnsureColumn("net_up_kbps", "REAL DEFAULT 0") &&
           EnsureColumn("process_count", "INTEGER DEFAULT 0") &&
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
        "INSERT INTO metrics(time, cpu, mem, disk, net_down_kbps, net_up_kbps, process_count, top_process, top_process_cpu, top_process_mem) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        sqlite3_finalize(stmt);
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
        sqlite3_bind_text(stmt, 8, snapshot.topProcess.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 9, snapshot.topProcess.cpuPercent);
        sqlite3_bind_double(stmt, 10, snapshot.topProcess.memoryMB);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            ok = false;
            break;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    sqlite3_finalize(stmt);

    const char* endSql = ok ? "COMMIT;" : "ROLLBACK;";
    if (sqlite3_exec(db_, endSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    if (errMsg) sqlite3_free(errMsg);
    return ok;
}
