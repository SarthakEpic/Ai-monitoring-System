#include "PerformanceIntelligence.h"

#include <dwmapi.h>
#include <psapi.h>
#include <pdhmsg.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <unordered_set>

#include "sqlite3.h"

using namespace std;

namespace {

long long NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

string Lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(tolower(c));
    });
    return value;
}

bool Contains(const string& value, const string& token) {
    return Lower(value).find(Lower(token)) != string::npos;
}

unsigned long long FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

void AddReason(CriticalityNode& node, const string& reason, double score) {
    node.reasons.push_back(reason);
    node.criticality = max(node.criticality, score);
}

bool IsAudioVideoProcess(const string& name) {
    const string lowered = Lower(name);
    return lowered == "audiodg.exe" || Contains(lowered, "zoom") || Contains(lowered, "teams") ||
           Contains(lowered, "discord") || Contains(lowered, "obs") || Contains(lowered, "vlc");
}

bool IsCompilerProcess(const string& name) {
    const string lowered = Lower(name);
    return lowered == "cl.exe" || lowered == "link.exe" || lowered == "msbuild.exe" ||
           Contains(lowered, "clang") || Contains(lowered, "gcc") || Contains(lowered, "ninja");
}

void BindText(sqlite3_stmt* statement, int index, const string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace

const char* ToString(WorkloadPhase phase) {
    switch (phase) {
    case WorkloadPhase::AppLaunching: return "APP_LAUNCHING";
    case WorkloadPhase::ActiveInteraction: return "ACTIVE_INTERACTION";
    case WorkloadPhase::PassivePlayback: return "PASSIVE_PLAYBACK";
    case WorkloadPhase::Compilation: return "COMPILATION";
    case WorkloadPhase::Gaming: return "GAMING";
    case WorkloadPhase::VideoMeeting: return "VIDEO_MEETING";
    case WorkloadPhase::LargeFileTransfer: return "LARGE_FILE_TRANSFER";
    case WorkloadPhase::UserIdle: return "USER_IDLE";
    case WorkloadPhase::BatterySaver: return "BATTERY_SAVER";
    case WorkloadPhase::PostBootStabilization: return "POST_BOOT_STABILIZATION";
    default: return "UNKNOWN";
    }
}

WorkloadPhase WorkloadPhaseDetector::Detect(const SystemSnapshot& snapshot, const QoeTelemetrySample* telemetry) const {
    if (GetTickCount64() < 180000) return WorkloadPhase::PostBootStabilization;
    if (snapshot.intent.userState == "AWAY" || snapshot.intent.idleSeconds >= 300.0) return WorkloadPhase::UserIdle;

    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && power.ACLineStatus == 0 && power.BatteryLifePercent <= 25) {
        return WorkloadPhase::BatterySaver;
    }
    if (snapshot.intent.appKind == "COMMUNICATION") return WorkloadPhase::VideoMeeting;
    if (snapshot.intent.appKind == "GAME" || (snapshot.intent.isFullscreen && snapshot.intent.appKind != "MEDIA")) {
        return WorkloadPhase::Gaming;
    }
    if (snapshot.intent.appKind == "MEDIA" ||
        (snapshot.intent.appKind == "BROWSER" && snapshot.intent.isFullscreen && snapshot.intent.idleSeconds >= 5.0)) {
        return WorkloadPhase::PassivePlayback;
    }
    for (const ProcessSnapshot& process : snapshot.processGenome) {
        if (IsCompilerProcess(process.name) && process.cpuPercent >= 5.0) return WorkloadPhase::Compilation;
    }
    if (snapshot.netDownKBps + snapshot.netUpKBps >= 2048.0 ||
        snapshot.topProcess.ioReadKBps + snapshot.topProcess.ioWriteKBps >= 4096.0) {
        return WorkloadPhase::LargeFileTransfer;
    }
    if (snapshot.intent.foregroundPid != 0 && snapshot.intent.focusDurationSeconds <= 8.0) return WorkloadPhase::AppLaunching;
    if (snapshot.intent.userState == "ACTIVE") return WorkloadPhase::ActiveInteraction;
    if (telemetry && telemetry->inputAvailable && telemetry->inputResponseMs > 50.0) return WorkloadPhase::ActiveInteraction;
    return WorkloadPhase::Unknown;
}

