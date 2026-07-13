#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

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
#include "AutoHealPlanner.h"
#include "BackgroundAgent.h"
#include "BrowserIntegrationBridge.h"
#include "BenchmarkProof.h"
#include "CooperativeIntegrations.h"
#include "AppConfig.h"
#include "DecisionEngine.h"
#include "HealingVerifier.h"
#include "ImpactLearning.h"
#include "LowEndAutopilot.h"
#include "MetricsPipeline.h"
#include "PerformanceIntelligence.h"
#include "RuntimeFoundation.h"
#include "RuntimeHealth.h"
#include "SafeOnlinePolicy.h"
#include "SafetyPolicy.h"
#include "MetricsStorage.h"
#include "ModernDashboardUI.h"
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

template <typename T>
string DequeToJsonArray(const deque<T>& values) {
    ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) oss << ",";
        oss << fixed << setprecision(4) << values[i];
    }
    oss << "]";
    return oss.str();
}

void PushHistory(deque<double>& hist, double value, size_t maxSize) {
    hist.push_back(value);
    if (hist.size() > maxSize) hist.pop_front();
}

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

struct ModelPrediction {
    double risk = -1.0;
    double confidence = 0.0;
    string predictedClass = "UNKNOWN";
    string reason = "N/A";
    string rootCause = "unknown";
    string rootSeverity = "normal";
    string modelReadiness = "unknown";
    string modelGeneratedAt = "unknown";
    string recommendedAction = "monitor_only";
    bool safeToHeal = false;
    int featureCount = 0;
};

string ExtractJsonString(const string& text, const string& key, const string& fallback = "") {
    const string needle = "\"" + key + "\":";
    size_t pos = text.find(needle);
    if (pos == string::npos) return fallback;
    pos += needle.size();
    while (pos < text.size() && isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size() || text[pos] != '"') return fallback;
    ++pos;

    string value;
    bool escaped = false;
    for (; pos < text.size(); ++pos) {
        char ch = text[pos];
        if (escaped) {
            value.push_back(ch);
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            break;
        } else {
            value.push_back(ch);
        }
    }
    return value.empty() ? fallback : value;
}

