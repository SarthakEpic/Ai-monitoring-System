#include <cassert>
#include <string>

#include "DatabaseMigrations.h"
#include "sqlite3.h"

namespace {
bool TableExists(sqlite3* database, const char* tableName) {
    sqlite3_stmt* statement = nullptr;
    constexpr const char* kSql = "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1;";
    assert(sqlite3_prepare_v2(database, kSql, -1, &statement, nullptr) == SQLITE_OK);
    sqlite3_bind_text(statement, 1, tableName, -1, SQLITE_TRANSIENT);
    const bool exists = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return exists;
}

int RowCount(sqlite3* database, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_ROW);
    const int count = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return count;
}
}  // namespace

int main() {
    sqlite3* database = nullptr;
    assert(sqlite3_open(":memory:", &database) == SQLITE_OK);
    assert(sqlite3_exec(database, "CREATE TABLE metrics(time INTEGER, cpu REAL); INSERT INTO metrics VALUES(1, 42);", nullptr, nullptr, nullptr) == SQLITE_OK);

    std::string error;
    assert(ApplyAegisMigrations(database, error));
    assert(CurrentAegisSchemaVersion(database) == 19);
    assert(TableExists(database, "devices"));
    assert(TableExists(database, "sessions"));
    assert(TableExists(database, "workload_episodes"));
    assert(TableExists(database, "telemetry_summaries"));
    assert(TableExists(database, "qoe_outcomes"));
    assert(TableExists(database, "label_provenance"));
    assert(TableExists(database, "action_outcomes_v3"));
    assert(TableExists(database, "collector_health_samples"));
    assert(TableExists(database, "device_support_descriptors"));
    assert(TableExists(database, "telemetry_robust_baselines"));
    assert(TableExists(database, "runtime_performance_samples"));
    assert(RowCount(database, "SELECT COUNT(*) FROM metrics;") == 1);
    assert(RowCount(database, "SELECT COUNT(*) FROM schema_migrations WHERE version = 19;") == 1);

    assert(ApplyAegisMigrations(database, error));
    assert(RowCount(database, "SELECT COUNT(*) FROM schema_migrations WHERE version = 19;") == 1);
    sqlite3_close(database);
    return 0;
}