QoeTelemetryCollector::~QoeTelemetryCollector() {
    if (query_) PdhCloseQuery(query_);
}

bool QoeTelemetryCollector::Initialize(string& error) {
    if (query_) return true;
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
        error = "PdhOpenQuery failed";
        query_ = nullptr;
        return false;
    }
    const PDH_STATUS pageStatus = PdhAddEnglishCounterW(query_, L"\\Memory\\Page Reads/sec", 0, &pageReadsCounter_);
    const PDH_STATUS diskStatus = PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(_Total)\\Current Disk Queue Length", 0, &diskQueueCounter_);
    const PDH_STATUS switchStatus = PdhAddEnglishCounterW(query_, L"\\System\\Context Switches/sec", 0, &contextSwitchCounter_);
    if (pageStatus != ERROR_SUCCESS && diskStatus != ERROR_SUCCESS && switchStatus != ERROR_SUCCESS) {
        error = "required PDH performance counters are unavailable";
        PdhCloseQuery(query_);
        query_ = nullptr;
        return false;
    }
    PdhCollectQueryData(query_);
    return true;
}

double QoeTelemetryCollector::ReadCounter(PDH_HCOUNTER counter, bool& available) {
    available = false;
    if (!counter) return 0.0;
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS ||
        value.CStatus != PDH_CSTATUS_VALID_DATA) {
        return 0.0;
    }
    available = isfinite(value.doubleValue);
    return available ? max(0.0, value.doubleValue) : 0.0;
}

bool QoeTelemetryCollector::ReadForegroundProcess(DWORD pid, unsigned long long& cpu100ns, unsigned long& faults) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    const bool ok = GetProcessTimes(process, &created, &exited, &kernel, &user) &&
                    GetProcessMemoryInfo(process, &memory, sizeof(memory));
    if (ok) {
        cpu100ns = FileTimeValue(kernel) + FileTimeValue(user);
        faults = memory.PageFaultCount;
    }
    CloseHandle(process);
    return ok;
}

