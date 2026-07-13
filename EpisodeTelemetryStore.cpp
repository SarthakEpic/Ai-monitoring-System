#include "EpisodeTelemetryStore.h"

#include <bcrypt.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include "DatabaseMigrations.h"
#include "sqlite3.h"

namespace {
bool BindText(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string CollectorHealth(const EpisodeTelemetryInput& input) {
    if (!input.collectorReady) {
        return "UNAVAILABLE";
    }
    if (!input.qoe.withinOverheadBudget) {
        return "OVER_BUDGET";
    }
    return input.qoeCaptured ? "HEALTHY" : "SCHEDULED_REUSE";
}

std::string ForegroundBehavior(const SystemSnapshot& snapshot) {
    if (snapshot.intent.isFullscreen) {
        return "FULLSCREEN";
    }
    if (snapshot.intent.userState == "AWAY") {
        return "AWAY";
    }
    return snapshot.intent.foregroundPid == 0 ? "NO_FOREGROUND" : "INTERACTIVE";
}

bool Prepare(sqlite3* database, const char* sql, sqlite3_stmt*& statement, std::string& error) {
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK) {
        return true;
    }
    error = sqlite3_errmsg(database);
    return false;
}
}  // namespace

EpisodeTelemetryStore::~EpisodeTelemetryStore() {
    Close();
}

std::string EpisodeTelemetryStore::PseudonymizeLocalIdentifier(const std::string& value) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hashLength = 0;
    DWORD resultLength = 0;
    std::vector<unsigned char> result;
    const NTSTATUS opened = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    const bool initialized = opened >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0) >= 0;
    if (initialized) {
        result.resize(hashLength);
        const NTSTATUS created = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
        const NTSTATUS hashed = created >= 0
            ? BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0)
            : created;
        if (hashed >= 0 && BCryptFinishHash(hash, result.data(), hashLength, 0) >= 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            std::ostringstream output;
            output << "dev-";
            for (const unsigned char byte : result) {
                output << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            return output.str();
        }
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return "dev-unavailable";
}
std::string EpisodeTelemetryStore::NewId(const char* prefix, long long timestampMs, unsigned long sequence) {
    std::ostringstream output;
    output << prefix << '-' << timestampMs << '-' << sequence;
    return output.str();
}

EpisodeTelemetryStore::RobustBaseline EpisodeTelemetryStore::UpdateBaseline(std::deque<double>& values, double value) {
    constexpr size_t kMaximumSamples = 120;
    values.push_back(value);
    if (values.size() > kMaximumSamples) {
        values.pop_front();
    }

    std::vector<double> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const size_t middle = sorted.size() / 2;
    const double median = sorted.size() % 2 == 0 ? (sorted[middle - 1] + sorted[middle]) / 2.0 : sorted[middle];
    for (double& sample : sorted) {
        sample = std::abs(sample - median);
    }
    std::sort(sorted.begin(), sorted.end());
    const double mad = sorted.size() % 2 == 0 ? (sorted[middle - 1] + sorted[middle]) / 2.0 : sorted[middle];
    const double scale = std::max(0.01, mad * 1.4826);
    return {median, mad, (value - median) / scale};
}

bool EpisodeTelemetryStore::Execute(const char* sql, std::string& error) const {
    char* message = nullptr;
    const int result = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (result == SQLITE_OK) {
        return true;
    }
    error = message ? message : sqlite3_errmsg(db_);
    if (message) {
        sqlite3_free(message);
    }
    return false;
}

bool EpisodeTelemetryStore::Open(const std::string& path, DeviceSupportDescriptor descriptor, std::string& error) {
    Close();
    if (descriptor.deviceId.empty() || descriptor.hardwareFingerprint.empty()) {
        error = "Device support descriptor must be pseudonymous and non-empty.";
        return false;
    }

    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "SQLite open failed.";
        Close();
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    if (!Execute("PRAGMA foreign_keys = ON;", error)) {
        Close();
        return false;
    }
    if (!ApplyAegisMigrations(db_, error)) {
        Close();
        return false;
    }

    descriptor_ = std::move(descriptor);
    return true;
}

