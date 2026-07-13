#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "EpisodeTelemetryStore.h"
#include "sqlite3.h"

namespace {
int Count(sqlite3* database, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_ROW);
    const int value = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

EpisodeTelemetryInput Input(long long timestampMs, WorkloadPhase workload, bool captured = true) {
    EpisodeTelemetryInput input;
    input.snapshot.timestamp = timestampMs / 1000;
    input.snapshot.cpuUsage = 42.0;
    input.snapshot.memoryUsage = 53.0;
    input.snapshot.diskFree = 17.0;
    input.snapshot.netDownKBps = 4.0;
    input.snapshot.netUpKBps = 2.0;
    input.snapshot.processCount = 80;
    input.snapshot.intent.foregroundPid = 99;
    input.snapshot.intent.appKind = "BROWSER";
    input.qoe.timestampMs = timestampMs;
    input.qoe.collectionOverheadMs = 0.4;
    input.qoe.withinOverheadBudget = true;
    input.workload = workload;
    input.qoeCaptured = captured;
    input.collectorReady = true;
    return input;
}
}  // namespace

int main() {
    const std::filesystem::path databasePath = std::filesystem::temp_directory_path() / "aegis_episode_telemetry_test.db";
    const std::filesystem::path exportPath = std::filesystem::temp_directory_path() / "aegis_episode_telemetry_test.csv";
    std::filesystem::remove(databasePath);
    std::filesystem::remove(exportPath);

    DeviceSupportDescriptor descriptor;
    descriptor.deviceId = EpisodeTelemetryStore::PseudonymizeLocalIdentifier("test-device");
    descriptor.hardwareFingerprint = EpisodeTelemetryStore::PseudonymizeLocalIdentifier("test-hardware");
    descriptor.windowsBuildFamily = "Windows 11";
    descriptor.cpuCoreTier = "4_LOGICAL";
    descriptor.ramTier = "4GB";
    descriptor.storageTier = "UNKNOWN";
    descriptor.gpuTier = "UNKNOWN";
    descriptor.powerMode = "AC";

    EpisodeTelemetryStore store;
    std::string error;
    assert(store.Open(databasePath.string(), descriptor, error));
    assert(store.Record(Input(1000, WorkloadPhase::ActiveInteraction), error));
    assert(store.Record(Input(6000, WorkloadPhase::ActiveInteraction, false), error));
    assert(store.Record(Input(11000, WorkloadPhase::Compilation), error));
    assert(store.ExportTelemetryCsv(exportPath.string(), error));

    sqlite3* database = nullptr;
    assert(sqlite3_open(databasePath.string().c_str(), &database) == SQLITE_OK);
    assert(Count(database, "SELECT COUNT(*) FROM devices;") == 1);
    assert(Count(database, "SELECT COUNT(*) FROM sessions;") == 1);
    assert(Count(database, "SELECT COUNT(*) FROM workload_episodes;") == 2);
    assert(Count(database, "SELECT COUNT(*) FROM telemetry_summaries;") == 2);
    assert(Count(database, "SELECT COUNT(*) FROM collector_health_samples;") == 2);
    assert(Count(database, "SELECT COUNT(*) FROM telemetry_robust_baselines;") == 2);
    sqlite3_close(database);
    assert(std::filesystem::file_size(exportPath) > 100);

    assert(store.EnforceRetention(10000, error));
    assert(store.DeleteDeviceData(error));
    assert(sqlite3_open(databasePath.string().c_str(), &database) == SQLITE_OK);
    assert(Count(database, "SELECT COUNT(*) FROM devices;") == 0);
    assert(Count(database, "SELECT COUNT(*) FROM workload_episodes;") == 0);
    assert(Count(database, "SELECT COUNT(*) FROM telemetry_robust_baselines;") == 0);
    sqlite3_close(database);
    store.Close();
    std::filesystem::remove(databasePath);
    std::filesystem::remove(exportPath);
    return 0;
}