double QoeTelemetryCollector::MeasureInputResponse(DWORD foregroundPid, bool& available) {
    available = false;
    HWND window = GetForegroundWindow();
    DWORD windowPid = 0;
    if (!window || !GetWindowThreadProcessId(window, &windowPid) || windowPid != foregroundPid) return 0.0;
    LARGE_INTEGER frequency{}, start{}, finish{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    DWORD_PTR ignored = 0;
    const LRESULT response = SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &ignored);
    QueryPerformanceCounter(&finish);
    available = response != 0;
    if (!available || frequency.QuadPart == 0) return 100.0;
    return static_cast<double>(finish.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

void QoeTelemetryCollector::ReadFrameMetrics(QoeTelemetrySample& sample, double intervalSeconds) {
    DWM_TIMING_INFO timing{};
    timing.cbSize = sizeof(timing);
    if (DwmGetCompositionTimingInfo(nullptr, &timing) != S_OK || intervalSeconds <= 0.0) return;
    const unsigned long long displayed = timing.cFramesDisplayed;
    const unsigned long long dropped = timing.cFramesDropped;
    if (previousFramesDisplayed_ > 0 && displayed >= previousFramesDisplayed_) {
        sample.frameRate = static_cast<double>(displayed - previousFramesDisplayed_) / intervalSeconds;
        sample.droppedFramesPerSecond = dropped >= previousFramesDropped_
            ? static_cast<double>(dropped - previousFramesDropped_) / intervalSeconds
            : 0.0;
        sample.frameMetricsAvailable = true;
    }
    previousFramesDisplayed_ = displayed;
    previousFramesDropped_ = dropped;
}

QoeTelemetrySample QoeTelemetryCollector::Capture(const SystemSnapshot& snapshot, WorkloadPhase workload) {
    const auto overheadStart = chrono::steady_clock::now();
    QoeTelemetrySample sample;
    sample.timestampMs = NowMs();
    sample.workload = workload;
    sample.foregroundPid = snapshot.intent.foregroundPid;
    sample.foregroundProcess = snapshot.intent.foregroundProcess;
    const double intervalSeconds = previousSampleMs_ > 0
        ? max(0.001, static_cast<double>(sample.timestampMs - previousSampleMs_) / 1000.0)
        : 0.0;

    if (query_) {
        PdhCollectQueryData(query_);
        sample.systemPageReadsPerSecond = ReadCounter(pageReadsCounter_, sample.pageReadAvailable);
        sample.diskQueueLength = ReadCounter(diskQueueCounter_, sample.diskQueueAvailable);
        bool contextAvailable = false;
        sample.contextSwitchesPerSecond = ReadCounter(contextSwitchCounter_, contextAvailable);
    }
    sample.inputResponseMs = MeasureInputResponse(sample.foregroundPid, sample.inputAvailable);

    unsigned long long cpu100ns = 0;
    unsigned long faults = 0;
    if (sample.foregroundPid != 0 && ReadForegroundProcess(sample.foregroundPid, cpu100ns, faults)) {
        if (previousForegroundPid_ == sample.foregroundPid && previousSampleMs_ > 0) {
            sample.foregroundCpuProgressMs = static_cast<double>(cpu100ns - previousForegroundCpu100ns_) / 10000.0;
            sample.foregroundPageFaultsPerSecond = faults >= previousForegroundFaults_
                ? static_cast<double>(faults - previousForegroundFaults_) / intervalSeconds
                : 0.0;
            sample.foregroundProgressAvailable = true;
        }
        previousForegroundCpu100ns_ = cpu100ns;
        previousForegroundFaults_ = faults;
        previousForegroundPid_ = sample.foregroundPid;
    }
    ReadFrameMetrics(sample, intervalSeconds);

    const bool pressure = snapshot.memoryUsage >= 80.0 || snapshot.cpuUsage >= 75.0 ||
                          sample.diskQueueLength >= 2.0 || sample.inputResponseMs >= 35.0;
    sample.recommendedSampleIntervalMs = pressure ? 1000 : 5000;
    sample.availability = string("input=") + (sample.inputAvailable ? "1" : "0") +
                          ",page_reads=" + (sample.pageReadAvailable ? "1" : "0") +
                          ",disk_queue=" + (sample.diskQueueAvailable ? "1" : "0") +
                          ",foreground_progress=" + (sample.foregroundProgressAvailable ? "1" : "0") +
                          ",frames=" + (sample.frameMetricsAvailable ? "1" : "0");
    sample.collectionOverheadMs = chrono::duration<double, milli>(chrono::steady_clock::now() - overheadStart).count();
    sample.withinOverheadBudget = sample.collectionOverheadMs <= 10.0;
    previousSampleMs_ = sample.timestampMs;
    return sample;
}

PerformanceCriticalityGraph PerformanceCriticalityEngine::Build(const SystemSnapshot& snapshot, WorkloadPhase workload) const {
    PerformanceCriticalityGraph graph;
    graph.timestampMs = NowMs();
    graph.foregroundPid = snapshot.intent.foregroundPid;
    graph.workload = workload;
    unordered_map<DWORD, const ProcessSnapshot*> byPid;
    for (const ProcessSnapshot& process : snapshot.processGenome) byPid[process.pid] = &process;

    unordered_set<DWORD> foregroundFamily;
    if (graph.foregroundPid != 0) foregroundFamily.insert(graph.foregroundPid);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const ProcessSnapshot& process : snapshot.processGenome) {
            if (foregroundFamily.contains(process.parentPid) && foregroundFamily.insert(process.pid).second) changed = true;
        }
    }

    for (const ProcessSnapshot& process : snapshot.processGenome) {
        CriticalityNode node;
        node.pid = process.pid;
        node.processName = process.name;
        if (process.pid == graph.foregroundPid) AddReason(node, "active foreground process", 100.0);
        if (foregroundFamily.contains(process.pid) && process.pid != graph.foregroundPid) AddReason(node, "foreground process-family dependency", 92.0);
        if (process.parentPid != 0 && byPid.contains(process.parentPid)) {
            graph.edges.push_back({process.parentPid, process.pid, "PARENT_CHILD", foregroundFamily.contains(process.parentPid) ? 1.0 : 0.45});
        }
        if (process.matchesUserIntent) AddReason(node, "matches current user intent", 95.0);
        if (process.isRecentlyActive) AddReason(node, "recently active application", 82.0);
        if (process.hasVisibleWindow) AddReason(node, "visible user-facing window", 72.0);
        if (process.isSystemCritical || process.safety == "PROTECTED") AddReason(node, "deterministic protected target", 100.0);
        if (IsAudioVideoProcess(process.name) &&
            (workload == WorkloadPhase::VideoMeeting || workload == WorkloadPhase::PassivePlayback || workload == WorkloadPhase::Gaming)) {
            AddReason(node, "audio/video deadline dependency", 96.0);
        }
        if (IsCompilerProcess(process.name) && workload == WorkloadPhase::Compilation) {
            AddReason(node, "active compilation throughput dependency", 88.0);
        }
        if (process.ioReadKBps + process.ioWriteKBps > 1024.0 &&
            (workload == WorkloadPhase::AppLaunching || workload == WorkloadPhase::LargeFileTransfer)) {
            AddReason(node, "current workload I/O dependency", 75.0);
        }
        if (node.reasons.empty()) AddReason(node, "no observed foreground dependency", 10.0);
        node.onForegroundPath = foregroundFamily.contains(process.pid) || process.matchesUserIntent;
        node.protectedFromIntervention = node.criticality >= 70.0 || process.isSystemCritical;
        graph.nodes.push_back(move(node));
    }
    return graph;
}