void EpisodeTelemetryStore::Close() {
    if (db_) {
        const long long nowMs = static_cast<long long>(std::time(nullptr)) * 1000;
        std::string ignoredError;
        CloseActiveEpisode(nowMs, "SESSION_ENDED", ignoredError);
        if (!sessionId_.empty()) {
            sqlite3_stmt* statement = nullptr;
            constexpr const char* kSql = "UPDATE sessions SET ended_at=?1 WHERE session_id=?2;";
            if (Prepare(db_, kSql, statement, ignoredError)) {
                sqlite3_bind_int64(statement, 1, nowMs);
                BindText(statement, 2, sessionId_);
                sqlite3_step(statement);
                sqlite3_finalize(statement);
            }
        }
        sqlite3_close(db_);
        db_ = nullptr;
    }
    sessionId_.clear();
    activeEpisodeId_.clear();
    activeForegroundBehavior_.clear();
    activeWorkload_ = WorkloadPhase::Unknown;
    cpuHistory_.clear();
    memoryHistory_.clear();
    diskHistory_.clear();
}
bool EpisodeTelemetryStore::EnsureSession(long long timestampMs, const std::string& runtimeMode, std::string& error) {
    if (!sessionId_.empty()) {
        return true;
    }
    sessionId_ = NewId("session", timestampMs, ++sequence_);

    sqlite3_stmt* statement = nullptr;
    constexpr const char* kDeviceSql =
        "INSERT INTO devices(device_id, hardware_fingerprint, platform, created_at, last_seen_at) "
        "VALUES(?1, ?2, ?3, ?4, ?4) "
        "ON CONFLICT(device_id) DO UPDATE SET last_seen_at = excluded.last_seen_at;";
    if (!Prepare(db_, kDeviceSql, statement, error)) {
        return false;
    }
    const bool deviceWritten =
        BindText(statement, 1, descriptor_.deviceId) &&
        BindText(statement, 2, descriptor_.hardwareFingerprint) &&
        BindText(statement, 3, descriptor_.windowsBuildFamily) &&
        sqlite3_bind_int64(statement, 4, timestampMs) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!deviceWritten) {
        error = sqlite3_errmsg(db_);
        return false;
    }

    constexpr const char* kDescriptorSql =
        "INSERT INTO device_support_descriptors(device_id, windows_build_family, cpu_core_tier, ram_tier, storage_tier, gpu_tier, power_mode, updated_at) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
        "ON CONFLICT(device_id) DO UPDATE SET windows_build_family=excluded.windows_build_family, cpu_core_tier=excluded.cpu_core_tier, "
        "ram_tier=excluded.ram_tier, storage_tier=excluded.storage_tier, gpu_tier=excluded.gpu_tier, power_mode=excluded.power_mode, updated_at=excluded.updated_at;";
    if (!Prepare(db_, kDescriptorSql, statement, error)) {
        return false;
    }
    const bool descriptorWritten =
        BindText(statement, 1, descriptor_.deviceId) && BindText(statement, 2, descriptor_.windowsBuildFamily) &&
        BindText(statement, 3, descriptor_.cpuCoreTier) && BindText(statement, 4, descriptor_.ramTier) &&
        BindText(statement, 5, descriptor_.storageTier) && BindText(statement, 6, descriptor_.gpuTier) &&
        BindText(statement, 7, descriptor_.powerMode) && sqlite3_bind_int64(statement, 8, timestampMs) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!descriptorWritten) {
        error = sqlite3_errmsg(db_);
        return false;
    }

    constexpr const char* kSessionSql =
        "INSERT INTO sessions(session_id, device_id, started_at, ended_at, app_version, runtime_mode) VALUES(?1, ?2, ?3, NULL, ?4, ?5);";
    if (!Prepare(db_, kSessionSql, statement, error)) {
        return false;
    }
    const bool sessionWritten = BindText(statement, 1, sessionId_) && BindText(statement, 2, descriptor_.deviceId) &&
        sqlite3_bind_int64(statement, 3, timestampMs) == SQLITE_OK && BindText(statement, 4, "aegis-phase1") &&
        BindText(statement, 5, runtimeMode) && sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!sessionWritten) {
        error = sqlite3_errmsg(db_);
    }
    return sessionWritten;
}

