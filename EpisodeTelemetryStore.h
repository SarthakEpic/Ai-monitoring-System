#pragma once

#include <deque>
#include <string>

#include "PerformanceIntelligence.h"
#include "SystemMetrics.h"

struct sqlite3;

struct DeviceSupportDescriptor {
    std::string deviceId;
    std::string hardwareFingerprint;
    std::string windowsBuildFamily;
    std::string cpuCoreTier;
    std::string ramTier;
    std::string storageTier;
    std::string gpuTier;
    std::string powerMode;
};

struct EpisodeTelemetryInput {
    SystemSnapshot snapshot;
    QoeTelemetrySample qoe;
    WorkloadPhase workload = WorkloadPhase::Unknown;
    bool qoeCaptured = false;
    bool collectorReady = false;
    std::string runtimeMode = "MONITOR_ONLY";
    std::string featureSourceVersion = "v3";
};

// Persists only scheduled QoE samples in workload-bounded episodes. The class
// never records raw process paths, window titles, command lines, or telemetry uploads.
class EpisodeTelemetryStore {
public:
    EpisodeTelemetryStore() = default;
    ~EpisodeTelemetryStore();

    bool Open(const std::string& path, DeviceSupportDescriptor descriptor, std::string& error);
    void Close();
    bool Record(const EpisodeTelemetryInput& input, std::string& error);
    bool EnforceRetention(long long minimumTimestampMs, std::string& error);
    bool DeleteDeviceData(std::string& error);
    bool ExportTelemetryCsv(const std::string& path, std::string& error) const;

    [[nodiscard]] std::string DeviceId() const { return descriptor_.deviceId; }
    [[nodiscard]] std::string SessionId() const { return sessionId_; }
    [[nodiscard]] std::string ActiveEpisodeId() const { return activeEpisodeId_; }

    static std::string PseudonymizeLocalIdentifier(const std::string& value);

private:
    bool EnsureSession(long long timestampMs, const std::string& runtimeMode, std::string& error);
    bool StartEpisode(const EpisodeTelemetryInput& input, long long timestampMs, const std::string& reason, std::string& error);
    bool CloseActiveEpisode(long long timestampMs, const std::string& reason, std::string& error);
    bool WriteSummary(const EpisodeTelemetryInput& input, long long timestampMs, std::string& error);
    bool WriteCollectorHealth(const EpisodeTelemetryInput& input, long long timestampMs, std::string& error);
    bool Execute(const char* sql, std::string& error) const;
    struct RobustBaseline {
        double median = 0.0;
        double mad = 0.0;
        double robustZ = 0.0;
    };

    static std::string NewId(const char* prefix, long long timestampMs, unsigned long sequence);
    static RobustBaseline UpdateBaseline(std::deque<double>& values, double value);

    sqlite3* db_ = nullptr;
    DeviceSupportDescriptor descriptor_;
    std::string sessionId_;
    std::string activeEpisodeId_;
    WorkloadPhase activeWorkload_ = WorkloadPhase::Unknown;
    std::string activeForegroundBehavior_;
    std::deque<double> cpuHistory_;
    std::deque<double> memoryHistory_;
    std::deque<double> diskHistory_;
    unsigned long sequence_ = 0;
};