QoeTelemetryJournal::~QoeTelemetryJournal() { Close(); }

bool QoeTelemetryJournal::Open(const string& path, string& error) {
    lock_guard lock(mutex_);
    if (db_) return true;
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "sqlite open failed";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    return EnsureSchema(error);
}

void QoeTelemetryJournal::Close() {
    lock_guard lock(mutex_);
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

bool QoeTelemetryJournal::EnsureSchema(string& error) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS qoe_samples (timestamp_ms INTEGER PRIMARY KEY, workload TEXT NOT NULL, foreground_pid INTEGER NOT NULL, "
        "foreground_process TEXT NOT NULL, input_response_ms REAL NOT NULL, page_reads_per_sec REAL NOT NULL, disk_queue REAL NOT NULL, "
        "context_switches_per_sec REAL NOT NULL, foreground_cpu_progress_ms REAL NOT NULL, foreground_faults_per_sec REAL NOT NULL, "
        "frame_rate REAL NOT NULL, dropped_frames_per_sec REAL NOT NULL, overhead_ms REAL NOT NULL, sample_interval_ms INTEGER NOT NULL, "
        "within_budget INTEGER NOT NULL, backend TEXT NOT NULL, availability TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS criticality_nodes (timestamp_ms INTEGER NOT NULL, foreground_pid INTEGER NOT NULL, workload TEXT NOT NULL, "
        "pid INTEGER NOT NULL, process_name TEXT NOT NULL, criticality REAL NOT NULL, foreground_path INTEGER NOT NULL, protected INTEGER NOT NULL, reasons TEXT NOT NULL, "
        "PRIMARY KEY(timestamp_ms,pid));";
    char* message = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        error = message ? message : sqlite3_errmsg(db_);
        if (message) sqlite3_free(message);
        return false;
    }
    return true;
}