bool EpisodeTelemetryStore::CloseActiveEpisode(long long timestampMs, const std::string& reason, std::string& error) {
    if (activeEpisodeId_.empty()) {
        return true;
    }
    sqlite3_stmt* statement = nullptr;
    constexpr const char* kSql = "UPDATE workload_episodes SET ended_at=?1, end_reason=?2 WHERE episode_id=?3;";
    if (!Prepare(db_, kSql, statement, error)) {
        return false;
    }
    const bool updated = sqlite3_bind_int64(statement, 1, timestampMs) == SQLITE_OK && BindText(statement, 2, reason) &&
        BindText(statement, 3, activeEpisodeId_) && sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!updated) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    activeEpisodeId_.clear();
    return true;
}

bool EpisodeTelemetryStore::StartEpisode(
    const EpisodeTelemetryInput& input,
    long long timestampMs,
    const std::string& reason,
    std::string& error
) {
    activeEpisodeId_ = NewId("episode", timestampMs, ++sequence_);
    activeWorkload_ = input.workload;
    activeForegroundBehavior_ = ForegroundBehavior(input.snapshot);

    sqlite3_stmt* statement = nullptr;
    constexpr const char* kSql =
        "INSERT INTO workload_episodes(episode_id, device_id, session_id, workload_phase, application_family, foreground_behavior, started_at, ended_at, start_reason, end_reason, quality_status, provenance) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?7, ?8, 'OPEN', ?9, 'MEASURED_QOE');";
    if (!Prepare(db_, kSql, statement, error)) {
        return false;
    }
    const bool inserted =
        BindText(statement, 1, activeEpisodeId_) && BindText(statement, 2, descriptor_.deviceId) && BindText(statement, 3, sessionId_) &&
        BindText(statement, 4, ToString(input.workload)) && BindText(statement, 5, input.snapshot.intent.appKind) &&
        BindText(statement, 6, activeForegroundBehavior_) && sqlite3_bind_int64(statement, 7, timestampMs) == SQLITE_OK &&
        BindText(statement, 8, reason) && BindText(statement, 9, CollectorHealth(input)) && sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!inserted) {
        error = sqlite3_errmsg(db_);
        activeEpisodeId_.clear();
    }
    return inserted;
}

