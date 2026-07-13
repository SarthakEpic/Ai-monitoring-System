#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cctype>

#include "AdaptiveBaseline.h"
#include "AdaptiveSampling.h"
#include "AutoHealPlanner.h"
#include "BackgroundAgent.h"
#include "BrowserIntegrationBridge.h"
#include "BenchmarkProof.h"
#include "CooperativeIntegrations.h"
#include "AppConfig.h"
#include "DecisionEngine.h"
#include "EpisodeTelemetryStore.h"
#include "HealingVerifier.h"
#include "ImpactLearning.h"
#include "InferenceProtocol.h"
#include "LowEndAutopilot.h"
#include "MetricsPipeline.h"
#include "MonitoringScheduler.h"
#include "PerformanceIntelligence.h"
#include "RuntimeFoundation.h"
#include "RuntimeOrchestrator.h"
#include "RuntimeStateStore.h"
#include "ThreadedRuntimeComponent.h"
#include "RuntimeHealth.h"
#include "RollingFeatureCache.h"
#include "SafeOnlinePolicy.h"
#include "SafetyPolicy.h"
#include "MetricsStorage.h"
#include "ModernDashboardUI.h"
#include "ObserverEffectGovernor.h"
#include "SystemMetrics.h"

using namespace std;
using namespace std::chrono;
namespace fs = std::filesystem;

constexpr size_t HISTORY_SIZE = 30;
constexpr size_t AI_WINDOW = 8;
constexpr int DEFAULT_AI_PREDICT_INTERVAL_TICKS = 10;
constexpr double DEFAULT_AI_ALERT_THRESHOLD = 65.0;
constexpr double DEFAULT_AI_ALERT_CLEAR_THRESHOLD = 55.0;
constexpr int DEFAULT_AI_ALERT_TRIGGER_STREAK = 3;
constexpr int DEFAULT_AI_ALERT_CLEAR_STREAK = 3;
constexpr int AI_MODEL_RETRY_COUNT = 1;
constexpr int AI_MODEL_RETRY_DELAY_MS = 60;
constexpr int DEFAULT_AI_MODEL_TIMEOUT_MS = 8000;
constexpr int DEFAULT_AI_MODEL_CACHE_TTL_TICKS = 30;
constexpr UINT WM_TRAYICON_MESSAGE = WM_APP + 42;
constexpr UINT ID_TRAY_OPEN_DASHBOARD = 5001;
constexpr UINT ID_TRAY_QUICK_RESTORE = 5002;
constexpr UINT ID_TRAY_EXIT = 5003;


double g_cpu = 0.0;
double g_mem = 0.0;
double g_totalMemoryMB = 0.0;
double g_availableMemoryMB = 0.0;
double g_disk = 0.0;
double g_aiProb = 0.0;
double g_aiConfidence = 0.0;
double g_riskScore = 0.0;
double g_anomalyScore = 0.0;
double g_pressureScore = 0.0;
double g_netDown = 0.0;
double g_netUp = 0.0;
double g_topProcessCpu = 0.0;
double g_topProcessMem = 0.0;
double g_topProcessPrivateMem = 0.0;
double g_topProcessWaste = 0.0;
double g_topProcessExpectedGain = 0.0;
int g_processCount = 0;
unsigned long g_topProcessPid = 0;

bool g_alert = false;
string g_aiSource = "WARMING UP";
string g_aiClass = "UNKNOWN";
string g_aiReason = "Collecting baseline";
string g_rootCause = "unknown";
string g_modelReadiness = "unknown";
string g_modelGeneratedAt = "unknown";
string g_recommendedAction = "monitor_only";
string g_scenarioLabel = "auto";
string g_topProcessName = "N/A";
string g_topProcessCategory = "UNKNOWN";
string g_topProcessSafety = "UNKNOWN";
string g_topProcessRecommendation = "observe";
string g_topProcessReason = "insufficient process context";
string g_userState = "UNKNOWN";
string g_foregroundProcess = "N/A";
string g_foregroundAppKind = "UNKNOWN";
string g_intentReason = "No foreground intent captured";
double g_userIdleSeconds = 0.0;
double g_focusDurationSeconds = 0.0;
bool g_foregroundFullscreen = false;
string g_decisionSummary = "System stable";
string g_decisionReason = "Collecting baseline";
string g_decisionRootCauseDetail = "No dominant pressure source";
string g_actionTarget = "system";
string g_safetyGate = "OBSERVE_ONLY";
string g_blockedReason = "Auto-heal execution disabled";
double g_actionConfidence = 0.0;
double g_expectedOptimizationGain = 0.0;
int g_cooldownRemainingSeconds = 0;
int g_candidateCount = 0;
bool g_dryRun = true;
RuntimeMode g_runtimeMode = RuntimeMode::MonitorOnly;
RiskLevel g_decisionLevel = RiskLevel::Normal;
bool g_safeToHeal = false;
int g_modelFeatureCount = 0;

deque<double> g_cpuHist;
deque<double> g_memHist;
deque<double> g_diskHist;
deque<double> g_netHist;
deque<double> g_processHist;
deque<double> g_topCpuHist;
deque<double> g_topMemHist;
deque<double> g_inputLatencyHist;
vector<ProcessSnapshot> g_processGenome;
QoeTelemetrySample g_qoeSample;
PerformanceCriticalityGraph g_criticalityGraph;
ShadowPolicyDecision g_shadowPolicyDecision;
OnlinePolicyDecision g_onlinePolicyDecision;

mutex g_dataMutex;
mutex g_logMutex;

HFONT gFontTitle = NULL;
HFONT gFontSection = NULL;
HFONT gFontValue = NULL;
HFONT gFontSmall = NULL;

AppConfig g_config;
MetricsStorage g_storage;
MetricsPipeline g_pipeline;
DecisionEngine g_decisionEngine;
AutoHealPlanner g_autoHealPlanner;
AdaptiveBaselineEngine g_adaptiveBaseline;
LowEndAutopilotEngine g_lowEndAutopilot;
BackgroundAgentEngine g_backgroundAgent;
BenchmarkProofEngine g_benchmarkProof;
HealingVerifier g_healingVerifier;
SafetyPolicyEngine g_safetyPolicyEngine;
RuntimeHealthMonitor g_runtimeHealth;
HealPlan g_healPlan;
HealVerification g_healVerification;
SafetyPolicyResult g_safetyPolicyResult;
RuntimeHealthSample g_runtimeHealthSample;
AdaptiveBaselineResult g_adaptiveBaselineResult;
LowEndAutopilotResult g_autopilotResult;
BackgroundAgentResult g_backgroundAgentResult;
BenchmarkProofResult g_benchmarkProofResult;
atomic<bool> g_running = true;
thread g_monitorThread;
RuntimeStateStore g_runtimeStateStore;
unique_ptr<RuntimeOrchestrator> g_runtimeOrchestrator;
long long g_tick = 0;
DashboardView g_dashboardView = DashboardView::Overview;
PROCESS_INFORMATION g_inferenceServiceProcess{};
bool g_inferenceServiceActive = false;
NOTIFYICONDATAW g_trayIcon{};
bool g_trayIconInstalled = false;
atomic<int> g_quickRestoreRequests = 0;
string g_quickRestoreStatus = "IDLE";
bool g_agentStartMinimized = false;
UINT g_taskbarCreatedMessage = 0;

double ClampDouble(double value, double lo, double hi) {
    return max(lo, min(hi, value));
}

string ShortenText(const string& text, size_t maxLen) {
    if (text.size() <= maxLen) return text;
    if (maxLen <= 3) return text.substr(0, maxLen);
    return text.substr(0, maxLen - 3) + "...";
}

string ToDisplayToken(string text) {
    replace(text.begin(), text.end(), '_', ' ');
    return text;
}

string ToUpperAscii(string text) {
    transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(toupper(ch));
    });
    return text;
}

vector<string> SplitCsvValues(const string& csv) {
    vector<string> values;
    string token;
    istringstream stream(csv);
    while (getline(stream, token, ',')) {
        const size_t start = token.find_first_not_of(" \t\r\n");
        const size_t end = token.find_last_not_of(" \t\r\n");
        if (start != string::npos) values.push_back(token.substr(start, end - start + 1));
    }
    return values;
}

bool ContainsProcessNameInsensitive(const vector<string>& names, const string& processName) {
    const string normalized = ToUpperAscii(fs::path(processName).filename().string());
    for (const string& name : names) {
        if (ToUpperAscii(fs::path(name).filename().string()) == normalized) return true;
    }
    return false;
}
string FormatModeLabel(const string& mode) {
    if (mode == "HIGH_PERFORMANCE") return "HIGH PERFORMANCE";
    if (mode == "BALANCED") return "BALANCED";
    return "LOW END";
}

int ResolveModeDefaultPredictInterval(const string& mode) {
    if (mode == "HIGH_PERFORMANCE") return 3;
    if (mode == "BALANCED") return 6;
    return DEFAULT_AI_PREDICT_INTERVAL_TICKS;
}

int ResolveModeDefaultTimeoutMs(const string& mode) {
    if (mode == "HIGH_PERFORMANCE") return 5000;
    if (mode == "BALANCED") return 6500;
    return DEFAULT_AI_MODEL_TIMEOUT_MS;
}

int ResolveModeDefaultCacheTtlTicks(const string& mode) {
    if (mode == "HIGH_PERFORMANCE") return 12;
    if (mode == "BALANCED") return 20;
    return DEFAULT_AI_MODEL_CACHE_TTL_TICKS;
}

string ResolvePerformanceMode() {
    string mode = ToUpperAscii(g_config.GetString("PERFORMANCE_MODE", "LOW_END"));
    if (mode != "HIGH_PERFORMANCE" && mode != "BALANCED") {
        mode = "LOW_END";
    }
    return mode;
}

string NarrowWide(const std::wstring& text) {
    if (text.empty()) return "";
    const int required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string value(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), value.data(), required, nullptr, nullptr);
    return value;
}

DeviceSupportDescriptor BuildDeviceSupportDescriptor() {
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD computerNameLength = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computerName, &computerNameLength);
    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    SYSTEM_POWER_STATUS power{};
    GetSystemPowerStatus(&power);

    const unsigned long long memoryMb = memory.ullTotalPhys / (1024ULL * 1024ULL);
    const std::string hostToken = NarrowWide(std::wstring(computerName, computerNameLength));
    DeviceSupportDescriptor descriptor;
    descriptor.deviceId = EpisodeTelemetryStore::PseudonymizeLocalIdentifier("aegis-device:" + hostToken);
    descriptor.hardwareFingerprint = EpisodeTelemetryStore::PseudonymizeLocalIdentifier(
        hostToken + ":" + std::to_string(systemInfo.dwNumberOfProcessors) + ":" + std::to_string(memoryMb)
    );
    descriptor.windowsBuildFamily = "WINDOWS_BUILD_UNVERIFIED";
    descriptor.cpuCoreTier = std::to_string(systemInfo.dwNumberOfProcessors) + "_LOGICAL";
    descriptor.ramTier = std::to_string((memoryMb + 511ULL) / 1024ULL) + "GB";
    descriptor.storageTier = "UNCLASSIFIED";
    descriptor.gpuTier = "UNCLASSIFIED";
    descriptor.powerMode = power.ACLineStatus == AC_LINE_ONLINE ? "AC" : "BATTERY_OR_UNKNOWN";
    return descriptor;
}
wstring GetExecutableDir();
string ResolveExistingPathString(const vector<fs::path>& candidates);

string NormalizeScenarioLabel(string label) {
    label = ToUpperAscii(label);
    for (char& ch : label) {
        if (ch == '-' || ch == ' ') ch = '_';
    }

    if (label == "NORMAL" || label == "WARNING" || label == "CRITICAL" || label == "RECOVERY") {
        return label;
    }
    return "AUTO";
}

string ReadScenarioLabel() {
    const fs::path exeDir = GetExecutableDir();
    const string labelFileName = g_config.GetString("TRAINING_LABEL_FILE", "training_label.txt");
    const fs::path labelPath = ResolveExistingPathString({
        fs::current_path() / fs::path(labelFileName),
        exeDir / fs::path(labelFileName),
        exeDir.parent_path() / fs::path(labelFileName),
        exeDir.parent_path().parent_path() / fs::path(labelFileName),
    });

    ifstream file(labelPath);
    if (!file) return "AUTO";

    string label;
    getline(file, label);
    return NormalizeScenarioLabel(label);
}

wstring WidenAscii(const string& text) {
    return wstring(text.begin(), text.end());
}

