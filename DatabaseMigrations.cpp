#include "DatabaseMigrations.h"

#include "sqlite3.h"

namespace {
constexpr int kAegisSchemaVersion = 19;

bool Execute(sqlite3* database, const char* sql, std::string& error) {
    char* message = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &message) == SQLITE_OK) {
        return true;
    }

    error = message ? message : "SQLite command failed without an error message.";
    if (message) {
        sqlite3_free(message);
    }
    return false;
}

bool MigrationExists(sqlite3* database, int version, bool& exists, std::string& error) {
    sqlite3_stmt* statement = nullptr;
    constexpr const char* kSql = "SELECT 1 FROM schema_migrations WHERE version = ?1 LIMIT 1;";
    if (sqlite3_prepare_v2(database, kSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(database);
        return false;
    }

    sqlite3_bind_int(statement, 1, version);
    const int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result == SQLITE_ROW) {
        exists = true;
        return true;
    }
    if (result == SQLITE_DONE) {
        exists = false;
        return true;
    }

    error = sqlite3_errmsg(database);
    return false;
}

constexpr const char* kPhaseOneV3Schema = R"sql(
CREATE TABLE IF NOT EXISTS devices (
    device_id TEXT PRIMARY KEY,
    hardware_fingerprint TEXT NOT NULL,
    platform TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    last_seen_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions (
    session_id TEXT PRIMARY KEY,
    device_id TEXT NOT NULL,
    started_at INTEGER NOT NULL,
    ended_at INTEGER,
    app_version TEXT NOT NULL,
    runtime_mode TEXT NOT NULL,
    FOREIGN KEY(device_id) REFERENCES devices(device_id)
);
CREATE TABLE IF NOT EXISTS workload_episodes (
    episode_id TEXT PRIMARY KEY,
    device_id TEXT NOT NULL,
    session_id TEXT NOT NULL,
    workload_phase TEXT NOT NULL,
    application_family TEXT NOT NULL,
    foreground_behavior TEXT NOT NULL,
    started_at INTEGER NOT NULL,
    ended_at INTEGER NOT NULL,
    start_reason TEXT NOT NULL,
    end_reason TEXT NOT NULL,
    quality_status TEXT NOT NULL,
    provenance TEXT NOT NULL,
    FOREIGN KEY(device_id) REFERENCES devices(device_id),
    FOREIGN KEY(session_id) REFERENCES sessions(session_id)
);
CREATE TABLE IF NOT EXISTS telemetry_summaries (
    summary_id TEXT PRIMARY KEY,
    episode_id TEXT NOT NULL,
    observed_at INTEGER NOT NULL,
    cpu_percent REAL,
    memory_percent REAL,
    disk_free_percent REAL,
    network_down_kbps REAL,
    network_up_kbps REAL,
    process_count INTEGER,
    collector_health TEXT NOT NULL,
    schema_version INTEGER NOT NULL DEFAULT 3,
    FOREIGN KEY(episode_id) REFERENCES workload_episodes(episode_id)
);
CREATE TABLE IF NOT EXISTS qoe_outcomes (
    outcome_id TEXT PRIMARY KEY,
    episode_id TEXT NOT NULL,
    observed_at INTEGER NOT NULL,
    app_launch_ms REAL,
    foreground_switch_ms REAL,
    input_latency_ms REAL,
    ui_stall_ratio REAL,
    user_impact_event INTEGER NOT NULL DEFAULT 0,
    collector_health TEXT NOT NULL,
    FOREIGN KEY(episode_id) REFERENCES workload_episodes(episode_id)
);
CREATE TABLE IF NOT EXISTS label_provenance (
    label_id TEXT PRIMARY KEY,
    episode_id TEXT NOT NULL,
    label TEXT NOT NULL,
    provenance TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    annotator TEXT NOT NULL DEFAULT 'system',
    certification_eligible INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(episode_id) REFERENCES workload_episodes(episode_id)
);
CREATE TABLE IF NOT EXISTS action_outcomes_v3 (
    action_outcome_id TEXT PRIMARY KEY,
    episode_id TEXT NOT NULL,
    action_type TEXT NOT NULL,
    action_target TEXT NOT NULL,
    recommended_at INTEGER NOT NULL,
    executed_at INTEGER,
    verified_at INTEGER,
    result TEXT NOT NULL,
    recovered_memory_mb REAL,
    cpu_delta_percent REAL,
    user_impact_detected INTEGER NOT NULL DEFAULT 0,
    safety_mode TEXT NOT NULL,
    FOREIGN KEY(episode_id) REFERENCES workload_episodes(episode_id)
);
CREATE TABLE IF NOT EXISTS collector_health_samples (
    health_id TEXT PRIMARY KEY,
    collector_name TEXT NOT NULL,
    observed_at INTEGER NOT NULL,
    status TEXT NOT NULL,
    duration_ms REAL,
    failure_reason TEXT,
    backoff_until INTEGER
);
CREATE INDEX IF NOT EXISTS idx_sessions_device_time ON sessions(device_id, started_at);
CREATE INDEX IF NOT EXISTS idx_workload_episodes_device_time ON workload_episodes(device_id, started_at);
CREATE INDEX IF NOT EXISTS idx_telemetry_summaries_episode_time ON telemetry_summaries(episode_id, observed_at);
CREATE INDEX IF NOT EXISTS idx_qoe_outcomes_episode_time ON qoe_outcomes(episode_id, observed_at);
CREATE INDEX IF NOT EXISTS idx_label_provenance_episode ON label_provenance(episode_id, provenance);
CREATE INDEX IF NOT EXISTS idx_action_outcomes_episode_time ON action_outcomes_v3(episode_id, recommended_at);
CREATE INDEX IF NOT EXISTS idx_collector_health_name_time ON collector_health_samples(collector_name, observed_at);
)sql";