bool EpisodeTelemetryStore::WriteSummary(const EpisodeTelemetryInput& input, long long timestampMs, std::string& error) {
    if (!input.qoeCaptured) {
        return true;
    }
    const std::string summaryId = NewId("summary", timestampMs, ++sequence_);
    sqlite3_stmt* statement = nullptr;
    constexpr const char* kSummarySql =
        "INSERT INTO telemetry_summaries(summary_id, episode_id, observed_at, cpu_percent, memory_percent, disk_free_percent, network_down_kbps, network_up_kbps, process_count, collector_health, schema_version) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, 3);";
    if (!Prepare(db_, kSummarySql, statement, error)) {
        return false;
    }
    bool written = BindText(statement, 1, summaryId) && BindText(statement, 2, activeEpisodeId_) &&
        sqlite3_bind_int64(statement, 3, timestampMs) == SQLITE_OK && sqlite3_bind_double(statement, 4, input.snapshot.cpuUsage) == SQLITE_OK &&
        sqlite3_bind_double(statement, 5, input.snapshot.memoryUsage) == SQLITE_OK && sqlite3_bind_double(statement, 6, input.snapshot.diskFree) == SQLITE_OK &&
        sqlite3_bind_double(statement, 7, input.snapshot.netDownKBps) == SQLITE_OK && sqlite3_bind_double(statement, 8, input.snapshot.netUpKBps) == SQLITE_OK &&
        sqlite3_bind_int(statement, 9, input.snapshot.processCount) == SQLITE_OK && BindText(statement, 10, CollectorHealth(input)) && sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!written) {
        error = sqlite3_errmsg(db_);
        return false;
    }

    const RobustBaseline cpu = UpdateBaseline(cpuHistory_, input.snapshot.cpuUsage);
    const RobustBaseline memory = UpdateBaseline(memoryHistory_, input.snapshot.memoryUsage);
    const RobustBaseline disk = UpdateBaseline(diskHistory_, input.snapshot.diskFree);
    const std::string missingness = "input=" + std::to_string(input.qoe.inputAvailable ? 0 : 1) +
        ";page_reads=" + std::to_string(input.qoe.pageReadAvailable ? 0 : 1) +
        ";disk_queue=" + std::to_string(input.qoe.diskQueueAvailable ? 0 : 1) +
        ";foreground_progress=" + std::to_string(input.qoe.foregroundProgressAvailable ? 0 : 1) +
        ";frame_metrics=" + std::to_string(input.qoe.frameMetricsAvailable ? 0 : 1);
    constexpr const char* kBaselineSql =
        "INSERT INTO telemetry_robust_baselines(summary_id, cpu_median, cpu_mad, cpu_robust_z, memory_median, memory_mad, memory_robust_z, disk_median, disk_mad, disk_robust_z, sample_count, feature_source_version, missingness_mask) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13);";
    if (!Prepare(db_, kBaselineSql, statement, error)) {
        return false;
    }
    const int sampleCount = static_cast<int>(cpuHistory_.size());
    written = BindText(statement, 1, summaryId) && sqlite3_bind_double(statement, 2, cpu.median) == SQLITE_OK &&
        sqlite3_bind_double(statement, 3, cpu.mad) == SQLITE_OK && sqlite3_bind_double(statement, 4, cpu.robustZ) == SQLITE_OK &&
        sqlite3_bind_double(statement, 5, memory.median) == SQLITE_OK && sqlite3_bind_double(statement, 6, memory.mad) == SQLITE_OK &&
        sqlite3_bind_double(statement, 7, memory.robustZ) == SQLITE_OK && sqlite3_bind_double(statement, 8, disk.median) == SQLITE_OK &&
        sqlite3_bind_double(statement, 9, disk.mad) == SQLITE_OK && sqlite3_bind_double(statement, 10, disk.robustZ) == SQLITE_OK &&
        sqlite3_bind_int(statement, 11, sampleCount) == SQLITE_OK && BindText(statement, 12, input.featureSourceVersion) &&
        BindText(statement, 13, missingness) && sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!written) {
        error = sqlite3_errmsg(db_);
    }
    return written;
}
bool EpisodeTelemetryStore::WriteCollectorHealth(const EpisodeTelemetryInput& input, long long timestampMs, std::string& error) {
    sqlite3_stmt* statement = nullptr;
    constexpr const char* kSql =
        "INSERT INTO collector_health_samples(health_id, collector_name, observed_at, status, duration_ms, failure_reason, backoff_until) "
        "VALUES(?1, 'qoe', ?2, ?3, ?4, ?5, NULL);";
    if (!Prepare(db_, kSql, statement, error)) {
        return false;
    }
    const std::string status = CollectorHealth(input);
    const std::string failureReason = input.collectorReady ? "" : input.qoe.availability;
    const bool written = BindText(statement, 1, NewId("collector", timestampMs, ++sequence_)) && sqlite3_bind_int64(statement, 2, timestampMs) == SQLITE_OK &&
        BindText(statement, 3, status) && sqlite3_bind_double(statement, 4, input.qoe.collectionOverheadMs) == SQLITE_OK &&
        BindText(statement, 5, failureReason) && sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!written) {
        error = sqlite3_errmsg(db_);
    }
    return written;
}

bool EpisodeTelemetryStore::Record(const EpisodeTelemetryInput& input, std::string& error) {
    if (!db_) {
        error = "Episode telemetry store is not open.";
        return false;
    }
    const long long timestampMs = input.qoeCaptured && input.qoe.timestampMs > 0
        ? input.qoe.timestampMs
        : input.snapshot.timestamp * 1000;
    if (timestampMs <= 0) {
        error = "Episode telemetry requires a wall-clock timestamp.";
        return false;
    }
    if (!EnsureSession(timestampMs, input.runtimeMode, error)) {
        return false;
    }

    const std::string behavior = ForegroundBehavior(input.snapshot);
    const bool boundary = activeEpisodeId_.empty() || input.workload != activeWorkload_ || behavior != activeForegroundBehavior_;
    if (!Execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }
    bool written = true;
    if (boundary && !activeEpisodeId_.empty()) {
        written = CloseActiveEpisode(timestampMs, "WORKLOAD_OR_FOREGROUND_CHANGE", error);
    }
    if (written && boundary) {
        written = StartEpisode(input, timestampMs, activeEpisodeId_.empty() ? "INITIAL_OR_CHANGED_CONTEXT" : "CHANGED_CONTEXT", error);
    }
    if (written) {
        written = WriteSummary(input, timestampMs, error);
    }
    if (written && input.qoeCaptured) {
        written = WriteCollectorHealth(input, timestampMs, error);
    }
    if (written) {
        written = Execute("COMMIT;", error);
    }
    if (!written) {
        std::string rollbackError;
        Execute("ROLLBACK;", rollbackError);
    }
    return written;
}