wstring GetExecutableDir() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L".";
    return fs::path(path).parent_path().wstring();
}

wstring ResolveExistingPath(const vector<wstring>& candidates) {
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && fs::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.empty() ? L"" : candidates.front();
}

string ResolveExistingPathString(const vector<fs::path>& candidates) {
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && fs::exists(candidate)) {
            return candidate.string();
        }
    }
    return candidates.empty() ? "" : candidates.front().string();
}

void ShowAlert(const string& message) {
    MessageBeep(MB_ICONWARNING);
    MessageBoxA(nullptr, message.c_str(), "AI Alert", MB_OK | MB_ICONWARNING);
}

string JsonEscape(const string& value) {
    string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

void AppendRuntimeLog(const string& event, const map<string, string>& fields) {
    lock_guard<mutex> lock(g_logMutex);
    const fs::path logPath = fs::path(GetExecutableDir()) / L"runtime_events.jsonl";
    ofstream file(logPath, ios::app);
    if (!file) return;

    const auto now = system_clock::to_time_t(system_clock::now());
    file << "{\"ts\":" << now << ",\"event\":\"" << JsonEscape(event) << "\"";
    for (const auto& [key, value] : fields) {
        file << ",\"" << JsonEscape(key) << "\":\"" << JsonEscape(value) << "\"";
    }
    file << "}\n";
}


bool ConfigBool(const string& key, bool defaultValue) {
    return g_config.GetInt(key, defaultValue ? 1 : 0) != 0;
}

LowEndAutopilotConfig BuildLowEndAutopilotConfig(const string& performanceMode) {
    LowEndAutopilotConfig config;
    config.enabled = ConfigBool("LOW_END_AUTOPILOT_ENABLED", performanceMode == "LOW_END");
    config.forceLowEndDevice = ConfigBool("LOW_END_FORCE", false) || performanceMode == "LOW_END";
    config.protectForegroundApp = ConfigBool("AUTOPILOT_PROTECT_FOREGROUND", true);
    config.delayUpdaterAndSyncApps = ConfigBool("AUTOPILOT_DELAY_UPDATERS", true);
    config.sleepUnusedBrowserHelpers = ConfigBool("AUTOPILOT_SLEEP_BROWSER_HELPERS", true);
    config.preferReversibleActions = ConfigBool("AUTOPILOT_REVERSIBLE_ONLY", true);
    config.maxActionsPerCycle = max(1, g_config.GetInt("AUTOPILOT_MAX_ACTIONS", 4));
    config.weakDeviceMemoryMB = max(1024.0, g_config.GetDouble("LOW_END_RAM_MB", 4608.0));
    config.memoryPressureThreshold = ClampDouble(g_config.GetDouble("AUTOPILOT_MEMORY_PRESSURE", 78.0), 1.0, 100.0);
    config.cpuPressureThreshold = ClampDouble(g_config.GetDouble("AUTOPILOT_CPU_PRESSURE", 70.0), 1.0, 100.0);
    config.diskFreePressureThreshold = ClampDouble(g_config.GetDouble("AUTOPILOT_DISK_FREE_PRESSURE", 8.0), 1.0, 50.0);
    config.minCandidateSafetyScore = ClampDouble(g_config.GetDouble("AUTOPILOT_MIN_SAFETY", 55.0), 0.0, 100.0);
    return config;
}

bool IsStartupAgentRegistered() {
    wchar_t value[2048]{};
    DWORD valueSize = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"PredictiveAutoHealAgent",
        RRF_RT_REG_SZ,
        nullptr,
        value,
        &valueSize
    );
    return status == ERROR_SUCCESS;
}

BackgroundAgentConfig BuildBackgroundAgentConfig(HWND hwnd) {
    BackgroundAgentConfig config;
    config.enabled = ConfigBool("BACKGROUND_AGENT_ENABLED", true);
    config.trayIconEnabled = ConfigBool("AGENT_TRAY_ICON", true);
    config.silentMonitoring = ConfigBool("AGENT_SILENT_MONITORING", true);
    config.startOnBootConfigured = ConfigBool("AGENT_START_ON_BOOT", false) || IsStartupAgentRegistered();
    config.startMinimized = g_agentStartMinimized || ConfigBool("AGENT_START_MINIMIZED", false);
    config.hideOnMinimize = ConfigBool("AGENT_HIDE_ON_MINIMIZE", true);
    config.quickRestoreEnabled = ConfigBool("AGENT_QUICK_RESTORE", true);
    config.trayIconInstalled = g_trayIconInstalled;
    config.dashboardVisible = hwnd && IsWindowVisible(hwnd);
    config.quickRestoreRequested = g_quickRestoreRequests.load() > 0;
    {
        lock_guard<mutex> lock(g_dataMutex);
        config.quickRestoreStatus = g_quickRestoreStatus;
    }
    return config;
}