bool QoeTelemetryJournal::Save(
    const QoeTelemetrySample& sample,
    const PerformanceCriticalityGraph& graph,
    string& error
) {
    lock_guard lock(mutex_);
    if (!db_) {
        error = "QoE journal is not open";
        return false;
    }
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db_);
        return false;
    }

    const char* sampleSql =
        "INSERT OR REPLACE INTO qoe_samples VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* statement = nullptr;
    bool ok = sqlite3_prepare_v2(db_, sampleSql, -1, &statement, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(statement, 1, sample.timestampMs);
        BindText(statement, 2, ToString(sample.workload));
        sqlite3_bind_int64(statement, 3, sample.foregroundPid);
        BindText(statement, 4, sample.foregroundProcess);
        sqlite3_bind_double(statement, 5, sample.inputResponseMs);
        sqlite3_bind_double(statement, 6, sample.systemPageReadsPerSecond);
        sqlite3_bind_double(statement, 7, sample.diskQueueLength);
        sqlite3_bind_double(statement, 8, sample.contextSwitchesPerSecond);
        sqlite3_bind_double(statement, 9, sample.foregroundCpuProgressMs);
        sqlite3_bind_double(statement, 10, sample.foregroundPageFaultsPerSecond);
        sqlite3_bind_double(statement, 11, sample.frameRate);
        sqlite3_bind_double(statement, 12, sample.droppedFramesPerSecond);
        sqlite3_bind_double(statement, 13, sample.collectionOverheadMs);
        sqlite3_bind_int(statement, 14, sample.recommendedSampleIntervalMs);
        sqlite3_bind_int(statement, 15, sample.withinOverheadBudget ? 1 : 0);
        BindText(statement, 16, sample.backend);
        BindText(statement, 17, sample.availability);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    if (statement) {
        sqlite3_finalize(statement);
        statement = nullptr;
    }

    const char* nodeSql =
        "INSERT OR REPLACE INTO criticality_nodes VALUES(?,?,?,?,?,?,?,?,?);";
    for (const CriticalityNode& node : graph.nodes) {
        if (!ok) {
            break;
        }
        if (sqlite3_prepare_v2(db_, nodeSql, -1, &statement, nullptr) != SQLITE_OK) {
            ok = false;
            break;
        }

        ostringstream reasons;
        for (size_t index = 0; index < node.reasons.size(); ++index) {
            if (index > 0) {
                reasons << "; ";
            }
            reasons << node.reasons[index];
        }

        sqlite3_bind_int64(statement, 1, graph.timestampMs);
        sqlite3_bind_int64(statement, 2, graph.foregroundPid);
        BindText(statement, 3, ToString(graph.workload));
        sqlite3_bind_int64(statement, 4, node.pid);
        BindText(statement, 5, node.processName);
        sqlite3_bind_double(statement, 6, node.criticality);
        sqlite3_bind_int(statement, 7, node.onForegroundPath ? 1 : 0);
        sqlite3_bind_int(statement, 8, node.protectedFromIntervention ? 1 : 0);
        BindText(statement, 9, reasons.str());

        ok = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    if (statement) {
        sqlite3_finalize(statement);
    }

    if (ok) {
        ok = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    if (!ok) {
        error = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return ok;
}

bool QoeTelemetryJournal::EnforceRetention(
    int maximumQoeRows,
    int maximumGraphRows,
    string& error
) {
    lock_guard lock(mutex_);
    if (!db_) {
        error = "QoE journal is not open";
        return false;
    }

    const int qoeRows = max(100, maximumQoeRows);
    const int graphRows = max(1000, maximumGraphRows);
    const string sql =
        "DELETE FROM qoe_samples WHERE timestamp_ms NOT IN ("
        "SELECT timestamp_ms FROM qoe_samples ORDER BY timestamp_ms DESC LIMIT " +
        to_string(qoeRows) + ");" +
        "DELETE FROM criticality_nodes WHERE rowid NOT IN ("
        "SELECT rowid FROM criticality_nodes ORDER BY timestamp_ms DESC LIMIT " +
        to_string(graphRows) + ");";

    char* message = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        error = message ? message : sqlite3_errmsg(db_);
        if (message) {
            sqlite3_free(message);
        }
        return false;
    }
    return true;
}