bool EpisodeTelemetryStore::EnforceRetention(long long minimumTimestampMs, std::string& error) {
    if (!db_) {
        error = "Episode telemetry store is not open.";
        return false;
    }
    const std::string cutoff = std::to_string(minimumTimestampMs);
    const std::string sql =
        "BEGIN IMMEDIATE;"
        "DELETE FROM telemetry_robust_baselines WHERE summary_id IN (SELECT summary_id FROM telemetry_summaries WHERE observed_at < " + cutoff + ");"
        "DELETE FROM telemetry_summaries WHERE observed_at < " + cutoff + ";"
        "DELETE FROM qoe_outcomes WHERE observed_at < " + cutoff + ";"
        "DELETE FROM collector_health_samples WHERE observed_at < " + cutoff + ";"
        "DELETE FROM workload_episodes WHERE ended_at < " + cutoff + ";"
        "COMMIT;";
    return Execute(sql.c_str(), error);
}
bool EpisodeTelemetryStore::DeleteDeviceData(std::string& error) {
    if (!db_) {
        error = "Episode telemetry store is not open.";
        return false;
    }
    const std::string escapedId = descriptor_.deviceId;
    if (escapedId.find('\'') != std::string::npos) {
        error = "Pseudonymous device ID contains an unsupported quote.";
        return false;
    }
    const std::string episodes = "(SELECT episode_id FROM workload_episodes WHERE device_id='" + escapedId + "')";
    const std::string sql =
        "BEGIN IMMEDIATE;"
        "DELETE FROM telemetry_robust_baselines WHERE summary_id IN (SELECT summary_id FROM telemetry_summaries WHERE episode_id IN " + episodes + ");"
        "DELETE FROM telemetry_summaries WHERE episode_id IN " + episodes + ";"
        "DELETE FROM qoe_outcomes WHERE episode_id IN " + episodes + ";"
        "DELETE FROM label_provenance WHERE episode_id IN " + episodes + ";"
        "DELETE FROM action_outcomes_v3 WHERE episode_id IN " + episodes + ";"
        "DELETE FROM workload_episodes WHERE device_id='" + escapedId + "';"
        "DELETE FROM sessions WHERE device_id='" + escapedId + "';"
        "DELETE FROM device_support_descriptors WHERE device_id='" + escapedId + "';"
        "DELETE FROM devices WHERE device_id='" + escapedId + "';"
        "COMMIT;";
    return Execute(sql.c_str(), error);
}
bool EpisodeTelemetryStore::ExportTelemetryCsv(const std::string& path, std::string& error) const {
    if (!db_) {
        error = "Episode telemetry store is not open.";
        return false;
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "Unable to open telemetry export path.";
        return false;
    }
    output << "episode_id,observed_at,cpu_percent,memory_percent,disk_free_percent,network_down_kbps,network_up_kbps,process_count,collector_health\n";
    sqlite3_stmt* statement = nullptr;
    constexpr const char* kSql =
        "SELECT t.episode_id,t.observed_at,t.cpu_percent,t.memory_percent,t.disk_free_percent,t.network_down_kbps,t.network_up_kbps,t.process_count,t.collector_health "
        "FROM telemetry_summaries t JOIN workload_episodes e ON e.episode_id=t.episode_id WHERE e.device_id=?1 ORDER BY t.observed_at;";
    if (!Prepare(db_, kSql, statement, error)) {
        return false;
    }
    BindText(statement, 1, descriptor_.deviceId);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        for (int column = 0; column < 9; ++column) {
            if (column > 0) {
                output << ',';
            }
            const unsigned char* value = sqlite3_column_text(statement, column);
            output << (value ? reinterpret_cast<const char*>(value) : "");
        }
        output << '\n';
    }
    sqlite3_finalize(statement);
    if (!output) {
        error = "Unable to write telemetry export.";
        return false;
    }
    return true;
}