bool CommandLineHasFlag(const wstring& flag) {
    wstring commandLine = GetCommandLineW();
    transform(commandLine.begin(), commandLine.end(), commandLine.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    wstring normalized = flag;
    transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return commandLine.find(normalized) != wstring::npos;
}

void ShowDashboardWindow(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

void InstallTrayIcon(HWND hwnd) {
    if (g_trayIconInstalled || !ConfigBool("AGENT_TRAY_ICON", true)) return;

    ZeroMemory(&g_trayIcon, sizeof(g_trayIcon));
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = hwnd;
    g_trayIcon.uID = 1;
    g_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIcon.uCallbackMessage = WM_TRAYICON_MESSAGE;
    g_trayIcon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_trayIcon.szTip, L"PredictiveAutoHeal Agent");
    g_trayIconInstalled = Shell_NotifyIconW(NIM_ADD, &g_trayIcon) == TRUE;
}

void RemoveTrayIcon() {
    if (!g_trayIconInstalled) return;
    Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
    g_trayIconInstalled = false;
}

void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN_DASHBOARD, L"Open Dashboard");
    AppendMenuW(menu, MF_STRING, ID_TRAY_QUICK_RESTORE, L"Quick Restore (simulation)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

double ComputeFallbackProbability(double cpu, double mem, double disk, int cpuTh, int memTh, int diskTh) {
    double cpuScore = ClampDouble((cpu / max(1, cpuTh)) * 100.0, 0.0, 100.0);
    double memScore = ClampDouble((mem / max(1, memTh)) * 100.0, 0.0, 100.0);

    double diskScore = 0.0;
    if (disk < diskTh) {
        diskScore = ClampDouble(((diskTh - disk) / max(1, diskTh)) * 100.0, 0.0, 100.0);
    }

    double score = (cpuScore * 0.42) + (memScore * 0.42) + (diskScore * 0.16);
    return ClampDouble(score, 0.0, 100.0);
}

ModelPrediction ReadModelPrediction() {
    const fs::path exeDir = GetExecutableDir();
    const wstring scriptPath = ResolveExistingPath({
        (exeDir / L"predict_model.py").wstring(),
        (exeDir.parent_path() / L"predict_model.py").wstring(),
        (exeDir.parent_path().parent_path() / L"predict_model.py").wstring(),
        L"predict_model.py",
        L"..\\predict_model.py",
        L"..\\..\\predict_model.py",
    });
    const wstring modelPath = ResolveExistingPath({
        (exeDir / L"ai_model.joblib").wstring(),
        (exeDir.parent_path() / L"ai_model.joblib").wstring(),
        (exeDir.parent_path().parent_path() / L"ai_model.joblib").wstring(),
        L"ai_model.joblib",
        L"..\\ai_model.joblib",
    });
    const wstring inputPath = ResolveExistingPath({
        (exeDir / L"runtime_features.json").wstring(),
        (exeDir.parent_path() / L"runtime_features.json").wstring(),
        L"runtime_features.json",
    });
    const wstring outputPath = (exeDir / L"ai_prediction.txt").wstring();
    const wstring pythonExe = WidenAscii(g_config.GetString("PYTHON_EXE", "python"));

    const wstring command =
        L"cmd.exe /S /C \"\"" + pythonExe +
        L"\" \"" + scriptPath +
        L"\" --input \"" + inputPath +
        L"\" --model \"" + modelPath +
        L"\" > \"" + outputPath + L"\"\"";

    vector<wchar_t> cmdBuffer(command.begin(), command.end());
    cmdBuffer.push_back(L'\0');

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    BOOL ok = CreateProcessW(
        nullptr,
        cmdBuffer.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    ModelPrediction failed;
    if (!ok) return failed;

    string performanceMode = ResolvePerformanceMode();
    int defaultTimeoutMs = ResolveModeDefaultTimeoutMs(performanceMode);
    const DWORD timeoutMs = static_cast<DWORD>(max(1000, g_config.GetInt("AI_MODEL_TIMEOUT_MS", defaultTimeoutMs)));
    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        DeleteFileW(outputPath.c_str());
        return failed;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        DeleteFileW(outputPath.c_str());
        return failed;
    }

    ifstream file(outputPath);
    string text((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());

    DeleteFileW(outputPath.c_str());

    return ParseModelPredictionText(text);
}

ModelPrediction ReadModelPredictionWithRetry() {
    for (int attempt = 0; attempt < AI_MODEL_RETRY_COUNT; ++attempt) {
        ModelPrediction prediction = ReadModelPrediction();
        if (prediction.risk >= 0.0) return prediction;

        if (attempt + 1 < AI_MODEL_RETRY_COUNT) {
            this_thread::sleep_for(milliseconds(AI_MODEL_RETRY_DELAY_MS));
        }
    }
    return ModelPrediction{};
}

bool UsePersistentInferenceService() {
    string mode = ToUpperAscii(g_config.GetString("AI_INFERENCE_MODE", "SERVICE"));
    return mode == "SERVICE" || mode == "PERSISTENT_SERVICE";
}

bool IsInferenceServiceRunning() {
    if (!g_inferenceServiceActive) return false;

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(g_inferenceServiceProcess.hProcess, &exitCode)) {
        g_inferenceServiceActive = false;
        return false;
    }

    if (exitCode == STILL_ACTIVE) return true;

    CloseHandle(g_inferenceServiceProcess.hThread);
    CloseHandle(g_inferenceServiceProcess.hProcess);
    g_inferenceServiceProcess = PROCESS_INFORMATION{};
    g_inferenceServiceActive = false;
    return false;
}

bool StartInferenceService() {
    if (!UsePersistentInferenceService()) return false;
    if (IsInferenceServiceRunning()) return true;

    const fs::path exeDir = GetExecutableDir();
    const wstring scriptPath = ResolveExistingPath({
        (exeDir / L"inference_service.py").wstring(),
        (exeDir.parent_path() / L"inference_service.py").wstring(),
        (exeDir.parent_path().parent_path() / L"inference_service.py").wstring(),
        L"inference_service.py",
        L"..\\inference_service.py",
        L"..\\..\\inference_service.py",
    });
    const wstring modelPath = ResolveExistingPath({
        (exeDir / L"ai_model.joblib").wstring(),
        (exeDir.parent_path() / L"ai_model.joblib").wstring(),
        (exeDir.parent_path().parent_path() / L"ai_model.joblib").wstring(),
        L"ai_model.joblib",
        L"..\\ai_model.joblib",
    });
    const wstring inputPath = (exeDir / L"runtime_features.json").wstring();
    const wstring outputPath = (exeDir / L"ai_prediction_service.json").wstring();

    if (scriptPath.empty() || modelPath.empty() || !fs::exists(scriptPath) || !fs::exists(modelPath)) {
        AppendRuntimeLog("inference_service_unavailable", {
            {"script", fs::path(scriptPath).string()},
            {"model", fs::path(modelPath).string()},
        });
        return false;
    }

    const wstring pythonExe = WidenAscii(g_config.GetString("PYTHON_EXE", "python"));
    const int pollMs = max(200, g_config.GetInt("AI_SERVICE_POLL_MS", 1000));
    const wstring command =
        L"\"" + pythonExe +
        L"\" \"" + scriptPath +
        L"\" --input \"" + inputPath +
        L"\" --model \"" + modelPath +
        L"\" --output \"" + outputPath +
        L"\" --poll-ms " + to_wstring(pollMs);

    vector<wchar_t> cmdBuffer(command.begin(), command.end());
    cmdBuffer.push_back(L'\0');

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    BOOL ok = CreateProcessW(
        nullptr,
        cmdBuffer.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!ok) {
        AppendRuntimeLog("inference_service_start_failed", {
            {"script", fs::path(scriptPath).string()},
            {"model", fs::path(modelPath).string()},
        });
        return false;
    }

    g_inferenceServiceProcess = pi;
    g_inferenceServiceActive = true;
    AppendRuntimeLog("inference_service_started", {
        {"script", fs::path(scriptPath).filename().string()},
        {"poll_ms", to_string(pollMs)},
    });
    return true;
}

void StopInferenceService() {
    if (!g_inferenceServiceActive) return;

    DWORD exitCode = 1;
    if (GetExitCodeProcess(g_inferenceServiceProcess.hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
        TerminateProcess(g_inferenceServiceProcess.hProcess, 0);
        WaitForSingleObject(g_inferenceServiceProcess.hProcess, 1000);
    }

    CloseHandle(g_inferenceServiceProcess.hThread);
    CloseHandle(g_inferenceServiceProcess.hProcess);
    g_inferenceServiceProcess = PROCESS_INFORMATION{};
    g_inferenceServiceActive = false;
}

ModelPrediction ReadServicePrediction() {
    ModelPrediction failed;
    const fs::path outputPath = fs::path(GetExecutableDir()) / L"ai_prediction_service.json";
    if (!fs::exists(outputPath)) return failed;

    ifstream file(outputPath);
    if (!file) return failed;

    string text((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    return ParseModelPredictionText(text);
}

double ReadCurrentProcessCpuTimeMs() {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0.0;
    ULARGE_INTEGER kernelTicks{};
    kernelTicks.LowPart = kernel.dwLowDateTime;
    kernelTicks.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER userTicks{};
    userTicks.LowPart = user.dwLowDateTime;
    userTicks.HighPart = user.dwHighDateTime;
    return static_cast<double>(kernelTicks.QuadPart + userTicks.QuadPart) / 10000.0;
}
double ReadCurrentProcessIoBytes() {
    IO_COUNTERS counters{};
    if (!GetProcessIoCounters(GetCurrentProcess(), &counters)) return 0.0;
    return static_cast<double>(counters.ReadTransferCount + counters.WriteTransferCount + counters.OtherTransferCount);
}

void MonitorThread(HWND hwnd) {
    WindowsMetricsCollector collector;
    if (!collector.Initialize()) return;
    WorkloadPhaseDetector workloadDetector;
    QoeTelemetryCollector qoeCollector;
    AdaptiveSamplingConfig qoeSamplingConfig;
    qoeSamplingConfig.stableIntervalMs = std::max(1000, g_config.GetInt("QOE_STABLE_INTERVAL_MS", 5000));
    qoeSamplingConfig.elevatedRiskIntervalMs = std::max(500, g_config.GetInt("QOE_ELEVATED_INTERVAL_MS", 1000));
    qoeSamplingConfig.maximumFocusedBurstMs = std::max(
        qoeSamplingConfig.elevatedRiskIntervalMs,
        g_config.GetInt("QOE_MAX_FOCUSED_BURST_MS", 30000)
    );
    AdaptiveSamplingController qoeSampling(qoeSamplingConfig);
    CollectorCostRegistry collectorCosts;
    collectorCosts.Configure("qoe", SamplingTier::Tier2Focused, qoeSamplingConfig.elevatedRiskIntervalMs, qoeSamplingConfig.maximumFocusedBurstMs);
    ObserverEffectConfig observerConfig;
    observerConfig.maximumCollectorP95Ms = std::max(1.0, g_config.GetDouble("OBSERVER_MAX_COLLECTOR_P95_MS", 15.0));
    observerConfig.maximumInferenceP95Ms = std::max(1.0, g_config.GetDouble("OBSERVER_MAX_INFERENCE_P95_MS", 20.0));
    observerConfig.maximumProcessCpuPercent = std::max(0.1, g_config.GetDouble("OBSERVER_MAX_PROCESS_CPU_PERCENT", 1.0));
    observerConfig.maximumProcessIoBytesPerSecond = std::max(1024.0, g_config.GetDouble("OBSERVER_MAX_PROCESS_IO_BPS", 1048576.0));
    observerConfig.maximumWakeupsPerSecond = std::max(0.1, g_config.GetDouble("OBSERVER_MAX_WAKEUPS_PER_SEC", 2.0));
    observerConfig.maximumWorkingSetMb = std::max(16.0, g_config.GetDouble("OBSERVER_MAX_WORKING_SET_MB", 100.0));
    observerConfig.maximumPendingWrites = static_cast<size_t>(std::max(1, g_config.GetInt("OBSERVER_MAX_PENDING_WRITES", 64)));
    ObserverEffectGovernor observerGovernor(observerConfig);
    EvidenceBudgetScheduler evidenceScheduler(g_config.GetDouble("EVIDENCE_MIN_VALUE_PER_COST", 0.20));
    PerformanceCriticalityEngine criticalityEngine;
    WorkloadProtectionEngine workloadProtectionEngine;
    IntegrationCapabilityDetector integrationCapabilityDetector;
    const IntegrationCapabilities integrationCapabilities = integrationCapabilityDetector.Detect(GetExecutableDir());
    CooperativeIntegrationJournal cooperativeJournal;
    string cooperativeJournalError;
    const bool cooperativeJournalReady = cooperativeJournal.Open((fs::path(GetExecutableDir()) / L"monitor.db").string(), cooperativeJournalError);
    BrowserIntegrationBridge browserBridge(cooperativeJournalReady ? &cooperativeJournal : nullptr);
    string browserBridgeError;
    if (g_config.GetInt("BROWSER_COOP_ENABLED", 0) != 0) browserBridge.Start(browserBridgeError);
    QoeTelemetryJournal qoeJournal;
    string qoeInitializationError;
    const bool qoeCollectorReady = qoeCollector.Initialize(qoeInitializationError);
    string qoeJournalError;
    const bool qoeJournalReady = qoeJournal.Open((fs::path(GetExecutableDir()) / L"monitor.db").string(), qoeJournalError);
    EpisodeTelemetryStore episodeTelemetryStore;
    std::string episodeTelemetryError;
    const bool episodeTelemetryReady = episodeTelemetryStore.Open(
        (fs::path(GetExecutableDir()) / L"monitor.db").string(),
        BuildDeviceSupportDescriptor(),
        episodeTelemetryError
    );
    if (!episodeTelemetryReady) {
        AppendRuntimeLog("episode_telemetry_open_failed", {{"reason", episodeTelemetryError}});
    }
    WorkloadContextEncoder impactContextEncoder;
    ContextualImpactModel impactModel;
    ShadowContextualPolicy shadowImpactPolicy;
    LearningJournal learningJournal;
    string learningJournalError;
    const bool learningJournalReady = learningJournal.Open((fs::path(GetExecutableDir()) / L"monitor.db").string(), learningJournalError);
    if (learningJournalReady) learningJournal.RegisterPolicyVersion("impact-bandit-v1", "workload-context-v1:22", false, learningJournalError);
    auto nativeActionExecutor = make_shared<WindowsProcessActionExecutor>();
    ActionCoordinator actionCoordinator(nativeActionExecutor);
    SafeOnlinePolicyController onlinePolicy(actionCoordinator, impactModel, learningJournal);
    const RuntimeMode runtimeMode = ParseRuntimeMode(g_config.GetString("RUNTIME_MODE", "MONITOR_ONLY"));
    OnlinePolicyConfig onlinePolicyConfig;
    onlinePolicyConfig.onlineEnabled = RuntimeModePermitsAutomaticActions(runtimeMode, false) &&
        g_config.GetInt("ONLINE_POLICY_ENABLED", 0) != 0 &&
        g_config.GetInt("ACTION_EXECUTION_ENABLED", 0) != 0;
    bool persistedPolicyPromotion = false;
    string promotionLookupError;
    if (learningJournalReady) learningJournal.IsPolicyPromoted("impact-bandit-v1", persistedPolicyPromotion, promotionLookupError);
    onlinePolicyConfig.policyPromoted = g_config.GetInt("ONLINE_POLICY_PROMOTED", 0) != 0 && persistedPolicyPromotion;
    onlinePolicyConfig.globalDisable = g_config.GetInt("ACTION_GLOBAL_DISABLE", 1) != 0;
    onlinePolicyConfig.requireApproval = g_config.GetInt("ACTION_REQUIRE_USER_APPROVAL", 1) != 0;
    onlinePolicyConfig.maximumActionsPerHour = max(1, g_config.GetInt("ONLINE_MAX_ACTIONS_PER_HOUR", 3));
    onlinePolicyConfig.cooldownSeconds = max(10, g_config.GetInt("ONLINE_ACTION_COOLDOWN_SEC", 600));
    onlinePolicyConfig.maximumActionSeconds = max(1, g_config.GetInt("ACTION_MAX_DURATION_SEC", 30));
    onlinePolicyConfig.observationSeconds = max(1, g_config.GetInt("ONLINE_OBSERVATION_SEC", 8));
    onlinePolicyConfig.minimumLowerConfidenceBenefit = g_config.GetDouble("IMPACT_POLICY_REQUIRED_LOWER_BOUND", 0.05);
    onlinePolicyConfig.minimumTargetSafety = g_config.GetDouble("ONLINE_MIN_TARGET_SAFETY", 90.0);
    onlinePolicyConfig.maximumTargetCriticality = g_config.GetDouble("ONLINE_MAX_TARGET_CRITICALITY", 30.0);
    onlinePolicyConfig.killSwitchFile = fs::path(GetExecutableDir()) / L"STOP_ACTIONS";
    onlinePolicy.Configure(onlinePolicyConfig);
    vector<string> onlineRecoveryErrors;
    onlinePolicy.Initialize((fs::path(GetExecutableDir()) / L"monitor.db").string(), onlineRecoveryErrors);

    int cpuTh = g_config.GetInt("CPU_THRESHOLD", 80);
    int memTh = g_config.GetInt("MEM_THRESHOLD", 85);
    int diskTh = g_config.GetInt("DISK_THRESHOLD", 10);
    string performanceMode = ResolvePerformanceMode();
    int defaultPredictInterval = ResolveModeDefaultPredictInterval(performanceMode);
    int defaultModelCacheTtlTicks = ResolveModeDefaultCacheTtlTicks(performanceMode);
    int aiPredictIntervalTicks = max(1, g_config.GetInt("AI_PREDICT_INTERVAL_TICKS", defaultPredictInterval));
    double aiAlertThreshold = ClampDouble(g_config.GetDouble("AI_ALERT_THRESHOLD", DEFAULT_AI_ALERT_THRESHOLD), 0.0, 100.0);
    double aiAlertClearThreshold = ClampDouble(g_config.GetDouble("AI_ALERT_CLEAR_THRESHOLD", DEFAULT_AI_ALERT_CLEAR_THRESHOLD), 0.0, aiAlertThreshold);
    int aiAlertTriggerStreak = max(1, g_config.GetInt("AI_ALERT_TRIGGER_STREAK", DEFAULT_AI_ALERT_TRIGGER_STREAK));
    int aiAlertClearStreak = max(1, g_config.GetInt("AI_ALERT_CLEAR_STREAK", DEFAULT_AI_ALERT_CLEAR_STREAK));
    int modelCacheTtlTicks = max(aiPredictIntervalTicks, g_config.GetInt("AI_MODEL_CACHE_TTL_TICKS", defaultModelCacheTtlTicks));
    int runtimeHealthLogIntervalTicks = max(5, g_config.GetInt("RUNTIME_HEALTH_LOG_INTERVAL_TICKS", 30));
    int baselineMinSamples = max(10, g_config.GetInt("BASELINE_MIN_SAMPLES", 30));
    double baselineSensitivity = ClampDouble(g_config.GetDouble("BASELINE_SENSITIVITY", 1.6), 0.6, 3.0);
    int autopilotTelemetryLogIntervalTicks = max(1, g_config.GetInt("AUTOPILOT_TELEMETRY_LOG_INTERVAL_TICKS", performanceMode == "LOW_END" ? 10 : 5));
    LowEndAutopilotConfig lowEndAutopilotConfig = BuildLowEndAutopilotConfig(performanceMode);
    DecisionThresholds decisionThresholds;
    decisionThresholds.cpuThreshold = cpuTh;
    decisionThresholds.memThreshold = memTh;
    decisionThresholds.diskThreshold = diskTh;
    decisionThresholds.warningRiskThreshold = ClampDouble(g_config.GetDouble("DECISION_WARNING_THRESHOLD", 55.0), 0.0, 100.0);
    decisionThresholds.criticalRiskThreshold = ClampDouble(g_config.GetDouble("DECISION_CRITICAL_THRESHOLD", 75.0), decisionThresholds.warningRiskThreshold, 100.0);
    DecisionPolicy decisionPolicy;
    decisionPolicy.autoHealEnabled = RuntimeModePermitsAutomaticActions(runtimeMode, false) &&
        g_config.GetInt("AUTO_HEAL_ENABLED", 0) != 0;
    decisionPolicy.dryRun = g_config.GetInt("AUTO_HEAL_DRY_RUN", 1) != 0;
    decisionPolicy.safeMode = g_config.GetInt("SAFE_MODE", 1) != 0;
    decisionPolicy.cooldownSeconds = max(0, g_config.GetInt("AUTO_HEAL_COOLDOWN_SEC", 300));
    decisionPolicy.allowlistCsv = g_config.GetString("AUTO_HEAL_ALLOWLIST", "");
    decisionPolicy.denylistCsv = g_config.GetString("AUTO_HEAL_DENYLIST", "");
    const wstring runtimeFeaturesPath = (fs::path(GetExecutableDir()) / L"runtime_features.json").wstring();
    const bool preferInferenceService = UsePersistentInferenceService();
    const bool allowProcessFallback = g_config.GetInt("AI_SERVICE_FALLBACK_TO_PROCESS", 1) != 0;

    if (preferInferenceService) {
        StartInferenceService();
    }

    bool lastAlert = false;
    bool alertState = false;
    int highRiskStreak = 0;
    int lowRiskStreak = 0;
    ModelPrediction lastModelPrediction;
    long long lastModelTick = -modelCacheTtlTicks;
    string lastLoggedAutopilotStatus;
    string lastLoggedAgentStatus;
    string lastLoggedProofStatus;
    QoeTelemetrySample lastQoeSample;
    bool hasQoeSample = false;
    DWORD lastQoeForegroundPid = 0;
    string lastObserverEffectReason;
    RollingFeatureCache featureCache(HISTORY_SIZE);
    RollingPercentileWindow inferenceLatencyWindow(60);
    ProcessCpuUsageTracker monitorProcessCpu;
    CumulativeRateTracker monitorIoRate;
    CumulativeRateTracker monitorWakeupRate;
    unsigned long long monitorWakeups = 0;
    MonitoringScheduler monitorScheduler;

    while (g_running) {
        if (!monitorScheduler.WaitForNextTick(g_running, 1000)) break;

        SystemSnapshot snapshot = collector.Collect();
        snapshot.scenarioLabel = ReadScenarioLabel();
        WorkloadPhase workloadPhase = workloadDetector.Detect(snapshot);
        const long long nowMs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        const unsigned int logicalProcessors = static_cast<unsigned int>(std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
        const double monitorProcessCpuPercent = monitorProcessCpu.Observe(nowMs, ReadCurrentProcessCpuTimeMs(), logicalProcessors);
        const double monitorIoBytesPerSecond = monitorIoRate.Observe(nowMs, ReadCurrentProcessIoBytes());
        const double monitorWakeupsPerSecond = monitorWakeupRate.Observe(nowMs, static_cast<double>(++monitorWakeups));
        QoeTelemetrySample qoeSample;
        bool qoeCaptured = false;
        if (qoeCollectorReady) {
            AdaptiveSamplingInputs samplingInputs;
            samplingInputs.elevatedRisk = snapshot.cpuUsage >= cpuTh || snapshot.memoryUsage >= memTh || snapshot.diskFree <= diskTh;
            samplingInputs.severePressure = snapshot.cpuUsage >= 95.0 || snapshot.memoryUsage >= 95.0 || snapshot.diskFree <= 2.0;
            samplingInputs.foregroundChanged = hasQoeSample && lastQoeForegroundPid != snapshot.intent.foregroundPid;
            samplingInputs.dataInvalid = false;
            samplingInputs.safetyEvent = samplingInputs.severePressure;
            const AdaptiveSamplingDecision samplingDecision = qoeSampling.Next(nowMs, samplingInputs);
            const CollectorCostReport qoeCost = collectorCosts.Report("qoe");
            PROCESS_MEMORY_COUNTERS processMemory{};
            processMemory.cb = sizeof(processMemory);
            GetProcessMemoryInfo(GetCurrentProcess(), &processMemory, sizeof(processMemory));
            ObserverEffectSample observerSample;
            observerSample.collectorP95Ms = qoeCost.p95DurationMs;
            observerSample.inferenceP95Ms = inferenceLatencyWindow.P95();
            observerSample.processCpuPercent = monitorProcessCpuPercent;
            observerSample.processIoBytesPerSecond = monitorIoBytesPerSecond;
            observerSample.wakeupsPerSecond = monitorWakeupsPerSecond;
            observerSample.workingSetMb = static_cast<double>(processMemory.WorkingSetSize) / (1024.0 * 1024.0);
            observerSample.pendingWrites = g_pipeline.PendingCount();
            const ObserverEffectDecision observerDecision = observerGovernor.Observe(observerSample);
            g_runtimeHealth.RecordObserverMetrics(
                observerSample.collectorP95Ms,
                observerSample.inferenceP95Ms,
                observerSample.processCpuPercent,
                observerSample.processIoBytesPerSecond,
                observerSample.wakeupsPerSecond,
                observerSample.workingSetMb,
                static_cast<int>(observerSample.pendingWrites),
                observerDecision.reason
            );
            if (observerDecision.reason != lastObserverEffectReason) {
                AppendRuntimeLog("observer_effect_state", {{"reason", observerDecision.reason}, {"over_budget", observerDecision.overBudget ? "1" : "0"}});
                lastObserverEffectReason = observerDecision.reason;
            }
            EvidenceCandidate evidenceCandidate;
            evidenceCandidate.tier = samplingDecision.tier;
            evidenceCandidate.expectedRiskReduction = samplingInputs.severePressure ? 100.0 : samplingInputs.elevatedRisk ? 20.0 : 1.0;
            evidenceCandidate.estimatedCpuCost = std::max(0.1, qoeCost.p95DurationMs / 10.0);
            evidenceCandidate.estimatedLatencyCostMs = qoeCost.p95DurationMs;
            evidenceCandidate.safetyRequired = samplingInputs.safetyEvent || samplingInputs.severePressure;
            const EvidenceBudgetDecision evidenceDecision = evidenceScheduler.Decide(evidenceCandidate, observerDecision);
            if (samplingDecision.capture && evidenceDecision.collect) {
                qoeSample = qoeCollector.Capture(snapshot, workloadPhase);
                workloadPhase = workloadDetector.Detect(snapshot, &qoeSample);
                qoeSample.workload = workloadPhase;
                qoeSampling.RecordCapture(nowMs, qoeSample.withinOverheadBudget);
                collectorCosts.Record("qoe", {nowMs, qoeSample.collectionOverheadMs, true});
                lastQoeSample = qoeSample;
                hasQoeSample = true;
                lastQoeForegroundPid = snapshot.intent.foregroundPid;
                qoeCaptured = true;
            } else if (hasQoeSample) {
                qoeSample = lastQoeSample;
                qoeSample.availability += ",reused=1";
            } else {
                qoeSample.timestampMs = nowMs;
                qoeSample.workload = workloadPhase;
                qoeSample.foregroundPid = snapshot.intent.foregroundPid;
                qoeSample.foregroundProcess = snapshot.intent.foregroundProcess;
                qoeSample.availability = "awaiting_scheduled_initial_capture";
            }
        } else {
            qoeSample.timestampMs = nowMs;
            qoeSample.workload = workloadPhase;
            qoeSample.foregroundPid = snapshot.intent.foregroundPid;
            qoeSample.foregroundProcess = snapshot.intent.foregroundProcess;
            qoeSample.availability = "collector_unavailable:" + qoeInitializationError;
        }
        PerformanceCriticalityGraph criticalityGraph = criticalityEngine.Build(snapshot, workloadPhase);
        WorkloadProtectionResult workloadProtection = workloadProtectionEngine.Build(snapshot, criticalityGraph, workloadPhase);
        if (qoeJournalReady && qoeCaptured) {
            string qoeWriteError;
            const bool qoeSaved = qoeJournal.Save(qoeSample, criticalityGraph, qoeWriteError);
            if (!qoeSaved) AppendRuntimeLog("qoe_storage_write_failed", {{"reason", qoeWriteError}});
            if ((g_tick % 300) == 0) qoeJournal.EnforceRetention(10000, 50000, qoeWriteError);
        }
        if (episodeTelemetryReady) {
            EpisodeTelemetryInput episodeInput;
            episodeInput.snapshot = snapshot;
            episodeInput.qoe = qoeSample;
            episodeInput.workload = workloadPhase;
            episodeInput.qoeCaptured = qoeCaptured;
            episodeInput.collectorReady = qoeCollectorReady;
            episodeInput.runtimeMode = ToString(runtimeMode);
            episodeInput.featureSourceVersion = "qoe-pdh-winapi-v3";
            std::string episodeWriteError;
            if (!episodeTelemetryStore.Record(episodeInput, episodeWriteError)) {
                AppendRuntimeLog("episode_telemetry_write_failed", {{"reason", episodeWriteError}});
            }
            if ((g_tick % 300) == 0) {
                const long long retentionMs = static_cast<long long>(std::max(1, g_config.GetInt("EPISODE_RETENTION_DAYS", 14))) * 24LL * 60LL * 60LL * 1000LL;
                episodeTelemetryStore.EnforceRetention(nowMs - retentionMs, episodeWriteError);
            }
        }
        deque<double> cpuCopy, memCopy, diskCopy, netCopy, processCopy, topCpuCopy, topMemCopy;
        double aiProbLocal = 0.0;
        double aiConfidenceLocal = 0.0;
        string sourceLocal = "WARMING UP";
        string reasonLocal = "Collecting baseline";
        bool alertLocal = false;
        DecisionResult decisionResult;
        const RollingFeatureSnapshot featureSnapshot = featureCache.Push({
            snapshot.cpuUsage,
            snapshot.memoryUsage,
            snapshot.diskFree,
            snapshot.netDownKBps + snapshot.netUpKBps,
            static_cast<double>(snapshot.processCount),
            snapshot.topProcess.cpuPercent,
            snapshot.topProcess.memoryMB,
            qoeSample.inputAvailable ? qoeSample.inputResponseMs : snapshot.netDownKBps,
        });

        {
            lock_guard<mutex> lock(g_dataMutex);

            g_cpu = snapshot.cpuUsage;
            g_mem = snapshot.memoryUsage;
            g_totalMemoryMB = snapshot.totalMemoryMB;
            g_availableMemoryMB = snapshot.availableMemoryMB;
            g_disk = snapshot.diskFree;
            g_netDown = snapshot.netDownKBps;
            g_netUp = snapshot.netUpKBps;
            g_processCount = snapshot.processCount;
            g_scenarioLabel = snapshot.scenarioLabel;
            g_topProcessPid = snapshot.topProcess.pid;
            g_topProcessName = snapshot.topProcess.name;
            g_topProcessCpu = snapshot.topProcess.cpuPercent;
            g_topProcessMem = snapshot.topProcess.memoryMB;
            g_topProcessPrivateMem = snapshot.topProcess.privateMemoryMB;
            g_topProcessWaste = snapshot.topProcess.wasteScore;
            g_topProcessExpectedGain = snapshot.topProcess.expectedGainMB;
            g_topProcessCategory = snapshot.topProcess.category;
            g_topProcessSafety = snapshot.topProcess.safety;
            g_topProcessRecommendation = snapshot.topProcess.recommendation;
            g_topProcessReason = snapshot.topProcess.reason;
            g_userState = snapshot.intent.userState;
            g_foregroundProcess = snapshot.intent.foregroundProcess;
            g_foregroundAppKind = snapshot.intent.appKind;
            g_intentReason = snapshot.intent.reason;
            g_userIdleSeconds = snapshot.intent.idleSeconds;
            g_focusDurationSeconds = snapshot.intent.focusDurationSeconds;
            g_foregroundFullscreen = snapshot.intent.isFullscreen;
            g_processGenome = snapshot.processGenome;
            g_qoeSample = qoeSample;
            g_criticalityGraph = criticalityGraph;
            g_runtimeMode = runtimeMode;

            g_cpuHist = featureSnapshot.cpu;
            g_memHist = featureSnapshot.memory;
            g_diskHist = featureSnapshot.diskFree;
            g_netHist = featureSnapshot.network;
            g_processHist = featureSnapshot.processCount;
            g_topCpuHist = featureSnapshot.topProcessCpu;
            g_topMemHist = featureSnapshot.topProcessMemory;
            g_inputLatencyHist = featureSnapshot.responsivenessProxy;

            cpuCopy = featureSnapshot.cpu;
            memCopy = featureSnapshot.memory;
            diskCopy = featureSnapshot.diskFree;
            netCopy = featureSnapshot.network;
            processCopy = featureSnapshot.processCount;
            topCpuCopy = featureSnapshot.topProcessCpu;
            topMemCopy = featureSnapshot.topProcessMemory;        }

        g_pipeline.Enqueue(snapshot);
        AdaptiveBaselineResult baselineResult = g_adaptiveBaseline.EvaluateAndUpdate(snapshot, baselineMinSamples, baselineSensitivity);
        const auto featurePacketStart = steady_clock::now();
        const bool featurePacketWritten = WriteRuntimeFeaturePacket(runtimeFeaturesPath, {
            AI_WINDOW,
            cpuTh,
            memTh,
            diskTh,
            cpuCopy,
            memCopy,
            diskCopy,
            netCopy,
            processCopy,
            topCpuCopy,
            topMemCopy,
        });
        const double featurePacketLatencyMs = static_cast<double>(duration_cast<milliseconds>(steady_clock::now() - featurePacketStart).count());
        if (!featurePacketWritten && (g_tick % 30) == 0) {
            AppendRuntimeLog("legacy_feature_packet_write_failed", {{"latency_ms", to_string(featurePacketLatencyMs)}});
        }

        ModelPrediction modelPrediction;
        string predictionPath = "none";
        string predictionFailure = "none";
        double predictionLatencyMs = 0.0;
        const bool hasModelWindow =
            cpuCopy.size() >= AI_WINDOW &&
            memCopy.size() >= AI_WINDOW &&
            diskCopy.size() >= AI_WINDOW;

        const bool shouldRunModel =
            hasModelWindow &&
            ((g_tick % aiPredictIntervalTicks) == 0 || g_aiSource == "WARMING UP");

        if (shouldRunModel) {
            const auto predictionStart = steady_clock::now();
            predictionPath = "attempted";

            if (preferInferenceService) {
                if (StartInferenceService()) {
                    predictionPath = "service";
                    modelPrediction = ReadServicePrediction();
                    if (modelPrediction.risk < 0.0) {
                        predictionFailure = "service did not return a usable prediction";
                    }
                } else {
                    predictionFailure = "inference service unavailable";
                }
            }

            if (modelPrediction.risk < 0.0 && (!preferInferenceService || allowProcessFallback)) {
                predictionPath = "process";
                modelPrediction = ReadModelPredictionWithRetry();
                if (modelPrediction.risk < 0.0 && predictionFailure == "none") {
                    predictionFailure = "one-shot prediction failed";
                }
            }

            if (modelPrediction.risk < 0.0 && predictionFailure == "none") {
                predictionFailure = "model unavailable";
            }

            predictionLatencyMs = static_cast<double>(duration_cast<milliseconds>(steady_clock::now() - predictionStart).count());
            inferenceLatencyWindow.Record(predictionLatencyMs);
        }
        double currentAiProb = 0.0;
        double currentAiConfidence = 0.0;
        string currentSource = "WARMING UP";
        string currentAiReason = "Collecting baseline";
        string currentAiClass = "UNKNOWN";
        string currentRootCause = "unknown";
        string currentRecommendedAction = "monitor_only";
        string currentPredictionPath = "none";

        {
            lock_guard<mutex> lock(g_dataMutex);

            if (!hasModelWindow) {
                g_aiProb = ComputeFallbackProbability(snapshot.cpuUsage, snapshot.memoryUsage, snapshot.diskFree, cpuTh, memTh, diskTh);
                g_aiConfidence = 35.0;
                g_aiClass = "WARMING_UP";
                g_aiReason = "Collecting baseline window";
                g_rootCause = "baseline";
                g_modelReadiness = "unknown";
                g_modelGeneratedAt = "unknown";
                g_modelFeatureCount = 0;
                g_recommendedAction = "monitor_only";
                g_safeToHeal = false;
                g_aiSource = "WARMING UP";
                currentPredictionPath = "warmup";
            } else if (modelPrediction.risk >= 0.0) {
                lastModelPrediction = modelPrediction;
                lastModelTick = g_tick;
                g_aiProb = modelPrediction.risk;
                g_aiConfidence = modelPrediction.confidence;
                g_aiClass = modelPrediction.predictedClass;
                g_aiReason = modelPrediction.reason;
                g_rootCause = modelPrediction.rootCause;
                g_modelReadiness = modelPrediction.modelReadiness;
                g_modelGeneratedAt = modelPrediction.modelGeneratedAt;
                g_modelFeatureCount = modelPrediction.featureCount;
                g_recommendedAction = modelPrediction.recommendedAction;
                g_safeToHeal = modelPrediction.safeToHeal;
                g_aiSource = "MODEL";
                currentPredictionPath = predictionPath == "attempted" ? "model" : predictionPath;
            } else if (lastModelPrediction.risk >= 0.0 && (g_tick - lastModelTick) <= modelCacheTtlTicks) {
                g_aiProb = lastModelPrediction.risk;
                g_aiConfidence = lastModelPrediction.confidence;
                g_aiClass = lastModelPrediction.predictedClass;
                g_aiReason = lastModelPrediction.reason;
                g_rootCause = lastModelPrediction.rootCause;
                g_modelReadiness = lastModelPrediction.modelReadiness;
                g_modelGeneratedAt = lastModelPrediction.modelGeneratedAt;
                g_modelFeatureCount = lastModelPrediction.featureCount;
                g_recommendedAction = lastModelPrediction.recommendedAction;
                g_safeToHeal = lastModelPrediction.safeToHeal;
                g_aiSource = "MODEL";
                currentPredictionPath = "cache";
            } else {
                g_aiProb = ComputeFallbackProbability(snapshot.cpuUsage, snapshot.memoryUsage, snapshot.diskFree, cpuTh, memTh, diskTh);
                g_aiConfidence = 40.0;
                g_aiClass = g_aiProb >= decisionThresholds.criticalRiskThreshold ? "CRITICAL" : (g_aiProb >= decisionThresholds.warningRiskThreshold ? "WARNING" : "NORMAL");
                g_aiReason = "fallback threshold pressure";
                g_rootCause = "threshold_pressure";
                g_modelReadiness = "unknown";
                g_modelGeneratedAt = "unknown";
                g_modelFeatureCount = 0;
                g_recommendedAction = g_aiProb >= decisionThresholds.warningRiskThreshold ? "increase_observation" : "monitor_only";
                g_safeToHeal = false;
                g_aiSource = "FALLBACK";
                currentPredictionPath = "fallback";
            }

            currentAiProb = g_aiProb;
            currentAiConfidence = g_aiConfidence;
            currentSource = g_aiSource;
            currentAiReason = g_aiReason;
            currentAiClass = g_aiClass;
            currentRootCause = g_rootCause;
            currentRecommendedAction = g_recommendedAction;
        }

        DecisionContext decisionContext;
        decisionContext.cpuHistory = cpuCopy;
        decisionContext.memHistory = memCopy;
        decisionContext.diskHistory = diskCopy;
        decisionContext.netHistory = netCopy;
        decisionContext.processHistory = processCopy;
        decisionContext.baselineAnomalyScore = baselineResult.anomalyScore;
        decisionContext.baselineRiskAdjustment = baselineResult.riskAdjustment;
        decisionContext.baselineStatus = baselineResult.status;
        decisionContext.baselineDominantMetric = baselineResult.dominantMetric;
        decisionContext.baselineReady = baselineResult.ready;
        decisionContext.qoeAvailable = qoeSample.inputAvailable || qoeSample.pageReadAvailable || qoeSample.diskQueueAvailable || qoeSample.foregroundProgressAvailable;
        decisionContext.workloadPhase = ToString(workloadPhase);
        decisionContext.inputResponseMs = qoeSample.inputResponseMs;
        decisionContext.systemPageReadsPerSecond = qoeSample.systemPageReadsPerSecond;
        decisionContext.diskQueueLength = qoeSample.diskQueueLength;
        decisionContext.droppedFramesPerSecond = qoeSample.droppedFramesPerSecond;
        for (const CriticalityNode& node : criticalityGraph.nodes) {
            if (node.protectedFromIntervention) decisionContext.protectedCriticalPathPids.push_back(node.pid);
        }
        for (DWORD protectedPid : workloadProtection.protectedPids) {
            if (find(decisionContext.protectedCriticalPathPids.begin(), decisionContext.protectedCriticalPathPids.end(), protectedPid) == decisionContext.protectedCriticalPathPids.end()) {
                decisionContext.protectedCriticalPathPids.push_back(protectedPid);
            }
        }
        decisionResult = g_decisionEngine.Evaluate(
            snapshot,
            currentAiProb,
            currentAiConfidence,
            currentSource,
            currentAiReason,
            decisionThresholds,
            decisionContext,
            decisionPolicy
        );
        double targetCriticality = 100.0;
        for (const CriticalityNode& node : criticalityGraph.nodes) {
            if (node.pid == decisionResult.actionTargetPid) {
                targetCriticality = node.criticality;
                break;
            }
        }
        const WorkloadContextFeatures impactContext = impactContextEncoder.Encode(
            snapshot,
            qoeSample,
            workloadPhase,
            decisionResult.actionTargetPid,
            targetCriticality,
            decisionResult.candidate.safetyScore
        );
        const bool shadowTargetSafe = decisionResult.actionTargetPid != 0 &&
            targetCriticality < 70.0 &&
            decisionResult.candidate.safetyScore >= 70.0 &&
            !decisionResult.candidate.protectedByUserIntent &&
            !decisionResult.candidate.deniedByPolicy;
        vector<ImpactCandidate> shadowCandidates = {
            {ResourceActionType::LowerPriority, decisionResult.actionTargetPid, decisionResult.actionTarget, true, shadowTargetSafe},
            {ResourceActionType::EnableEcoQos, decisionResult.actionTargetPid, decisionResult.actionTarget, true, shadowTargetSafe},
            {ResourceActionType::LowerMemoryPriority, decisionResult.actionTargetPid, decisionResult.actionTarget, true, shadowTargetSafe},
        };
        const ShadowPolicyDecision shadowDecision = shadowImpactPolicy.Select(
            impactContext,
            shadowCandidates,
            impactModel,
            0.05,
            max(20, g_config.GetInt("IMPACT_POLICY_MIN_OBSERVATIONS", 30))
        );
        if (learningJournalReady && (g_tick % 10) == 0) {
            string shadowWriteError;
            learningJournal.SaveShadowDecision(shadowDecision, impactContext, shadowWriteError);
        }
        HealPlan healPlan = g_autoHealPlanner.BuildPlan(snapshot, decisionResult, decisionPolicy);
        HealVerification healVerification = g_healingVerifier.Evaluate(snapshot, decisionResult, healPlan);
        SafetyPolicyResult policyResult = g_safetyPolicyEngine.Evaluate(snapshot, decisionResult, healPlan, healVerification, decisionPolicy);
        LowEndAutopilotResult autopilotResult = g_lowEndAutopilot.Evaluate(snapshot, decisionResult, healPlan, baselineResult, lowEndAutopilotConfig);
        BackgroundAgentConfig backgroundAgentConfig = BuildBackgroundAgentConfig(hwnd);
        BackgroundAgentResult backgroundAgentResult = g_backgroundAgent.Evaluate(snapshot, autopilotResult, backgroundAgentConfig);
        BenchmarkProofResult benchmarkProofResult = g_benchmarkProof.Build(snapshot, decisionResult, healPlan, autopilotResult);
        OnlinePolicyInput onlineInput;
        onlineInput.shadow = shadowDecision;
        onlineInput.deterministicPolicy = policyResult;
        onlineInput.context = impactContext;
        onlineInput.foregroundPid = snapshot.intent.foregroundPid;
        onlineInput.allowlist = SplitCsvValues(g_config.GetString("ACTION_ALLOWED_PROCESSES", ""));
        onlineInput.protectedNames = SplitCsvValues(decisionPolicy.denylistCsv);
        onlineInput.protectedNames.insert(onlineInput.protectedNames.end(), workloadProtection.protectedNames.begin(), workloadProtection.protectedNames.end());
        for (const ProcessSnapshot& process : snapshot.processGenome) {
            if (process.pid == decisionResult.actionTargetPid) {
                onlineInput.targetExecutablePath = process.exePath;
                onlineInput.targetCategory = process.category;
                onlineInput.targetSafety = process.safety;
                onlineInput.targetSystemCritical = process.isSystemCritical;
                onlineInput.targetMatchesUserIntent = process.matchesUserIntent || process.isForeground || process.isRecentlyActive;
                break;
            }
        }
        const string approvalMode = ToUpperAscii(g_config.GetString("ACTION_APPROVAL_MODE", "MANUAL"));
        onlineInput.userApproved = approvalMode == "ALLOWLIST" &&
            ContainsProcessNameInsensitive(onlineInput.allowlist, shadowDecision.targetName);
        const OnlinePolicyDecision onlineDecision = onlinePolicy.Evaluate(onlineInput);
        if (onlineDecision.eligible) onlinePolicy.Execute(onlineInput, snapshot);
        const vector<ActionImpactResult> onlineOutcomes = onlinePolicy.Tick(snapshot, OutcomePolicy{});
        for (const ActionImpactResult& outcome : onlineOutcomes) {
            AppendRuntimeLog("online_action_outcome", {
                {"transaction_id", outcome.transactionId},
                {"status", ToString(outcome.status)},
                {"reward", to_string(outcome.reward)},
                {"confidence", to_string(outcome.confidence)},
                {"rollback", outcome.rollbackSucceeded ? "1" : "0"},
                {"reason", outcome.reason},
            });
        }
        const vector<BrowserBridgeResult> browserResults = browserBridge.DrainResults();
        for (const BrowserBridgeResult& result : browserResults) {
            AppendRuntimeLog("browser_cooperative_outcome", {
                {"transaction_id", result.transactionId},
                {"status", result.status},
                {"reason", result.reason},
            });
        }
        if (backgroundAgentResult.quickRestoreRequested) {
            vector<string> restoreErrors;
            onlinePolicy.EmergencyStop("user requested Quick Restore", restoreErrors);
            browserBridge.QueueRestoreAll();
            g_quickRestoreRequests.store(0);
        }

        {
            lock_guard<mutex> lock(g_dataMutex);
            g_riskScore = decisionResult.riskScore;
            g_anomalyScore = decisionResult.anomalyScore;
            g_pressureScore = decisionResult.pressureScore;
            g_decisionLevel = decisionResult.level;
            g_decisionSummary = decisionResult.summary;
            g_decisionReason = decisionResult.reason;
            g_decisionRootCauseDetail = decisionResult.rootCauseDetail;
            g_rootCause = decisionResult.rootCause;
            g_recommendedAction = decisionResult.recommendedAction;
            g_actionTarget = decisionResult.actionTarget;
            g_safetyGate = decisionResult.safetyGate;
            g_blockedReason = decisionResult.blockedReason;
            g_actionConfidence = decisionResult.actionConfidence;
            g_expectedOptimizationGain = decisionResult.expectedGainMB;
            g_cooldownRemainingSeconds = decisionResult.cooldownRemainingSeconds;
            g_candidateCount = decisionResult.candidateCount;
            g_dryRun = decisionResult.dryRun;
            g_safeToHeal = decisionResult.safeToHeal;
            g_healPlan = healPlan;
            g_healVerification = healVerification;
            g_safetyPolicyResult = policyResult;
            g_adaptiveBaselineResult = baselineResult;
            g_autopilotResult = autopilotResult;
            g_backgroundAgentResult = backgroundAgentResult;
            g_benchmarkProofResult = benchmarkProofResult;
            g_shadowPolicyDecision = shadowDecision;
            g_onlinePolicyDecision = onlineDecision;

            if (decisionResult.level == RiskLevel::Critical) {
                ++highRiskStreak;
                lowRiskStreak = 0;
            } else if (decisionResult.level == RiskLevel::Normal || decisionResult.riskScore <= aiAlertClearThreshold) {
                ++lowRiskStreak;
                highRiskStreak = 0;
            } else {
                highRiskStreak = 0;
                lowRiskStreak = 0;
            }

            if (!alertState && highRiskStreak >= aiAlertTriggerStreak) {
                alertState = true;
                lowRiskStreak = 0;
            } else if (alertState && lowRiskStreak >= aiAlertClearStreak) {
                alertState = false;
                highRiskStreak = 0;
            }

            aiProbLocal = g_aiProb;
            aiConfidenceLocal = g_aiConfidence;
            sourceLocal = g_aiSource;
            reasonLocal = g_decisionReason;
            alertLocal = alertState;
            g_alert = alertLocal;
        }

        const auto storageBatchStart = steady_clock::now();
        const bool decisionAuditOk = g_storage.LogDecisionAudit(
            snapshot,
            decisionResult,
            currentAiProb,
            currentAiConfidence,
            currentSource
        );
        g_runtimeHealth.RecordStorageWrite("decision_audit", decisionAuditOk);
        if (!decisionAuditOk && (g_tick % 30 == 0)) {
            AppendRuntimeLog("decision_audit_write_failed", {
                {"risk", to_string(static_cast<int>(decisionResult.riskScore))},
                {"action", decisionResult.recommendedAction},
                {"target", decisionResult.actionTarget},
            });
        }

        const bool healPlanOk = g_storage.LogHealPlan(snapshot, decisionResult, healPlan);
        g_runtimeHealth.RecordStorageWrite("heal_plan", healPlanOk);
        if (!healPlanOk && (g_tick % 30 == 0)) {
            AppendRuntimeLog("heal_plan_write_failed", {
                {"plan_id", healPlan.planId},
                {"status", healPlan.status},
                {"action", healPlan.actionName},
                {"target", healPlan.targetName},
            });
        }

        const bool healVerificationOk = g_storage.LogHealVerification(snapshot, decisionResult, healPlan, healVerification);
        g_runtimeHealth.RecordStorageWrite("heal_verification", healVerificationOk);
        if (!healVerificationOk && (g_tick % 30 == 0)) {
            AppendRuntimeLog("heal_verification_write_failed", {
                {"verification_id", healVerification.verificationId},
                {"status", healVerification.status},
                {"outcome", healVerification.outcomeLabel},
                {"plan_id", healPlan.planId},
            });
        }

        const bool safetyPolicyOk = g_storage.LogSafetyPolicy(snapshot, decisionResult, healPlan, healVerification, policyResult);
        g_runtimeHealth.RecordStorageWrite("safety_policy", safetyPolicyOk);
        if (!safetyPolicyOk && (g_tick % 30 == 0)) {
            AppendRuntimeLog("safety_policy_write_failed", {
                {"level", policyResult.levelName},
                {"reason_code", policyResult.reasonCode},
                {"target", policyResult.targetName},
            });
        }

        const bool adaptiveBaselineOk = g_storage.LogAdaptiveBaseline(baselineResult);
        g_runtimeHealth.RecordStorageWrite("adaptive_baseline", adaptiveBaselineOk);
        if (!adaptiveBaselineOk && (g_tick % 30 == 0)) {
            AppendRuntimeLog("adaptive_baseline_write_failed", {
                {"status", baselineResult.status},
                {"dominant_metric", baselineResult.dominantMetric},
                {"anomaly", to_string(static_cast<int>(baselineResult.anomalyScore))},
            });
        }

        const bool stageStatusChanged =
            autopilotResult.status != lastLoggedAutopilotStatus ||
            backgroundAgentResult.status != lastLoggedAgentStatus ||
            benchmarkProofResult.status != lastLoggedProofStatus;
        const bool shouldLogAutopilotTelemetry =
            stageStatusChanged || (g_tick % autopilotTelemetryLogIntervalTicks) == 0;
        if (shouldLogAutopilotTelemetry) {
            const bool autopilotOk = g_storage.LogLowEndAutopilot(autopilotResult);
            g_runtimeHealth.RecordStorageWrite("low_end_autopilot", autopilotOk);
            if (!autopilotOk) {
                AppendRuntimeLog("low_end_autopilot_write_failed", {
                    {"status", autopilotResult.status},
                    {"actions", to_string(autopilotResult.actionsRecommended)},
                    {"primary_action", autopilotResult.primaryAction},
                });
            }

            const bool backgroundAgentOk = g_storage.LogBackgroundAgent(backgroundAgentResult);
            g_runtimeHealth.RecordStorageWrite("background_agent", backgroundAgentOk);
            if (!backgroundAgentOk) {
                AppendRuntimeLog("background_agent_write_failed", {
                    {"status", backgroundAgentResult.status},
                    {"mode", backgroundAgentResult.mode},
                    {"tray", backgroundAgentResult.trayIconReady ? "1" : "0"},
                });
            }

            const bool benchmarkProofOk = g_storage.LogBenchmarkProof(benchmarkProofResult);
            g_runtimeHealth.RecordStorageWrite("benchmark_proof", benchmarkProofOk);
            if (!benchmarkProofOk) {
                AppendRuntimeLog("benchmark_proof_write_failed", {
                    {"status", benchmarkProofResult.status},
                    {"actions", to_string(benchmarkProofResult.actionsRecommended)},
                    {"recovered_ram", to_string(static_cast<int>(benchmarkProofResult.recoveredRamMB))},
                });
            }

            lastLoggedAutopilotStatus = autopilotResult.status;
            lastLoggedAgentStatus = backgroundAgentResult.status;
            lastLoggedProofStatus = benchmarkProofResult.status;
        }
        if (alertLocal && !lastAlert) {
            g_runtimeHealth.RecordAlert();
            string msg =
                "System Risk Detected!\n\n" +
                string("Risk Score: ") + to_string(static_cast<int>(decisionResult.riskScore)) + "%\n" +
                string("AI Probability: ") + to_string(static_cast<int>(aiProbLocal)) + "%\n" +
                string("Confidence: ") + to_string(static_cast<int>(aiConfidenceLocal)) + "%\n" +
                string("Source: ") + sourceLocal + "\n" +
                string("Reason: ") + reasonLocal + "\n" +
                string("Top Process: ") + ShortenText(snapshot.topProcess.name, 28) + "\n" +
                string("Process Type: ") + snapshot.topProcess.category + "\n" +
                string("Safety: ") + snapshot.topProcess.safety + "\n" +
                string("Action: ") + decisionResult.recommendedAction + "\n" +
                string("Gate: ") + decisionResult.safetyGate + "\n" +
                string("Target: ") + decisionResult.actionTarget + "\n" +
                string("Plan: ") + healPlan.status + "\n" +
                string("Verify: ") + healVerification.status + "\n" +
                string("Policy: ") + policyResult.levelName + " / " + policyResult.reasonCode + "\n" +
                string("Decision: ") + decisionResult.summary;

            ShowAlert(msg);
            AppendRuntimeLog("alert_triggered", {
                {"risk", to_string(static_cast<int>(decisionResult.riskScore))},
                {"ai_probability", to_string(static_cast<int>(aiProbLocal))},
                {"confidence", to_string(static_cast<int>(aiConfidenceLocal))},
                {"source", sourceLocal},
                {"reason", reasonLocal},
                {"top_process", snapshot.topProcess.name},
                {"top_process_category", snapshot.topProcess.category},
                {"top_process_safety", snapshot.topProcess.safety},
                {"top_process_waste", to_string(static_cast<int>(snapshot.topProcess.wasteScore))},
                {"root_cause", decisionResult.rootCause},
                {"root_cause_detail", decisionResult.rootCauseDetail},
                {"action", decisionResult.recommendedAction},
                {"target", decisionResult.actionTarget},
                {"safety_gate", decisionResult.safetyGate},
                {"blocked_reason", decisionResult.blockedReason},
                {"dry_run", decisionResult.dryRun ? "1" : "0"},
                {"heal_plan_id", healPlan.planId},
                {"heal_plan_status", healPlan.status},
                {"heal_plan_action", healPlan.actionName},
                {"heal_plan_mode", healPlan.executionMode},
                {"heal_plan_readiness", to_string(static_cast<int>(healPlan.readinessScore))},
                {"heal_verification_status", healVerification.status},
                {"heal_verification_outcome", healVerification.outcomeLabel},
                {"risk_after_estimate", to_string(static_cast<int>(healVerification.riskAfterEstimate))},
                {"risk_delta_estimate", to_string(static_cast<int>(healVerification.riskDeltaEstimate))},
                {"policy_level", policyResult.levelName},
                {"policy_reason_code", policyResult.reasonCode},
                {"policy_score", to_string(static_cast<int>(policyResult.policyScore))},
                {"policy_execution_eligible", policyResult.executionEligible ? "1" : "0"},
                {"baseline_status", baselineResult.status},
                {"baseline_anomaly", to_string(static_cast<int>(baselineResult.anomalyScore))},
                {"baseline_metric", baselineResult.dominantMetric},
                {"autopilot_status", autopilotResult.status},
                {"autopilot_actions", to_string(autopilotResult.actionsRecommended)},
                {"autopilot_recovered_ram", to_string(static_cast<int>(autopilotResult.estimatedRecoveredRamMB))},
                {"agent_status", backgroundAgentResult.status},
                {"proof_status", benchmarkProofResult.status},
                {"user_state", snapshot.intent.userState},
                {"foreground_process", snapshot.intent.foregroundProcess},
                {"app_kind", snapshot.intent.appKind},
            });
        }

        g_runtimeHealth.RecordPredictionCycle(
            hasModelWindow,
            shouldRunModel,
            currentSource,
            currentPredictionPath,
            shouldRunModel && modelPrediction.risk >= 0.0,
            predictionLatencyMs,
            IsInferenceServiceRunning(),
            predictionFailure
        );

        RuntimeHealthSample runtimeHealthSample = g_runtimeHealth.Snapshot(
            snapshot.timestamp,
            currentSource,
            currentPredictionPath,
            IsInferenceServiceRunning(),
            g_storage.IsReady()
        );

        if ((g_tick % runtimeHealthLogIntervalTicks) == 0) {
            const bool runtimeHealthOk = g_storage.LogRuntimeHealth(runtimeHealthSample);
            g_runtimeHealth.RecordStorageWrite("runtime_health", runtimeHealthOk);
            if (!runtimeHealthOk) {
                AppendRuntimeLog("runtime_health_write_failed", {
                    {"status", runtimeHealthSample.status},
                    {"source", runtimeHealthSample.predictionSource},
                    {"path", runtimeHealthSample.predictionPath},
                });
            }
            runtimeHealthSample = g_runtimeHealth.Snapshot(
                snapshot.timestamp,
                currentSource,
                currentPredictionPath,
                IsInferenceServiceRunning(),
                g_storage.IsReady()
            );
            const bool runtimePerformanceOk = g_storage.LogRuntimePerformance(runtimeHealthSample);
            g_runtimeHealth.RecordStorageWrite("runtime_performance", runtimePerformanceOk);
            const double storageBatchLatencyMs = static_cast<double>(
                duration_cast<milliseconds>(steady_clock::now() - storageBatchStart).count()
            );
            g_runtimeHealth.RecordStorageBatchLatency(storageBatchLatencyMs);
            if (!runtimePerformanceOk) {
                AppendRuntimeLog("runtime_performance_write_failed", {});
            }
            runtimeHealthSample = g_runtimeHealth.Snapshot(
                snapshot.timestamp,
                currentSource,
                currentPredictionPath,
                IsInferenceServiceRunning(),
                g_storage.IsReady()
            );        }

        {
            lock_guard<mutex> lock(g_dataMutex);
            g_runtimeHealthSample = runtimeHealthSample;
        }

        if ((g_tick % max(runtimeHealthLogIntervalTicks, 30)) == 0) {
            AppendRuntimeLog("runtime_health_sample", {
                {"status", runtimeHealthSample.status},
                {"summary", runtimeHealthSample.summary},
                {"availability", to_string(static_cast<int>(runtimeHealthSample.availabilityScore))},
                {"model_success_rate", to_string(static_cast<int>(runtimeHealthSample.modelSuccessRate))},
                {"fallback_rate", to_string(static_cast<int>(runtimeHealthSample.fallbackRate))},
                {"storage_success_rate", to_string(static_cast<int>(runtimeHealthSample.storageSuccessRate))},
                {"avg_latency_ms", to_string(static_cast<int>(runtimeHealthSample.avgPredictionLatencyMs))},
                {"path", runtimeHealthSample.predictionPath},
                {"last_failure", runtimeHealthSample.lastFailure},
            });
        }
        if (shouldRunModel || (g_tick % 30 == 0)) {
            AppendRuntimeLog("prediction_cycle", {
                {"source", currentSource},
                {"prediction_path", currentPredictionPath},
                {"prediction_latency_ms", to_string(static_cast<int>(predictionLatencyMs))},
                {"runtime_health", runtimeHealthSample.status},
                {"class", currentAiClass},
                {"root_cause", currentRootCause},
                {"action", currentRecommendedAction},
                {"decision_root_cause", decisionResult.rootCause},
                {"root_cause_detail", decisionResult.rootCauseDetail},
                {"decision_action", decisionResult.recommendedAction},
                {"target", decisionResult.actionTarget},
                {"safety_gate", decisionResult.safetyGate},
                {"blocked_reason", decisionResult.blockedReason},
                {"dry_run", decisionResult.dryRun ? "1" : "0"},
                {"cooldown_remaining", to_string(decisionResult.cooldownRemainingSeconds)},
                {"action_confidence", to_string(static_cast<int>(decisionResult.actionConfidence))},
                {"candidate_count", to_string(decisionResult.candidateCount)},
                {"heal_plan_id", healPlan.planId},
                {"heal_plan_status", healPlan.status},
                {"heal_plan_action", healPlan.actionName},
                {"heal_plan_mode", healPlan.executionMode},
                {"heal_plan_readiness", to_string(static_cast<int>(healPlan.readinessScore))},
                {"heal_plan_would_execute", healPlan.wouldExecute ? "1" : "0"},
                {"heal_verification_status", healVerification.status},
                {"heal_verification_outcome", healVerification.outcomeLabel},
                {"risk_after_estimate", to_string(static_cast<int>(healVerification.riskAfterEstimate))},
                {"risk_delta_estimate", to_string(static_cast<int>(healVerification.riskDeltaEstimate))},
                {"policy_level", policyResult.levelName},
                {"policy_reason_code", policyResult.reasonCode},
                {"policy_score", to_string(static_cast<int>(policyResult.policyScore))},
                {"policy_execution_eligible", policyResult.executionEligible ? "1" : "0"},
                {"baseline_status", baselineResult.status},
                {"baseline_anomaly", to_string(static_cast<int>(baselineResult.anomalyScore))},
                {"baseline_metric", baselineResult.dominantMetric},
                {"autopilot_status", autopilotResult.status},
                {"autopilot_actions", to_string(autopilotResult.actionsRecommended)},
                {"autopilot_recovered_ram", to_string(static_cast<int>(autopilotResult.estimatedRecoveredRamMB))},
                {"agent_status", backgroundAgentResult.status},
                {"proof_status", benchmarkProofResult.status},
                {"model_readiness", g_modelReadiness},
                {"risk", to_string(static_cast<int>(decisionResult.riskScore))},
                {"ai_probability", to_string(static_cast<int>(currentAiProb))},
                {"confidence", to_string(static_cast<int>(currentAiConfidence))},
                {"cpu", to_string(static_cast<int>(snapshot.cpuUsage))},
                {"mem", to_string(static_cast<int>(snapshot.memoryUsage))},
                {"disk", to_string(static_cast<int>(snapshot.diskFree))},
                {"top_process", snapshot.topProcess.name},
                {"top_process_category", snapshot.topProcess.category},
                {"top_process_safety", snapshot.topProcess.safety},
                {"top_process_waste", to_string(static_cast<int>(snapshot.topProcess.wasteScore))},
                {"expected_gain_mb", to_string(static_cast<int>(snapshot.topProcess.expectedGainMB))},
                {"user_state", snapshot.intent.userState},
                {"foreground_process", snapshot.intent.foregroundProcess},
                {"app_kind", snapshot.intent.appKind},
                {"idle_seconds", to_string(static_cast<int>(snapshot.intent.idleSeconds))},
                {"quick_restore", backgroundAgentResult.quickRestoreStatus},
                {"benchmark_recovered_ram", to_string(static_cast<int>(benchmarkProofResult.recoveredRamMB))},
            });
        }

        lastAlert = alertLocal;
        ++g_tick;

        InvalidateRect(hwnd, NULL, TRUE);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreatedMessage != 0 && uMsg == g_taskbarCreatedMessage) {
        g_trayIconInstalled = false;
        InstallTrayIcon(hwnd);
        return 0;
    }

    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client;
        GetClientRect(hwnd, &client);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        HBRUSH bg = CreateSolidBrush(RGB(14, 17, 24));
        FillRect(memDC, &client, bg);
        DeleteObject(bg);

        SetBkMode(memDC, TRANSPARENT);

        SystemSnapshot snapshot;
        double aiProb = 0.0;
        double aiConfidence = 0.0;
        bool criticalAlertActive = false;
        string source;
        string aiClass;
        string aiReason;
        string rootCause;
        string modelReadiness;
        string modelGeneratedAt;
        string recommendedAction;
        string actionTarget;
        string safetyGate;
        string blockedReason;
        string decisionRootCauseDetail;
        string decisionSummary;
        bool safeToHeal = false;
        bool dryRun = true;
        HealPlan healPlan;
        HealVerification healVerification;
        SafetyPolicyResult policyResult;
        RuntimeHealthSample runtimeHealth;
        AdaptiveBaselineResult baseline;
        LowEndAutopilotResult autopilot;
        BackgroundAgentResult backgroundAgent;
        BenchmarkProofResult benchmarkProof;
        int modelFeatureCount = 0;
        int cooldownRemainingSeconds = 0;
        int candidateCount = 0;
        RiskLevel decisionLevel = RiskLevel::Normal;
        double riskScore = 0.0;
        double anomalyScore = 0.0;
        double pressureScore = 0.0;
        double actionConfidence = 0.0;
        double expectedOptimizationGain = 0.0;
        deque<double> cpuHist, memHist, diskHist;
        int cpuTh = g_config.GetInt("CPU_THRESHOLD", 80);
        int memTh = g_config.GetInt("MEM_THRESHOLD", 85);
        int diskTh = g_config.GetInt("DISK_THRESHOLD", 10);
        double warningRiskThreshold = ClampDouble(g_config.GetDouble("DECISION_WARNING_THRESHOLD", 55.0), 0.0, 100.0);
        double criticalRiskThreshold = ClampDouble(g_config.GetDouble("DECISION_CRITICAL_THRESHOLD", 75.0), warningRiskThreshold, 100.0);
        string performanceMode = ResolvePerformanceMode();
        int aiPredictIntervalTicks = max(1, g_config.GetInt("AI_PREDICT_INTERVAL_TICKS", ResolveModeDefaultPredictInterval(performanceMode)));
        int modelCacheTtlTicks = max(aiPredictIntervalTicks, g_config.GetInt("AI_MODEL_CACHE_TTL_TICKS", ResolveModeDefaultCacheTtlTicks(performanceMode)));
        size_t pendingWrites = g_pipeline.PendingCount();
        bool storageReady = g_storage.IsReady();

        {
            lock_guard<mutex> lock(g_dataMutex);
            snapshot.cpuUsage = g_cpu;
            snapshot.memoryUsage = g_mem;
            snapshot.totalMemoryMB = g_totalMemoryMB;
            snapshot.availableMemoryMB = g_availableMemoryMB;
            snapshot.diskFree = g_disk;
            snapshot.netDownKBps = g_netDown;
            snapshot.netUpKBps = g_netUp;
            snapshot.processCount = g_processCount;
            snapshot.scenarioLabel = g_scenarioLabel;
            snapshot.topProcess.pid = g_topProcessPid;
            snapshot.topProcess.name = g_topProcessName;
            snapshot.topProcess.cpuPercent = g_topProcessCpu;
            snapshot.topProcess.memoryMB = g_topProcessMem;
            snapshot.topProcess.privateMemoryMB = g_topProcessPrivateMem;
            snapshot.topProcess.wasteScore = g_topProcessWaste;
            snapshot.topProcess.expectedGainMB = g_topProcessExpectedGain;
            snapshot.topProcess.category = g_topProcessCategory;
            snapshot.topProcess.safety = g_topProcessSafety;
            snapshot.topProcess.recommendation = g_topProcessRecommendation;
            snapshot.topProcess.reason = g_topProcessReason;
            snapshot.intent.userState = g_userState;
            snapshot.intent.foregroundProcess = g_foregroundProcess;
            snapshot.intent.appKind = g_foregroundAppKind;
            snapshot.intent.reason = g_intentReason;
            snapshot.intent.idleSeconds = g_userIdleSeconds;
            snapshot.intent.focusDurationSeconds = g_focusDurationSeconds;
            snapshot.intent.isFullscreen = g_foregroundFullscreen;
            aiProb = g_aiProb;
            aiConfidence = g_aiConfidence;
            riskScore = g_riskScore;
            anomalyScore = g_anomalyScore;
            pressureScore = g_pressureScore;
            criticalAlertActive = g_alert;
            source = g_aiSource;
            aiClass = g_aiClass;
            aiReason = g_aiReason;
            rootCause = g_rootCause;
            modelReadiness = g_modelReadiness;
            modelGeneratedAt = g_modelGeneratedAt;
            modelFeatureCount = g_modelFeatureCount;
            recommendedAction = g_recommendedAction;
            actionTarget = g_actionTarget;
            safetyGate = g_safetyGate;
            blockedReason = g_blockedReason;
            decisionRootCauseDetail = g_decisionRootCauseDetail;
            safeToHeal = g_safeToHeal;
            dryRun = g_dryRun;
            cooldownRemainingSeconds = g_cooldownRemainingSeconds;
            candidateCount = g_candidateCount;
            actionConfidence = g_actionConfidence;
            expectedOptimizationGain = g_expectedOptimizationGain;
            healPlan = g_healPlan;
            healVerification = g_healVerification;
            policyResult = g_safetyPolicyResult;
            runtimeHealth = g_runtimeHealthSample;
            baseline = g_adaptiveBaselineResult;
            autopilot = g_autopilotResult;
            backgroundAgent = g_backgroundAgentResult;
            benchmarkProof = g_benchmarkProofResult;
            decisionSummary = g_decisionSummary;
            decisionLevel = g_decisionLevel;
            cpuHist = g_cpuHist;
            memHist = g_memHist;
            diskHist = g_diskHist;
        }

        DashboardUiState ui;
        ui.system = snapshot;
        ui.runtime = runtimeHealth;
        ui.baseline = baseline;
        ui.autopilot = autopilot;
        ui.agent = backgroundAgent;
        ui.benchmark = benchmarkProof;
        ui.healPlan = healPlan;
        ui.healVerification = healVerification;
        ui.safetyPolicy = policyResult;
        ui.cpuHistory = cpuHist;
        ui.memoryHistory = memHist;
        ui.diskHistory = diskHist;
        {
            lock_guard<mutex> lock(g_dataMutex);
            ui.processes = g_processGenome;
            ui.qoe = g_qoeSample;
            ui.criticality = g_criticalityGraph;
            ui.shadowPolicy = g_shadowPolicyDecision;
            ui.onlinePolicy = g_onlinePolicyDecision;
            ui.latencyHistory = g_inputLatencyHist;
        }
        ui.aiProbability = aiProb;
        ui.aiConfidence = aiConfidence;
        ui.riskScore = riskScore;
        ui.anomalyScore = anomalyScore;
        ui.pressureScore = pressureScore;
        ui.actionConfidence = actionConfidence;
        ui.expectedGainMB = expectedOptimizationGain;
        ui.modelFeatureCount = modelFeatureCount;
        ui.candidateCount = candidateCount;
        ui.cooldownSeconds = cooldownRemainingSeconds;
        ui.cpuThreshold = cpuTh;
        ui.memoryThreshold = memTh;
        ui.diskThreshold = diskTh;
        ui.warningThreshold = static_cast<int>(warningRiskThreshold);
        ui.criticalThreshold = static_cast<int>(criticalRiskThreshold);
        ui.predictIntervalSeconds = aiPredictIntervalTicks;
        ui.cacheSeconds = modelCacheTtlTicks;
        ui.pendingWrites = pendingWrites;
        ui.storageReady = storageReady;
        ui.alertActive = criticalAlertActive;
        ui.dryRun = dryRun;
        ui.actionExecutionEnabled = g_config.GetInt("ACTION_EXECUTION_ENABLED", 0) != 0;
        ui.actionGlobalDisable = g_config.GetInt("ACTION_GLOBAL_DISABLE", 1) != 0;
        ui.onlinePolicyEnabled = g_config.GetInt("ONLINE_POLICY_ENABLED", 0) != 0;
        ui.onlinePolicyPromoted = g_config.GetInt("ONLINE_POLICY_PROMOTED", 0) != 0;
        ui.browserIntegrationEnabled = g_config.GetInt("BROWSER_COOP_ENABLED", 0) != 0;
        ui.bitsIntegrationEnabled = g_config.GetInt("BITS_COOP_ENABLED", 0) != 0;
        ui.prefetchEnabled = g_config.GetInt("PREDICTIVE_PREFETCH_ENABLED", 0) != 0;
        ui.riskLevel = DecisionEngine::ToString(decisionLevel);
        ui.aiSource = source;
        ui.aiClass = aiClass;
        ui.aiReason = aiReason;
        ui.modelReadiness = modelReadiness;
        ui.modelGeneratedAt = modelGeneratedAt;
        ui.decisionSummary = decisionSummary;
        ui.decisionReason = g_decisionReason;
        ui.rootCause = rootCause;
        ui.rootCauseDetail = decisionRootCauseDetail;
        ui.recommendedAction = recommendedAction;
        ui.actionTarget = actionTarget;
        ui.safetyGate = safetyGate;
        ui.blockedReason = blockedReason;
        ui.performanceMode = FormatModeLabel(performanceMode);
        ui.policyMode = ToUpperAscii(ToDisplayToken(g_config.GetString("IMPACT_POLICY_MODE", "SHADOW")));
        ui.modelVersion = modelFeatureCount > 0
            ? to_string(modelFeatureCount) + "F " + ToUpperAscii(ToDisplayToken(modelReadiness))
            : ToUpperAscii(ToDisplayToken(modelReadiness));

        const DashboardUiFonts fonts{gFontTitle, gFontSection, gFontValue, gFontSmall};
        DrawModernDashboard(memDC, client, ui, g_dashboardView, fonts);
        BitBlt(hdc, 0, 0, client.right, client.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        RECT client;
        GetClientRect(hwnd, &client);
        const DashboardView nextView = HitTestDashboardNavigation(client, x, y, g_dashboardView);
        if (nextView != g_dashboardView) {
            g_dashboardView = nextView;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_TRAYICON_MESSAGE:
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowDashboardWindow(hwnd);
        } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPEN_DASHBOARD:
            ShowDashboardWindow(hwnd);
            return 0;
        case ID_TRAY_QUICK_RESTORE: {
            string restoreStatus;
            bool restoreAvailable = false;
            {
                lock_guard<mutex> lock(g_dataMutex);
                restoreAvailable = g_autopilotResult.quickRestoreAvailable;
                g_quickRestoreStatus = restoreAvailable ? "DRY_RUN_RESET" : "NO_EXECUTED_ACTIONS";
                restoreStatus = g_quickRestoreStatus;
            }
            g_quickRestoreRequests.store(1);
            AppendRuntimeLog("quick_restore_requested", {
                {"status", restoreStatus},
                {"restore_available", restoreAvailable ? "1" : "0"},
                {"execution", "none_dry_run_only"},
            });
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 1180;
        info->ptMinTrackSize.y = 720;
        return 0;
    }

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED &&
            ConfigBool("BACKGROUND_AGENT_ENABLED", true) &&
            ConfigBool("AGENT_HIDE_ON_MINIMIZE", true) &&
            g_trayIconInstalled) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (ConfigBool("BACKGROUND_AGENT_ENABLED", true) &&
            ConfigBool("AGENT_CLOSE_TO_TRAY", true) &&
            g_trayIconInstalled) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        if (g_runtimeOrchestrator) {
            g_runtimeOrchestrator->Stop();
        } else {
            g_running = false;
            if (g_monitorThread.joinable()) g_monitorThread.join();
        }
        StopInferenceService();
        g_pipeline.Stop();
        if (gFontTitle) DeleteObject(gFontTitle);
        if (gFontSection) DeleteObject(gFontSection);
        if (gFontValue) DeleteObject(gFontValue);
        if (gFontSmall) DeleteObject(gFontSmall);
        g_storage.Close();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int main() {
    const fs::path exeDir = GetExecutableDir();
    const string configPath = ResolveExistingPathString({
        exeDir / L"config.txt",
        exeDir.parent_path() / L"config.txt",
        exeDir.parent_path().parent_path() / L"config.txt",
        fs::path("config.txt"),
    });
    g_config.LoadFromFile(configPath);
    const RuntimeMode configuredRuntimeMode = ParseRuntimeMode(g_config.GetString("RUNTIME_MODE", "MONITOR_ONLY"));
    g_runtimeStateStore.SetMode(configuredRuntimeMode);
    g_agentStartMinimized = CommandLineHasFlag(L"--agent");
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    g_storage.Open((exeDir / L"monitor.db").string());
    g_pipeline.Start(
        &g_storage,
        static_cast<size_t>(max(1, g_config.GetInt("PIPELINE_BATCH_SIZE", 8))),
        milliseconds(max(100, g_config.GetInt("PIPELINE_FLUSH_MS", 2000)))
    );

    HINSTANCE hInstance = GetModuleHandle(NULL);
    const wchar_t CLASS_NAME[] = L"MonitorWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        MessageBoxA(NULL, "Failed to register window class.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    gFontTitle = CreateFontW(-27, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable Display");
    gFontSection = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable Text");
    gFontValue = CreateFontW(-34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable Display");
    gFontSmall = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable Text");

    RECT workArea{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        workArea = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const int workWidth = static_cast<int>(workArea.right - workArea.left);
    const int workHeight = static_cast<int>(workArea.bottom - workArea.top);
    const int windowWidth = max(1180, min(1600, workWidth - 24));
    const int windowHeight = max(720, min(940, workHeight - 24));
    const int windowX = static_cast<int>(workArea.left) + max(0, (workWidth - windowWidth) / 2);
    const int windowY = static_cast<int>(workArea.top) + max(0, (workHeight - windowHeight) / 2);

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"PredictiveAutoHeal Control Center",
        WS_OVERLAPPEDWINDOW,
        windowX, windowY, windowWidth, windowHeight,
        NULL, NULL, hInstance, NULL
    );
    if (!hwnd) {
        MessageBoxA(NULL, "Failed to create window.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    InstallTrayIcon(hwnd);
    const bool startHidden =
        ConfigBool("BACKGROUND_AGENT_ENABLED", true) &&
        (g_agentStartMinimized || ConfigBool("AGENT_START_MINIMIZED", false));
    ShowWindow(hwnd, startHidden ? SW_HIDE : SW_SHOW);
    if (!startHidden) UpdateWindow(hwnd);
    AppendRuntimeLog("background_agent_started", {
        {"start_hidden", startHidden ? "1" : "0"},
        {"tray_icon", g_trayIconInstalled ? "1" : "0"},
        {"start_on_boot", IsStartupAgentRegistered() ? "1" : "0"},
    });

    std::vector<std::unique_ptr<IRuntimeComponent>> runtimeComponents;
    runtimeComponents.push_back(std::make_unique<ThreadedRuntimeComponent>(
        RuntimeComponent::Collectors,
        [hwnd] {
            g_running = true;
            g_monitorThread = thread(MonitorThread, hwnd);
            return RuntimeStatus{};
        },
        [] {
            g_running = false;
            if (g_monitorThread.joinable()) g_monitorThread.join();
        }
    ));
    g_runtimeOrchestrator = std::make_unique<RuntimeOrchestrator>(g_runtimeStateStore, std::move(runtimeComponents));
    const RuntimeStatus runtimeStart = g_runtimeOrchestrator->Start();
    if (!runtimeStart.Succeeded()) {
        MessageBoxA(hwnd, runtimeStart.detail.c_str(), "Runtime startup failed", MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd);
    }

    const long long startupHealthTimestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    g_runtimeStateStore.SetComponentHealth({
        RuntimeComponent::Storage,
        g_storage.IsReady() ? ComponentHealthState::Healthy : ComponentHealthState::Unavailable,
        g_storage.IsReady() ? RuntimeStatusCode::Ok : RuntimeStatusCode::StorageUnavailable,
        g_storage.IsReady() ? "SQLite ready" : "SQLite unavailable; monitoring continues without persistence",
        startupHealthTimestamp
    });
    g_runtimeStateStore.SetComponentHealth({
        RuntimeComponent::Inference,
        ComponentHealthState::Degraded,
        RuntimeStatusCode::InferenceUnavailable,
        "Legacy Python inference is research-only and unavailable until a model window exists",
        startupHealthTimestamp
    });
    g_runtimeStateStore.SetComponentHealth({
        RuntimeComponent::Policy,
        ComponentHealthState::Healthy,
        RuntimeStatusCode::Ok,
        "Policy initialized with automatic actions disabled",
        startupHealthTimestamp
    });
    g_runtimeStateStore.SetComponentHealth({
        RuntimeComponent::Certificate,
        ComponentHealthState::Unavailable,
        RuntimeStatusCode::InvalidConfiguration,
        "No Phase 4 reliability certificate is installed",
        startupHealthTimestamp
    });
    g_runtimeStateStore.SetComponentHealth({
        RuntimeComponent::Ui,
        ComponentHealthState::Healthy,
        RuntimeStatusCode::Ok,
        "Win32 dashboard window is active",
        startupHealthTimestamp
    });

    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