constexpr const char* kPhaseOneV3SupportSchema = R"sql(
CREATE TABLE IF NOT EXISTS device_support_descriptors (
    device_id TEXT PRIMARY KEY,
    windows_build_family TEXT NOT NULL,
    cpu_core_tier TEXT NOT NULL,
    ram_tier TEXT NOT NULL,
    storage_tier TEXT NOT NULL,
    gpu_tier TEXT NOT NULL,
    power_mode TEXT NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY(device_id) REFERENCES devices(device_id)
);
CREATE INDEX IF NOT EXISTS idx_device_support_windows_cpu ON device_support_descriptors(windows_build_family, cpu_core_tier, ram_tier);
)sql";

constexpr const char* kPhaseOneV3RobustBaselineSchema = R"sql(
CREATE TABLE IF NOT EXISTS telemetry_robust_baselines (
    summary_id TEXT PRIMARY KEY,
    cpu_median REAL NOT NULL,
    cpu_mad REAL NOT NULL,
    cpu_robust_z REAL NOT NULL,
    memory_median REAL NOT NULL,
    memory_mad REAL NOT NULL,
    memory_robust_z REAL NOT NULL,
    disk_median REAL NOT NULL,
    disk_mad REAL NOT NULL,
    disk_robust_z REAL NOT NULL,
    sample_count INTEGER NOT NULL,
    feature_source_version TEXT NOT NULL,
    missingness_mask TEXT NOT NULL,
    FOREIGN KEY(summary_id) REFERENCES telemetry_summaries(summary_id)
);
CREATE INDEX IF NOT EXISTS idx_telemetry_robust_baselines_source ON telemetry_robust_baselines(feature_source_version, sample_count);
)sql";

constexpr const char* kPhaseOneRuntimePerformanceSchema = R"sql(
CREATE TABLE IF NOT EXISTS runtime_performance_samples (
    observed_at INTEGER PRIMARY KEY,
    collector_p95_ms REAL NOT NULL,
    inference_p95_ms REAL NOT NULL,
    process_cpu_percent REAL NOT NULL,
    process_io_bytes_per_second REAL NOT NULL,
    wakeups_per_second REAL NOT NULL,
    working_set_mb REAL NOT NULL,
    storage_batch_latency_ms REAL NOT NULL,
    pending_writes INTEGER NOT NULL,
    observer_state TEXT NOT NULL,
    schema_version INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_runtime_performance_observer ON runtime_performance_samples(observer_state, observed_at);
)sql";
}  // namespace

int CurrentAegisSchemaVersion(sqlite3* database) {
    if (!database) {
        return 0;
    }

    sqlite3_stmt* statement = nullptr;
    constexpr const char* kSql = "SELECT COALESCE(MAX(version), 0) FROM schema_migrations;";
    if (sqlite3_prepare_v2(database, kSql, -1, &statement, nullptr) != SQLITE_OK) {
        return 0;
    }

    int version = 0;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        version = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    return version;
}

bool ApplyAegisMigrations(sqlite3* database, std::string& error) {
    if (!database) {
        error = "Cannot migrate a null SQLite database handle.";
        return false;
    }

    if (!Execute(database,
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at INTEGER NOT NULL);",
        error)) {
        return false;
    }
    if (!Execute(database, "BEGIN IMMEDIATE;", error)) {
        return false;
    }

    bool v16Applied = false;
    bool v17Applied = false;
    bool v18Applied = false;
    bool v19Applied = false;
    bool applied = MigrationExists(database, 16, v16Applied, error) &&
        MigrationExists(database, 17, v17Applied, error) &&
        MigrationExists(database, 18, v18Applied, error) &&
        MigrationExists(database, 19, v19Applied, error);
    if (applied && !v16Applied) {
        applied = Execute(database, kPhaseOneV3Schema, error) &&
            Execute(database,
                "INSERT INTO schema_migrations(version, name, applied_at) VALUES(16, 'phase1_v3_episode_data_contract', strftime('%s','now'));",
                error);
    }
    if (applied && !v17Applied) {
        applied = Execute(database, kPhaseOneV3SupportSchema, error) &&
            Execute(database,
                "INSERT INTO schema_migrations(version, name, applied_at) VALUES(17, 'phase1_v3_device_support_descriptor', strftime('%s','now'));",
                error);
    }
    if (applied && !v18Applied) {
        applied = Execute(database, kPhaseOneV3RobustBaselineSchema, error) &&
            Execute(database,
                "INSERT INTO schema_migrations(version, name, applied_at) VALUES(18, 'phase1_v3_robust_baseline_features', strftime('%s','now'));",
                error);
    }
    if (applied && !v19Applied) {
        applied = Execute(database, kPhaseOneRuntimePerformanceSchema, error) &&
            Execute(database,
                "INSERT INTO schema_migrations(version, name, applied_at) VALUES(19, 'phase1_runtime_performance_evidence', strftime('%s','now'));",
                error);
    }
    if (applied) {
        applied = Execute(database, "COMMIT;", error);
    }
    if (applied) {
        return true;
    }

    std::string rollbackError;
    Execute(database, "ROLLBACK;", rollbackError);
    return false;
}