double ExtractJsonDouble(const string& text, const string& key, double fallback = 0.0) {
    const string needle = "\"" + key + "\":";
    size_t pos = text.find(needle);
    if (pos == string::npos) return fallback;
    pos += needle.size();
    while (pos < text.size() && isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    size_t end = pos;
    while (end < text.size() && (isdigit(static_cast<unsigned char>(text[end])) || text[end] == '-' || text[end] == '+' || text[end] == '.')) {
        ++end;
    }
    try {
        return stod(text.substr(pos, end - pos));
    } catch (...) {
        return fallback;
    }
}

bool ExtractJsonBool(const string& text, const string& key, bool fallback = false) {
    const string needle = "\"" + key + "\":";
    size_t pos = text.find(needle);
    if (pos == string::npos) return fallback;
    pos += needle.size();
    while (pos < text.size() && isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (text.compare(pos, 4, "true") == 0) return true;
    if (text.compare(pos, 5, "false") == 0) return false;
    return fallback;
}

ModelPrediction ParseModelPredictionText(const string& text) {
    ModelPrediction failed;
    try {
        ModelPrediction prediction;
        if (!text.empty() && text.front() == '{') {
            prediction.risk = ExtractJsonDouble(text, "risk", ExtractJsonDouble(text, "probability", -1.0));
            prediction.confidence = ExtractJsonDouble(text, "confidence", 0.0);
            prediction.predictedClass = ExtractJsonString(text, "class", "UNKNOWN");
            prediction.reason = ExtractJsonString(text, "reason", "N/A");
            prediction.rootCause = ExtractJsonString(text, "primary", "unknown");
            prediction.rootSeverity = ExtractJsonString(text, "severity", "normal");
            prediction.modelReadiness = ExtractJsonString(text, "model_readiness", "unknown");
            prediction.modelGeneratedAt = ExtractJsonString(text, "model_generated_at", "unknown");
            prediction.featureCount = static_cast<int>(ExtractJsonDouble(text, "feature_count", 0.0));
            prediction.recommendedAction = ExtractJsonString(text, "recommended_action", "monitor_only");
            prediction.safeToHeal = ExtractJsonBool(text, "safe_to_heal", false);
        } else {
            prediction.risk = stod(text);
            prediction.confidence = 55.0;
            prediction.predictedClass = prediction.risk >= 75.0 ? "CRITICAL" : (prediction.risk >= 55.0 ? "WARNING" : "NORMAL");
            prediction.reason = "legacy model probability";
            prediction.rootCause = "legacy_probability";
        }

        if (prediction.risk < 0.0 || prediction.risk > 100.0) return failed;
        prediction.confidence = ClampDouble(prediction.confidence, 0.0, 100.0);
        return prediction;
    } catch (...) {
        return failed;
    }
}

void WriteRuntimeFeaturesJson(
    const wstring& outputPath,
    const deque<double>& cpuHist,
    const deque<double>& memHist,
    const deque<double>& diskHist,
    const deque<double>& netHist,
    const deque<double>& processHist,
    const deque<double>& topCpuHist,
    const deque<double>& topMemHist,
    int cpuTh,
    int memTh,
    int diskTh
) {
    const wstring tempPath = outputPath + L".tmp";
    ofstream file(tempPath, ios::trunc);
    if (!file) return;

    file << "{\n";
    file << "  \"window\": " << AI_WINDOW << ",\n";
    file << "  \"cpu_threshold\": " << cpuTh << ",\n";
    file << "  \"mem_threshold\": " << memTh << ",\n";
    file << "  \"disk_threshold\": " << diskTh << ",\n";
    file << "  \"cpu_history\": " << DequeToJsonArray(cpuHist) << ",\n";
    file << "  \"mem_history\": " << DequeToJsonArray(memHist) << ",\n";
    file << "  \"disk_history\": " << DequeToJsonArray(diskHist) << ",\n";
    file << "  \"net_history\": " << DequeToJsonArray(netHist) << ",\n";
    file << "  \"process_history\": " << DequeToJsonArray(processHist) << ",\n";
    file << "  \"top_process_cpu_history\": " << DequeToJsonArray(topCpuHist) << ",\n";
    file << "  \"top_process_mem_history\": " << DequeToJsonArray(topMemHist) << "\n";
    file << "}\n";
    file.close();

    if (!MoveFileExW(tempPath.c_str(), outputPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
    }
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

void MonitorThread(HWND hwnd) {
    WindowsMetricsCollector collector;
    if (!collector.Initialize()) return;
    WorkloadPhaseDetector workloadDetector;
    QoeTelemetryCollector qoeCollector;
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
    onlinePolicyConfig.onlineEnabled = RuntimeModePermitsAutomaticActions(runtimeMode) &&
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
    decisionPolicy.autoHealEnabled = RuntimeModePermitsAutomaticActions(runtimeMode) &&
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

    while (g_running) {
        this_thread::sleep_for(seconds(1));
        if (!g_running) break;

        SystemSnapshot snapshot = collector.Collect();
        snapshot.scenarioLabel = ReadScenarioLabel();
        WorkloadPhase workloadPhase = workloadDetector.Detect(snapshot);
        QoeTelemetrySample qoeSample;
        if (qoeCollectorReady) {
            qoeSample = qoeCollector.Capture(snapshot, workloadPhase);
            workloadPhase = workloadDetector.Detect(snapshot, &qoeSample);
            qoeSample.workload = workloadPhase;
        } else {
            qoeSample.timestampMs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            qoeSample.workload = workloadPhase;
            qoeSample.foregroundPid = snapshot.intent.foregroundPid;
            qoeSample.foregroundProcess = snapshot.intent.foregroundProcess;
            qoeSample.availability = "collector_unavailable:" + qoeInitializationError;
        }
        PerformanceCriticalityGraph criticalityGraph = criticalityEngine.Build(snapshot, workloadPhase);
        WorkloadProtectionResult workloadProtection = workloadProtectionEngine.Build(snapshot, criticalityGraph, workloadPhase);
        const int qoeWriteIntervalTicks = max(1, qoeSample.recommendedSampleIntervalMs / 1000);
        if (qoeJournalReady && (g_tick % qoeWriteIntervalTicks) == 0) {
            string qoeWriteError;
            qoeJournal.Save(qoeSample, criticalityGraph, qoeWriteError);
            if ((g_tick % 300) == 0) qoeJournal.EnforceRetention(10000, 50000, qoeWriteError);
        }

        deque<double> cpuCopy, memCopy, diskCopy, netCopy, processCopy, topCpuCopy, topMemCopy;
        double aiProbLocal = 0.0;
        double aiConfidenceLocal = 0.0;
        string sourceLocal = "WARMING UP";
        string reasonLocal = "Collecting baseline";
        bool alertLocal = false;
        DecisionResult decisionResult;

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

            PushHistory(g_cpuHist, snapshot.cpuUsage, HISTORY_SIZE);
            PushHistory(g_memHist, snapshot.memoryUsage, HISTORY_SIZE);
            PushHistory(g_diskHist, snapshot.diskFree, HISTORY_SIZE);
            PushHistory(g_netHist, snapshot.netDownKBps + snapshot.netUpKBps, HISTORY_SIZE);
            PushHistory(g_processHist, static_cast<double>(snapshot.processCount), HISTORY_SIZE);
            PushHistory(g_topCpuHist, snapshot.topProcess.cpuPercent, HISTORY_SIZE);
            PushHistory(g_topMemHist, snapshot.topProcess.memoryMB, HISTORY_SIZE);
            PushHistory(g_inputLatencyHist, qoeSample.inputAvailable ? qoeSample.inputResponseMs : snapshot.netDownKBps,
                        HISTORY_SIZE);

            cpuCopy = g_cpuHist;
            memCopy = g_memHist;
            diskCopy = g_diskHist;
            netCopy = g_netHist;
            processCopy = g_processHist;
            topCpuCopy = g_topCpuHist;
            topMemCopy = g_topMemHist;
        }

        g_pipeline.Enqueue(snapshot);
        AdaptiveBaselineResult baselineResult = g_adaptiveBaseline.EvaluateAndUpdate(snapshot, baselineMinSamples, baselineSensitivity);
        WriteRuntimeFeaturesJson(runtimeFeaturesPath, cpuCopy, memCopy, diskCopy, netCopy, processCopy, topCpuCopy, topMemCopy, cpuTh, memTh, diskTh);

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
        }

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
        g_running = false;
        if (g_monitorThread.joinable()) {
            g_monitorThread.join();
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

    g_monitorThread = thread(MonitorThread, hwnd);

    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
