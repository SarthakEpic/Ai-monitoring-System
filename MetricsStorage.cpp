#include "MetricsStorage.h"
#include "DatabaseMigrations.h"

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

    const char* healPlansSql =
        "CREATE TABLE IF NOT EXISTS heal_plans ("
        "time INTEGER PRIMARY KEY, "
        "plan_id TEXT DEFAULT '', "
        "status TEXT DEFAULT 'OBSERVE', "
        "execution_mode TEXT DEFAULT 'SIMULATION_ONLY', "
        "action_type TEXT DEFAULT 'none', "
        "action_name TEXT DEFAULT 'monitor_only', "
        "target_kind TEXT DEFAULT 'system', "
        "target_pid INTEGER DEFAULT 0, "
        "target_name TEXT DEFAULT '', "
        "gate TEXT DEFAULT 'OBSERVE_ONLY', "
        "blocked_reason TEXT DEFAULT '', "
        "summary TEXT DEFAULT '', "
        "rationale TEXT DEFAULT '', "
        "pre_check TEXT DEFAULT '', "
        "execution_step TEXT DEFAULT '', "
        "post_check TEXT DEFAULT '', "
        "rollback_plan TEXT DEFAULT '', "
        "safety_notes TEXT DEFAULT '', "
        "expected_impact TEXT DEFAULT '', "
        "readiness_score REAL DEFAULT 0, "
        "confidence REAL DEFAULT 0, "
        "expected_gain_mb REAL DEFAULT 0, "
        "risk_before REAL DEFAULT 0, "
        "would_execute INTEGER DEFAULT 0, "
        "requires_user_approval INTEGER DEFAULT 0, "
        "blocked INTEGER DEFAULT 1, "
        "decision_level TEXT DEFAULT 'NORMAL', "
        "root_cause TEXT DEFAULT 'none', "
        "user_state TEXT DEFAULT '', "
        "foreground_process TEXT DEFAULT '');";

    if (sqlite3_exec(db_, healPlansSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* healVerificationsSql =
        "CREATE TABLE IF NOT EXISTS heal_verifications ("
        "time INTEGER PRIMARY KEY, "
        "verification_id TEXT DEFAULT '', "
        "plan_id TEXT DEFAULT '', "
        "status TEXT DEFAULT 'NOT_NEEDED', "
        "mode TEXT DEFAULT 'SIMULATION_ONLY', "
        "outcome_label TEXT DEFAULT 'NO_ACTION', "
        "summary TEXT DEFAULT '', "
        "reason TEXT DEFAULT '', "
        "success_criteria TEXT DEFAULT '', "
        "failure_criteria TEXT DEFAULT '', "
        "evidence TEXT DEFAULT '', "
        "observation_window_sec INTEGER DEFAULT 60, "
        "confidence REAL DEFAULT 0, "
        "risk_before REAL DEFAULT 0, "
        "risk_after_estimate REAL DEFAULT 0, "
        "risk_delta_estimate REAL DEFAULT 0, "
        "cpu_before REAL DEFAULT 0, "
        "cpu_after_estimate REAL DEFAULT 0, "
        "memory_before REAL DEFAULT 0, "
        "memory_after_estimate REAL DEFAULT 0, "
        "disk_before REAL DEFAULT 0, "
        "disk_after_estimate REAL DEFAULT 0, "
        "network_before_kbps REAL DEFAULT 0, "
        "network_after_estimate_kbps REAL DEFAULT 0, "
        "expected_gain_mb REAL DEFAULT 0, "
        "simulated_pass INTEGER DEFAULT 0, "
        "simulated_weak INTEGER DEFAULT 0, "
        "simulated_fail INTEGER DEFAULT 0, "
        "blocked INTEGER DEFAULT 0, "
        "plan_status TEXT DEFAULT '', "
        "action_name TEXT DEFAULT '', "
        "target_name TEXT DEFAULT '', "
        "root_cause TEXT DEFAULT 'none');";

    if (sqlite3_exec(db_, healVerificationsSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* safetyPolicySql =
        "CREATE TABLE IF NOT EXISTS safety_policy_evaluations ("
        "time INTEGER PRIMARY KEY, "
        "level TEXT DEFAULT 'SIMULATION_ONLY', "
        "reason_code TEXT DEFAULT 'NO_ACTION', "
        "reason TEXT DEFAULT '', "
        "target_pid INTEGER DEFAULT 0, "
        "target_name TEXT DEFAULT '', "
        "target_category TEXT DEFAULT 'UNKNOWN', "
        "target_safety TEXT DEFAULT 'UNKNOWN', "
        "policy_score REAL DEFAULT 0, "
        "minimum_required_score REAL DEFAULT 90, "
        "hard_block INTEGER DEFAULT 0, "
        "requires_approval INTEGER DEFAULT 0, "
        "simulation_only INTEGER DEFAULT 1, "
        "execution_eligible INTEGER DEFAULT 0, "
        "target_denied INTEGER DEFAULT 0, "
        "target_allowed INTEGER DEFAULT 0, "
        "target_protected INTEGER DEFAULT 0, "
        "user_intent_protected INTEGER DEFAULT 0, "
        "model_gate_passed INTEGER DEFAULT 0, "
        "decision_gate_passed INTEGER DEFAULT 0, "
        "plan_gate_passed INTEGER DEFAULT 0, "
        "verification_gate_passed INTEGER DEFAULT 0, "
        "confidence_gate_passed INTEGER DEFAULT 0, "
        "safe_mode_block INTEGER DEFAULT 1, "
        "dry_run_block INTEGER DEFAULT 1, "
        "auto_heal_disabled INTEGER DEFAULT 1, "
        "decision_level TEXT DEFAULT 'NORMAL', "
        "plan_status TEXT DEFAULT '', "
        "verification_status TEXT DEFAULT '', "
        "root_cause TEXT DEFAULT 'none');";

    if (sqlite3_exec(db_, safetyPolicySql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* runtimeHealthSql =
        "CREATE TABLE IF NOT EXISTS runtime_health_samples ("
        "time INTEGER PRIMARY KEY, "
        "status TEXT DEFAULT 'STARTING', "
        "summary TEXT DEFAULT '', "
        "prediction_source TEXT DEFAULT '', "
        "prediction_path TEXT DEFAULT '', "
        "last_failure TEXT DEFAULT 'none', "
        "availability_score REAL DEFAULT 0, "
        "model_success_rate REAL DEFAULT 100, "
        "fallback_rate REAL DEFAULT 0, "
        "storage_success_rate REAL DEFAULT 100, "
        "avg_prediction_latency_ms REAL DEFAULT 0, "
        "last_prediction_latency_ms REAL DEFAULT 0, "
        "total_cycles INTEGER DEFAULT 0, "
        "model_attempts INTEGER DEFAULT 0, "
        "model_successes INTEGER DEFAULT 0, "
        "model_failures INTEGER DEFAULT 0, "
        "service_successes INTEGER DEFAULT 0, "
        "process_successes INTEGER DEFAULT 0, "
        "fallback_predictions INTEGER DEFAULT 0, "
        "cached_predictions INTEGER DEFAULT 0, "
        "warmup_predictions INTEGER DEFAULT 0, "
        "storage_writes INTEGER DEFAULT 0, "
        "storage_failures INTEGER DEFAULT 0, "
        "alerts INTEGER DEFAULT 0, "
        "service_running INTEGER DEFAULT 0, "
        "storage_ready INTEGER DEFAULT 0);";

    if (sqlite3_exec(db_, runtimeHealthSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* adaptiveBaselineSql =
        "CREATE TABLE IF NOT EXISTS adaptive_baseline_samples ("
        "time INTEGER PRIMARY KEY, "
        "status TEXT DEFAULT 'WARMING_UP', "
        "summary TEXT DEFAULT '', "
        "dominant_metric TEXT DEFAULT 'none', "
        "sample_count INTEGER DEFAULT 0, "
        "ready INTEGER DEFAULT 0, "
        "confidence REAL DEFAULT 0, "
        "anomaly_score REAL DEFAULT 0, "
        "risk_hint REAL DEFAULT 0, "
        "risk_adjustment REAL DEFAULT 0, "
        "cpu_mean REAL DEFAULT 0, "
        "memory_mean REAL DEFAULT 0, "
        "disk_free_mean REAL DEFAULT 0, "
        "network_mean REAL DEFAULT 0, "
        "process_count_mean REAL DEFAULT 0, "
        "top_cpu_mean REAL DEFAULT 0, "
        "top_memory_mean REAL DEFAULT 0, "
        "cpu_deviation REAL DEFAULT 0, "
        "memory_deviation REAL DEFAULT 0, "
        "disk_deviation REAL DEFAULT 0, "
        "network_deviation REAL DEFAULT 0, "
        "process_deviation REAL DEFAULT 0, "
        "top_cpu_deviation REAL DEFAULT 0, "
        "top_memory_deviation REAL DEFAULT 0);";

    if (sqlite3_exec(db_, adaptiveBaselineSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    const char* lowEndAutopilotSql =
        "CREATE TABLE IF NOT EXISTS low_end_autopilot_samples ("
        "time INTEGER PRIMARY KEY, "
        "enabled INTEGER DEFAULT 0, "
        "low_end_device INTEGER DEFAULT 0, "
        "active INTEGER DEFAULT 0, "
        "mode TEXT DEFAULT 'STANDARD', "
        "status TEXT DEFAULT 'DISABLED', "
        "summary TEXT DEFAULT '', "
        "pressure_score REAL DEFAULT 0, "
        "memory_pressure REAL DEFAULT 0, "
        "cpu_pressure REAL DEFAULT 0, "
        "disk_pressure REAL DEFAULT 0, "
        "foreground_protected INTEGER DEFAULT 1, "
        "quick_restore_available INTEGER DEFAULT 0, "
        "reversible_action_count INTEGER DEFAULT 0, "
        "actions_recommended INTEGER DEFAULT 0, "
        "user_apps_touched INTEGER DEFAULT 0, "
        "estimated_recovered_ram_mb REAL DEFAULT 0, "
        "estimated_cpu_drop_percent REAL DEFAULT 0, "
        "primary_action TEXT DEFAULT 'monitor_only', "
        "primary_target TEXT DEFAULT 'system', "
        "safety_notes TEXT DEFAULT '');";

    if (sqlite3_exec(db_, lowEndAutopilotSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* autopilotActionsSql =
        "CREATE TABLE IF NOT EXISTS autopilot_actions ("
        "time INTEGER NOT NULL, "
        "rank INTEGER NOT NULL, "
        "action_type TEXT DEFAULT '', "
        "action_name TEXT DEFAULT '', "
        "target_pid INTEGER DEFAULT 0, "
        "target_name TEXT DEFAULT '', "
        "category TEXT DEFAULT 'UNKNOWN', "
        "safety TEXT DEFAULT 'UNKNOWN', "
        "reason TEXT DEFAULT '', "
        "reversibility TEXT DEFAULT '', "
        "expected_ram_mb REAL DEFAULT 0, "
        "expected_cpu_drop_percent REAL DEFAULT 0, "
        "safety_score REAL DEFAULT 0, "
        "foreground_protected INTEGER DEFAULT 1, "
        "user_app_touched INTEGER DEFAULT 0, "
        "reversible INTEGER DEFAULT 1, "
        "PRIMARY KEY(time, rank));";

    if (sqlite3_exec(db_, autopilotActionsSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* backgroundAgentSql =
        "CREATE TABLE IF NOT EXISTS background_agent_samples ("
        "time INTEGER PRIMARY KEY, "
        "enabled INTEGER DEFAULT 0, "
        "tray_icon_ready INTEGER DEFAULT 0, "
        "silent_monitoring INTEGER DEFAULT 0, "
        "start_on_boot_configured INTEGER DEFAULT 0, "
        "dashboard_visible INTEGER DEFAULT 1, "
        "quick_restore_available INTEGER DEFAULT 0, "
        "quick_restore_requested INTEGER DEFAULT 0, "
        "mode TEXT DEFAULT 'DASHBOARD', "
        "status TEXT DEFAULT 'DASHBOARD_ONLY', "
        "summary TEXT DEFAULT '', "
        "quick_restore_status TEXT DEFAULT 'IDLE', "
        "control_center TEXT DEFAULT 'dashboard');";

    if (sqlite3_exec(db_, backgroundAgentSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const char* benchmarkProofSql =
        "CREATE TABLE IF NOT EXISTS benchmark_proof_samples ("
        "time INTEGER PRIMARY KEY, "
        "status TEXT DEFAULT 'COLLECTING', "
        "mode TEXT DEFAULT 'ESTIMATE', "
        "summary TEXT DEFAULT '', "
        "before_cpu REAL DEFAULT 0, "
        "before_memory REAL DEFAULT 0, "
        "before_disk_free REAL DEFAULT 0, "
        "before_risk REAL DEFAULT 0, "
        "after_cpu_estimate REAL DEFAULT 0, "
        "after_memory_estimate REAL DEFAULT 0, "
        "after_disk_free_estimate REAL DEFAULT 0, "
        "after_risk_estimate REAL DEFAULT 0, "
        "recovered_ram_mb REAL DEFAULT 0, "
        "cpu_drop_percent REAL DEFAULT 0, "
        "disk_free_gain_percent REAL DEFAULT 0, "
        "risk_drop_percent REAL DEFAULT 0, "
        "actions_recommended INTEGER DEFAULT 0, "
        "user_apps_touched INTEGER DEFAULT 0, "
        "confidence REAL DEFAULT 0, "
        "foreground_process TEXT DEFAULT 'N/A');";

    if (sqlite3_exec(db_, benchmarkProofSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    const char* processIndexSql =
        "CREATE INDEX IF NOT EXISTS idx_process_samples_time ON process_samples(time);"
        "CREATE INDEX IF NOT EXISTS idx_process_samples_category_safety ON process_samples(category, safety);"
        "CREATE INDEX IF NOT EXISTS idx_process_samples_waste ON process_samples(waste_score DESC);"
        "CREATE INDEX IF NOT EXISTS idx_user_intent_samples_state ON user_intent_samples(user_state, app_kind);"
        "CREATE INDEX IF NOT EXISTS idx_decision_audits_level ON decision_audits(level, root_cause, safety_gate);"
        "CREATE INDEX IF NOT EXISTS idx_heal_plans_status ON heal_plans(status, action_type, gate);"
        "CREATE INDEX IF NOT EXISTS idx_heal_verifications_status ON heal_verifications(status, outcome_label, action_name);"
        "CREATE INDEX IF NOT EXISTS idx_safety_policy_level ON safety_policy_evaluations(level, reason_code, target_name);"
        "CREATE INDEX IF NOT EXISTS idx_runtime_health_status ON runtime_health_samples(status, prediction_source, prediction_path);"
        "CREATE INDEX IF NOT EXISTS idx_adaptive_baseline_status ON adaptive_baseline_samples(status, dominant_metric);"
        "CREATE INDEX IF NOT EXISTS idx_low_end_autopilot_status ON low_end_autopilot_samples(status, mode);"
        "CREATE INDEX IF NOT EXISTS idx_autopilot_actions_time ON autopilot_actions(time, rank);"
        "CREATE INDEX IF NOT EXISTS idx_background_agent_status ON background_agent_samples(status, mode);"
        "CREATE INDEX IF NOT EXISTS idx_benchmark_proof_status ON benchmark_proof_samples(status, mode);";

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
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(7, 'decision_audits', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(8, 'heal_plans', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(9, 'heal_verifications', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(10, 'safety_policy_evaluations', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(11, 'runtime_health_samples', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(12, 'adaptive_baseline_samples', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(13, 'low_end_autopilot_samples', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(14, 'background_agent_samples', strftime('%s','now'));"
        "INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) VALUES(15, 'benchmark_proof_samples', strftime('%s','now'));";

    if (sqlite3_exec(db_, migrationsSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    const bool legacySchemaReady =
        EnsureColumn("net_down_kbps", "REAL DEFAULT 0") &&
        EnsureColumn("net_up_kbps", "REAL DEFAULT 0") &&
        EnsureColumn("process_count", "INTEGER DEFAULT 0") &&
        EnsureColumn("scenario_label", "TEXT DEFAULT 'auto'") &&
        EnsureColumn("top_process", "TEXT DEFAULT ''") &&
        EnsureColumn("top_process_cpu", "REAL DEFAULT 0") &&
        EnsureColumn("top_process_mem", "REAL DEFAULT 0") &&
        EnsureTableColumn("process_samples", "intent_role", "TEXT DEFAULT 'none'") &&
        EnsureTableColumn("process_samples", "is_recently_active", "INTEGER DEFAULT 0") &&
        EnsureTableColumn("process_samples", "matches_user_intent", "INTEGER DEFAULT 0");
    if (!legacySchemaReady) {
        return false;
    }

    std::string migrationError;
    return ApplyAegisMigrations(db_, migrationError);
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

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok) {
        const long long cutoff = snapshot.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
        sqlite3_stmt* retentionStmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM heal_plans WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        }
    }

    if (ok) {
        const long long cutoff = snapshot.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
        sqlite3_stmt* retentionStmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM decision_audits WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        }
    }

    return ok;
}

bool MetricsStorage::LogHealPlan(
    const SystemSnapshot& snapshot,
    const DecisionResult& decision,
    const HealPlan& plan
) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    const char* insertSql =
        "INSERT OR REPLACE INTO heal_plans("
        "time, plan_id, status, execution_mode, action_type, action_name, target_kind, target_pid, target_name, "
        "gate, blocked_reason, summary, rationale, pre_check, execution_step, post_check, rollback_plan, safety_notes, "
        "expected_impact, readiness_score, confidence, expected_gain_mb, risk_before, would_execute, "
        "requires_user_approval, blocked, decision_level, root_cause, user_state, foreground_process"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(snapshot.timestamp));
    sqlite3_bind_text(stmt, 2, plan.planId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, plan.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, plan.executionMode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, plan.actionType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, plan.actionName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, plan.targetKind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(plan.targetPid));
    sqlite3_bind_text(stmt, 9, plan.targetName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, plan.gate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, plan.blockedReason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, plan.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, plan.rationale.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, plan.preCheck.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, plan.executionStep.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, plan.postCheck.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 17, plan.rollbackPlan.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 18, plan.safetyNotes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 19, plan.expectedImpact.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 20, plan.readinessScore);
    sqlite3_bind_double(stmt, 21, plan.confidence);
    sqlite3_bind_double(stmt, 22, plan.expectedGainMB);
    sqlite3_bind_double(stmt, 23, plan.riskBefore);
    sqlite3_bind_int(stmt, 24, plan.wouldExecute ? 1 : 0);
    sqlite3_bind_int(stmt, 25, plan.requiresUserApproval ? 1 : 0);
    sqlite3_bind_int(stmt, 26, plan.blocked ? 1 : 0);
    sqlite3_bind_text(stmt, 27, DecisionEngine::ToString(decision.level), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 28, decision.rootCause.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 29, snapshot.intent.userState.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 30, snapshot.intent.foregroundProcess.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool MetricsStorage::LogHealVerification(
    const SystemSnapshot& snapshot,
    const DecisionResult& decision,
    const HealPlan& plan,
    const HealVerification& verification
) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    const char* insertSql =
        "INSERT OR REPLACE INTO heal_verifications("
        "time, verification_id, plan_id, status, mode, outcome_label, summary, reason, success_criteria, "
        "failure_criteria, evidence, observation_window_sec, confidence, risk_before, risk_after_estimate, "
        "risk_delta_estimate, cpu_before, cpu_after_estimate, memory_before, memory_after_estimate, "
        "disk_before, disk_after_estimate, network_before_kbps, network_after_estimate_kbps, expected_gain_mb, "
        "simulated_pass, simulated_weak, simulated_fail, blocked, plan_status, action_name, target_name, root_cause"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, ?31, ?32, ?33);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(snapshot.timestamp));
    sqlite3_bind_text(stmt, 2, verification.verificationId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, plan.planId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, verification.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, verification.mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, verification.outcomeLabel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, verification.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, verification.reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, verification.successCriteria.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, verification.failureCriteria.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, verification.evidence.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 12, verification.observationWindowSeconds);
    sqlite3_bind_double(stmt, 13, verification.confidence);
    sqlite3_bind_double(stmt, 14, verification.riskBefore);
    sqlite3_bind_double(stmt, 15, verification.riskAfterEstimate);
    sqlite3_bind_double(stmt, 16, verification.riskDeltaEstimate);
    sqlite3_bind_double(stmt, 17, verification.cpuBefore);
    sqlite3_bind_double(stmt, 18, verification.cpuAfterEstimate);
    sqlite3_bind_double(stmt, 19, verification.memoryBefore);
    sqlite3_bind_double(stmt, 20, verification.memoryAfterEstimate);
    sqlite3_bind_double(stmt, 21, verification.diskBefore);
    sqlite3_bind_double(stmt, 22, verification.diskAfterEstimate);
    sqlite3_bind_double(stmt, 23, verification.networkBeforeKBps);
    sqlite3_bind_double(stmt, 24, verification.networkAfterEstimateKBps);
    sqlite3_bind_double(stmt, 25, verification.expectedGainMB);
    sqlite3_bind_int(stmt, 26, verification.simulatedPass ? 1 : 0);
    sqlite3_bind_int(stmt, 27, verification.simulatedWeak ? 1 : 0);
    sqlite3_bind_int(stmt, 28, verification.simulatedFail ? 1 : 0);
    sqlite3_bind_int(stmt, 29, verification.blocked ? 1 : 0);
    sqlite3_bind_text(stmt, 30, plan.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 31, plan.actionName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 32, plan.targetName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 33, decision.rootCause.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok) {
        const long long cutoff = snapshot.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
        sqlite3_stmt* retentionStmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM heal_verifications WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        }
    }

    return ok;
}

bool MetricsStorage::LogSafetyPolicy(
    const SystemSnapshot& snapshot,
    const DecisionResult& decision,
    const HealPlan& plan,
    const HealVerification& verification,
    const SafetyPolicyResult& policyResult
) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    const char* insertSql =
        "INSERT OR REPLACE INTO safety_policy_evaluations("
        "time, level, reason_code, reason, target_pid, target_name, target_category, target_safety, "
        "policy_score, minimum_required_score, hard_block, requires_approval, simulation_only, execution_eligible, "
        "target_denied, target_allowed, target_protected, user_intent_protected, model_gate_passed, decision_gate_passed, "
        "plan_gate_passed, verification_gate_passed, confidence_gate_passed, safe_mode_block, dry_run_block, "
        "auto_heal_disabled, decision_level, plan_status, verification_status, root_cause"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(snapshot.timestamp));
    sqlite3_bind_text(stmt, 2, policyResult.levelName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, policyResult.reasonCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, policyResult.reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(policyResult.targetPid));
    sqlite3_bind_text(stmt, 6, policyResult.targetName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, policyResult.targetCategory.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, policyResult.targetSafety.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 9, policyResult.policyScore);
    sqlite3_bind_double(stmt, 10, policyResult.minimumRequiredScore);
    sqlite3_bind_int(stmt, 11, policyResult.hardBlock ? 1 : 0);
    sqlite3_bind_int(stmt, 12, policyResult.requiresApproval ? 1 : 0);
    sqlite3_bind_int(stmt, 13, policyResult.simulationOnly ? 1 : 0);
    sqlite3_bind_int(stmt, 14, policyResult.executionEligible ? 1 : 0);
    sqlite3_bind_int(stmt, 15, policyResult.targetDenied ? 1 : 0);
    sqlite3_bind_int(stmt, 16, policyResult.targetAllowed ? 1 : 0);
    sqlite3_bind_int(stmt, 17, policyResult.targetProtected ? 1 : 0);
    sqlite3_bind_int(stmt, 18, policyResult.userIntentProtected ? 1 : 0);
    sqlite3_bind_int(stmt, 19, policyResult.modelGatePassed ? 1 : 0);
    sqlite3_bind_int(stmt, 20, policyResult.decisionGatePassed ? 1 : 0);
    sqlite3_bind_int(stmt, 21, policyResult.planGatePassed ? 1 : 0);
    sqlite3_bind_int(stmt, 22, policyResult.verificationGatePassed ? 1 : 0);
    sqlite3_bind_int(stmt, 23, policyResult.confidenceGatePassed ? 1 : 0);
    sqlite3_bind_int(stmt, 24, policyResult.safeModeBlock ? 1 : 0);
    sqlite3_bind_int(stmt, 25, policyResult.dryRunBlock ? 1 : 0);
    sqlite3_bind_int(stmt, 26, policyResult.autoHealDisabled ? 1 : 0);
    sqlite3_bind_text(stmt, 27, DecisionEngine::ToString(decision.level), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 28, plan.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 29, verification.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 30, decision.rootCause.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok) {
        const long long cutoff = snapshot.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
        sqlite3_stmt* retentionStmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM safety_policy_evaluations WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        }
    }

    return ok;
}

bool MetricsStorage::LogRuntimeHealth(const RuntimeHealthSample& sample) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    const char* insertSql =
        "INSERT OR REPLACE INTO runtime_health_samples("
        "time, status, summary, prediction_source, prediction_path, last_failure, availability_score, "
        "model_success_rate, fallback_rate, storage_success_rate, avg_prediction_latency_ms, last_prediction_latency_ms, "
        "total_cycles, model_attempts, model_successes, model_failures, service_successes, process_successes, "
        "fallback_predictions, cached_predictions, warmup_predictions, storage_writes, storage_failures, alerts, "
        "service_running, storage_ready"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(sample.timestamp));
    sqlite3_bind_text(stmt, 2, sample.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sample.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, sample.predictionSource.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, sample.predictionPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, sample.lastFailure.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 7, sample.availabilityScore);
    sqlite3_bind_double(stmt, 8, sample.modelSuccessRate);
    sqlite3_bind_double(stmt, 9, sample.fallbackRate);
    sqlite3_bind_double(stmt, 10, sample.storageSuccessRate);
    sqlite3_bind_double(stmt, 11, sample.avgPredictionLatencyMs);
    sqlite3_bind_double(stmt, 12, sample.lastPredictionLatencyMs);
    sqlite3_bind_int(stmt, 13, sample.totalCycles);
    sqlite3_bind_int(stmt, 14, sample.modelAttempts);
    sqlite3_bind_int(stmt, 15, sample.modelSuccesses);
    sqlite3_bind_int(stmt, 16, sample.modelFailures);
    sqlite3_bind_int(stmt, 17, sample.serviceSuccesses);
    sqlite3_bind_int(stmt, 18, sample.processSuccesses);
    sqlite3_bind_int(stmt, 19, sample.fallbackPredictions);
    sqlite3_bind_int(stmt, 20, sample.cachedPredictions);
    sqlite3_bind_int(stmt, 21, sample.warmupPredictions);
    sqlite3_bind_int(stmt, 22, sample.storageWrites);
    sqlite3_bind_int(stmt, 23, sample.storageFailures);
    sqlite3_bind_int(stmt, 24, sample.alerts);
    sqlite3_bind_int(stmt, 25, sample.serviceRunning ? 1 : 0);
    sqlite3_bind_int(stmt, 26, sample.storageReady ? 1 : 0);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok) {
        const long long cutoff = sample.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
        sqlite3_stmt* retentionStmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM runtime_health_samples WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        }
    }

    return ok;
}

bool MetricsStorage::LogRuntimePerformance(const RuntimeHealthSample& sample) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    constexpr const char* kInsertSql =
        "INSERT OR REPLACE INTO runtime_performance_samples("
        "observed_at, collector_p95_ms, inference_p95_ms, process_cpu_percent, "
        "process_io_bytes_per_second, wakeups_per_second, working_set_mb, "
        "storage_batch_latency_ms, pending_writes, observer_state, schema_version"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, 1);";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, kInsertSql, -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(sample.timestamp));
    sqlite3_bind_double(statement, 2, sample.collectorP95Ms);
    sqlite3_bind_double(statement, 3, sample.inferenceP95Ms);
    sqlite3_bind_double(statement, 4, sample.processCpuPercent);
    sqlite3_bind_double(statement, 5, sample.processIoBytesPerSecond);
    sqlite3_bind_double(statement, 6, sample.wakeupsPerSecond);
    sqlite3_bind_double(statement, 7, sample.workingSetMb);
    sqlite3_bind_double(statement, 8, sample.storageBatchLatencyMs);
    sqlite3_bind_int(statement, 9, sample.pendingWrites);
    sqlite3_bind_text(statement, 10, sample.observerState.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (ok) {
        constexpr const char* kRetentionSql =
            "DELETE FROM runtime_performance_samples WHERE observed_at < ?1;";
        sqlite3_stmt* retentionStatement = nullptr;
        if (sqlite3_prepare_v2(db_, kRetentionSql, -1, &retentionStatement, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(
                retentionStatement,
                1,
                static_cast<sqlite3_int64>(sample.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS)
            );
            ok = sqlite3_step(retentionStatement) == SQLITE_DONE;
            sqlite3_finalize(retentionStatement);
        } else {
            ok = false;
        }
    }
    return ok;
}
bool MetricsStorage::LogAdaptiveBaseline(const AdaptiveBaselineResult& baseline) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    const char* insertSql =
        "INSERT OR REPLACE INTO adaptive_baseline_samples("
        "time, status, summary, dominant_metric, sample_count, ready, confidence, anomaly_score, risk_hint, risk_adjustment, "
        "cpu_mean, memory_mean, disk_free_mean, network_mean, process_count_mean, top_cpu_mean, top_memory_mean, "
        "cpu_deviation, memory_deviation, disk_deviation, network_deviation, process_deviation, top_cpu_deviation, top_memory_deviation"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(baseline.timestamp));
    sqlite3_bind_text(stmt, 2, baseline.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, baseline.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, baseline.dominantMetric.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, baseline.sampleCount);
    sqlite3_bind_int(stmt, 6, baseline.ready ? 1 : 0);
    sqlite3_bind_double(stmt, 7, baseline.confidence);
    sqlite3_bind_double(stmt, 8, baseline.anomalyScore);
    sqlite3_bind_double(stmt, 9, baseline.riskHint);
    sqlite3_bind_double(stmt, 10, baseline.riskAdjustment);
    sqlite3_bind_double(stmt, 11, baseline.cpuMean);
    sqlite3_bind_double(stmt, 12, baseline.memoryMean);
    sqlite3_bind_double(stmt, 13, baseline.diskFreeMean);
    sqlite3_bind_double(stmt, 14, baseline.networkMean);
    sqlite3_bind_double(stmt, 15, baseline.processCountMean);
    sqlite3_bind_double(stmt, 16, baseline.topCpuMean);
    sqlite3_bind_double(stmt, 17, baseline.topMemoryMean);
    sqlite3_bind_double(stmt, 18, baseline.cpuDeviation);
    sqlite3_bind_double(stmt, 19, baseline.memoryDeviation);
    sqlite3_bind_double(stmt, 20, baseline.diskDeviation);
    sqlite3_bind_double(stmt, 21, baseline.networkDeviation);
    sqlite3_bind_double(stmt, 22, baseline.processDeviation);
    sqlite3_bind_double(stmt, 23, baseline.topCpuDeviation);
    sqlite3_bind_double(stmt, 24, baseline.topMemoryDeviation);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok) {
        const long long cutoff = baseline.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
        sqlite3_stmt* retentionStmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM adaptive_baseline_samples WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        }
    }

    return ok;
}
bool MetricsStorage::LogLowEndAutopilot(const LowEndAutopilotResult& autopilot) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    const char* insertSql =
        "INSERT OR REPLACE INTO low_end_autopilot_samples("
        "time, enabled, low_end_device, active, mode, status, summary, pressure_score, memory_pressure, "
        "cpu_pressure, disk_pressure, foreground_protected, quick_restore_available, reversible_action_count, "
        "actions_recommended, user_apps_touched, estimated_recovered_ram_mb, estimated_cpu_drop_percent, "
        "primary_action, primary_target, safety_notes"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(autopilot.timestamp));
    sqlite3_bind_int(stmt, 2, autopilot.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 3, autopilot.lowEndDevice ? 1 : 0);
    sqlite3_bind_int(stmt, 4, autopilot.active ? 1 : 0);
    sqlite3_bind_text(stmt, 5, autopilot.mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, autopilot.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, autopilot.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, autopilot.pressureScore);
    sqlite3_bind_double(stmt, 9, autopilot.memoryPressure);
    sqlite3_bind_double(stmt, 10, autopilot.cpuPressure);
    sqlite3_bind_double(stmt, 11, autopilot.diskPressure);
    sqlite3_bind_int(stmt, 12, autopilot.foregroundProtected ? 1 : 0);
    sqlite3_bind_int(stmt, 13, autopilot.quickRestoreAvailable ? 1 : 0);
    sqlite3_bind_int(stmt, 14, autopilot.reversibleActionCount);
    sqlite3_bind_int(stmt, 15, autopilot.actionsRecommended);
    sqlite3_bind_int(stmt, 16, autopilot.userAppsTouched);
    sqlite3_bind_double(stmt, 17, autopilot.estimatedRecoveredRamMB);
    sqlite3_bind_double(stmt, 18, autopilot.estimatedCpuDropPercent);
    sqlite3_bind_text(stmt, 19, autopilot.primaryAction.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 20, autopilot.primaryTarget.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 21, autopilot.safetyNotes.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok) return false;

    sqlite3_stmt* deleteStmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM autopilot_actions WHERE time = ?1;", -1, &deleteStmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(deleteStmt, 1, static_cast<sqlite3_int64>(autopilot.timestamp));
    ok = sqlite3_step(deleteStmt) == SQLITE_DONE;
    sqlite3_finalize(deleteStmt);
    if (!ok) return false;

    const char* actionSql =
        "INSERT OR REPLACE INTO autopilot_actions("
        "time, rank, action_type, action_name, target_pid, target_name, category, safety, reason, reversibility, "
        "expected_ram_mb, expected_cpu_drop_percent, safety_score, foreground_protected, user_app_touched, reversible"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16);";

    for (const auto& action : autopilot.recommendations) {
        sqlite3_stmt* actionStmt = nullptr;
        if (sqlite3_prepare_v2(db_, actionSql, -1, &actionStmt, nullptr) != SQLITE_OK) {
            return false;
        }
        sqlite3_bind_int64(actionStmt, 1, static_cast<sqlite3_int64>(autopilot.timestamp));
        sqlite3_bind_int(actionStmt, 2, action.rank);
        sqlite3_bind_text(actionStmt, 3, action.actionType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(actionStmt, 4, action.actionName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(actionStmt, 5, static_cast<sqlite3_int64>(action.targetPid));
        sqlite3_bind_text(actionStmt, 6, action.targetName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(actionStmt, 7, action.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(actionStmt, 8, action.safety.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(actionStmt, 9, action.reason.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(actionStmt, 10, action.reversibility.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(actionStmt, 11, action.expectedRecoveredRamMB);
        sqlite3_bind_double(actionStmt, 12, action.expectedCpuDropPercent);
        sqlite3_bind_double(actionStmt, 13, action.safetyScore);
        sqlite3_bind_int(actionStmt, 14, action.foregroundProtected ? 1 : 0);
        sqlite3_bind_int(actionStmt, 15, action.userAppTouched ? 1 : 0);
        sqlite3_bind_int(actionStmt, 16, action.reversible ? 1 : 0);
        ok = sqlite3_step(actionStmt) == SQLITE_DONE;
        sqlite3_finalize(actionStmt);
        if (!ok) return false;
    }

    const long long cutoff = autopilot.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
    sqlite3_stmt* retentionStmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM low_end_autopilot_samples WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
        ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
        sqlite3_finalize(retentionStmt);
    }
    if (sqlite3_prepare_v2(db_, "DELETE FROM autopilot_actions WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
        ok = sqlite3_step(retentionStmt) == SQLITE_DONE && ok;
        sqlite3_finalize(retentionStmt);
    }

    return ok;
}

bool MetricsStorage::LogBackgroundAgent(const BackgroundAgentResult& agent) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    const char* insertSql =
        "INSERT OR REPLACE INTO background_agent_samples("
        "time, enabled, tray_icon_ready, silent_monitoring, start_on_boot_configured, dashboard_visible, "
        "quick_restore_available, quick_restore_requested, mode, status, summary, quick_restore_status, control_center"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(agent.timestamp));
    sqlite3_bind_int(stmt, 2, agent.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 3, agent.trayIconReady ? 1 : 0);
    sqlite3_bind_int(stmt, 4, agent.silentMonitoring ? 1 : 0);
    sqlite3_bind_int(stmt, 5, agent.startOnBootConfigured ? 1 : 0);
    sqlite3_bind_int(stmt, 6, agent.dashboardVisible ? 1 : 0);
    sqlite3_bind_int(stmt, 7, agent.quickRestoreAvailable ? 1 : 0);
    sqlite3_bind_int(stmt, 8, agent.quickRestoreRequested ? 1 : 0);
    sqlite3_bind_text(stmt, 9, agent.mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, agent.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, agent.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, agent.quickRestoreStatus.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, agent.controlCenter.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok) {
        const long long cutoff = agent.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
        sqlite3_stmt* retentionStmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM background_agent_samples WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        }
    }

    return ok;
}

bool MetricsStorage::LogBenchmarkProof(const BenchmarkProofResult& proof) {
    lock_guard<mutex> lock(dbMutex_);
    if (!db_) return false;

    const char* insertSql =
        "INSERT OR REPLACE INTO benchmark_proof_samples("
        "time, status, mode, summary, before_cpu, before_memory, before_disk_free, before_risk, "
        "after_cpu_estimate, after_memory_estimate, after_disk_free_estimate, after_risk_estimate, "
        "recovered_ram_mb, cpu_drop_percent, disk_free_gain_percent, risk_drop_percent, actions_recommended, "
        "user_apps_touched, confidence, foreground_process"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(proof.timestamp));
    sqlite3_bind_text(stmt, 2, proof.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, proof.mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, proof.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, proof.beforeCpu);
    sqlite3_bind_double(stmt, 6, proof.beforeMemory);
    sqlite3_bind_double(stmt, 7, proof.beforeDiskFree);
    sqlite3_bind_double(stmt, 8, proof.beforeRisk);
    sqlite3_bind_double(stmt, 9, proof.afterCpuEstimate);
    sqlite3_bind_double(stmt, 10, proof.afterMemoryEstimate);
    sqlite3_bind_double(stmt, 11, proof.afterDiskFreeEstimate);
    sqlite3_bind_double(stmt, 12, proof.afterRiskEstimate);
    sqlite3_bind_double(stmt, 13, proof.recoveredRamMB);
    sqlite3_bind_double(stmt, 14, proof.cpuDropPercent);
    sqlite3_bind_double(stmt, 15, proof.diskFreeGainPercent);
    sqlite3_bind_double(stmt, 16, proof.riskDropPercent);
    sqlite3_bind_int(stmt, 17, proof.actionsRecommended);
    sqlite3_bind_int(stmt, 18, proof.userAppsTouched);
    sqlite3_bind_double(stmt, 19, proof.confidence);
    sqlite3_bind_text(stmt, 20, proof.foregroundProcess.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok) {
        const long long cutoff = proof.timestamp - PROCESS_SAMPLE_RETENTION_SECONDS;
        sqlite3_stmt* retentionStmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM benchmark_proof_samples WHERE time < ?1;", -1, &retentionStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(retentionStmt, 1, static_cast<sqlite3_int64>(cutoff));
            ok = sqlite3_step(retentionStmt) == SQLITE_DONE;
            sqlite3_finalize(retentionStmt);
        }
    }

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
