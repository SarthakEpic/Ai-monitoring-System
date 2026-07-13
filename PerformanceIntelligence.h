#pragma once

#include <windows.h>
#include <pdh.h>

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "SystemMetrics.h"

struct sqlite3;

enum class WorkloadPhase {
    Unknown,
    AppLaunching,
    ActiveInteraction,
    PassivePlayback,
    Compilation,
    Gaming,
    VideoMeeting,
    LargeFileTransfer,
    UserIdle,
    BatterySaver,
    PostBootStabilization,
};

struct QoeTelemetrySample {
    long long timestampMs = 0;
    WorkloadPhase workload = WorkloadPhase::Unknown;
    DWORD foregroundPid = 0;
    std::string foregroundProcess = "N/A";
    double inputResponseMs = 0.0;
    double systemPageReadsPerSecond = 0.0;
    double diskQueueLength = 0.0;
    double contextSwitchesPerSecond = 0.0;
    double foregroundCpuProgressMs = 0.0;
    double foregroundPageFaultsPerSecond = 0.0;
    double frameRate = 0.0;
    double droppedFramesPerSecond = 0.0;
    double collectionOverheadMs = 0.0;
    int recommendedSampleIntervalMs = 5000;
    bool inputAvailable = false;
    bool pageReadAvailable = false;
    bool diskQueueAvailable = false;
    bool foregroundProgressAvailable = false;
    bool frameMetricsAvailable = false;
    bool withinOverheadBudget = true;
    std::string backend = "PDH_WINAPI";
    std::string availability;
};

struct CriticalityEdge {
    DWORD fromPid = 0;
    DWORD toPid = 0;
    std::string relationship;
    double weight = 0.0;
};

struct CriticalityNode {
    DWORD pid = 0;
    std::string processName;
    double criticality = 0.0;
    bool onForegroundPath = false;
    bool protectedFromIntervention = false;
    std::vector<std::string> reasons;
};

struct PerformanceCriticalityGraph {
    long long timestampMs = 0;
    DWORD foregroundPid = 0;
    WorkloadPhase workload = WorkloadPhase::Unknown;
    std::vector<CriticalityNode> nodes;
    std::vector<CriticalityEdge> edges;
};

class WorkloadPhaseDetector {
public:
    using UptimeProvider = std::function<unsigned long long()>;
    using PowerStatusProvider = std::function<bool(SYSTEM_POWER_STATUS&)>;

    explicit WorkloadPhaseDetector(
        UptimeProvider uptimeProvider = [] { return GetTickCount64(); },
        PowerStatusProvider powerStatusProvider = [](SYSTEM_POWER_STATUS& status) { return GetSystemPowerStatus(&status) == TRUE; }
    );
    WorkloadPhase Detect(const SystemSnapshot& snapshot, const QoeTelemetrySample* telemetry = nullptr) const;

private:
    UptimeProvider uptimeProvider_;
    PowerStatusProvider powerStatusProvider_;
};

class QoeTelemetryCollector {
public:
    QoeTelemetryCollector() = default;
    ~QoeTelemetryCollector();

    bool Initialize(std::string& error);
    QoeTelemetrySample Capture(const SystemSnapshot& snapshot, WorkloadPhase workload);

private:
    static double ReadCounter(PDH_HCOUNTER counter, bool& available);
    static bool ReadForegroundProcess(DWORD pid, unsigned long long& cpu100ns, unsigned long& faults);
    static double MeasureInputResponse(DWORD foregroundPid, bool& available);
    void ReadFrameMetrics(QoeTelemetrySample& sample, double intervalSeconds);

    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER pageReadsCounter_ = nullptr;
    PDH_HCOUNTER diskQueueCounter_ = nullptr;
    PDH_HCOUNTER contextSwitchCounter_ = nullptr;
    long long previousSampleMs_ = 0;
    unsigned long long previousForegroundCpu100ns_ = 0;
    unsigned long previousForegroundFaults_ = 0;
    DWORD previousForegroundPid_ = 0;
    unsigned long long previousFramesDisplayed_ = 0;
    unsigned long long previousFramesDropped_ = 0;
};

class PerformanceCriticalityEngine {
public:
    PerformanceCriticalityGraph Build(const SystemSnapshot& snapshot, WorkloadPhase workload) const;
};

class QoeTelemetryJournal {
public:
    QoeTelemetryJournal() = default;
    ~QoeTelemetryJournal();

    bool Open(const std::string& path, std::string& error);
    void Close();
    bool Save(const QoeTelemetrySample& sample, const PerformanceCriticalityGraph& graph, std::string& error);
    bool EnforceRetention(int maximumQoeRows, int maximumGraphRows, std::string& error);

private:
    bool EnsureSchema(std::string& error);

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

const char* ToString(WorkloadPhase phase);
