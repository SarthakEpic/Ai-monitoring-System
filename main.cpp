#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
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

#include "AppConfig.h"
#include "DecisionEngine.h"
#include "MetricsPipeline.h"
#include "MetricsStorage.h"
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

enum class DashboardView {
    Overview,
    Reliability,
    Runtime,
    AutoHeal,
};

double g_cpu = 0.0;
double g_mem = 0.0;
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
int g_processCount = 0;
unsigned long g_topProcessPid = 0;

bool g_alert = false;
string g_aiSource = "WARMING UP";
string g_aiClass = "UNKNOWN";
string g_aiReason = "Collecting baseline";
string g_recommendedAction = "monitor_only";
string g_scenarioLabel = "auto";
string g_topProcessName = "N/A";
string g_decisionSummary = "System stable";
string g_decisionReason = "Collecting baseline";
RiskLevel g_decisionLevel = RiskLevel::Normal;
bool g_safeToHeal = false;

deque<double> g_cpuHist;
deque<double> g_memHist;
deque<double> g_diskHist;
deque<double> g_netHist;
deque<double> g_processHist;

mutex g_dataMutex;

HFONT gFontTitle = NULL;
HFONT gFontSection = NULL;
HFONT gFontValue = NULL;
HFONT gFontSmall = NULL;

AppConfig g_config;
MetricsStorage g_storage;
MetricsPipeline g_pipeline;
DecisionEngine g_decisionEngine;
atomic<bool> g_running = true;
thread g_monitorThread;
long long g_tick = 0;
DashboardView g_dashboardView = DashboardView::Overview;

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

string FormatRate(double kbps) {
    ostringstream oss;
    oss << fixed << setprecision(1);
    if (kbps >= 1024.0) {
        oss << (kbps / 1024.0) << " MB/s";
    } else {
        oss << kbps << " KB/s";
    }
    return oss.str();
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

COLORREF LevelFillColor(RiskLevel level) {
    switch (level) {
    case RiskLevel::Critical:
        return RGB(64, 24, 32);
    case RiskLevel::Warning:
        return RGB(84, 58, 12);
    case RiskLevel::Normal:
    default:
        return RGB(25, 51, 44);
    }
}

COLORREF LevelAccentColor(RiskLevel level) {
    switch (level) {
    case RiskLevel::Critical:
        return RGB(231, 76, 60);
    case RiskLevel::Warning:
        return RGB(241, 196, 15);
    case RiskLevel::Normal:
    default:
        return RGB(46, 204, 113);
    }
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
    string recommendedAction = "monitor_only";
    bool safeToHeal = false;
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

void WriteRuntimeFeaturesJson(
    const wstring& outputPath,
    const deque<double>& cpuHist,
    const deque<double>& memHist,
    const deque<double>& diskHist,
    const deque<double>& netHist,
    const deque<double>& processHist,
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
    file << "  \"process_history\": " << DequeToJsonArray(processHist) << "\n";
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

    try {
        ModelPrediction prediction;
        if (!text.empty() && text.front() == '{') {
            prediction.risk = ExtractJsonDouble(text, "risk", ExtractJsonDouble(text, "probability", -1.0));
            prediction.confidence = ExtractJsonDouble(text, "confidence", 0.0);
            prediction.predictedClass = ExtractJsonString(text, "class", "UNKNOWN");
            prediction.reason = ExtractJsonString(text, "reason", "N/A");
            prediction.recommendedAction = ExtractJsonString(text, "recommended_action", "monitor_only");
            prediction.safeToHeal = ExtractJsonBool(text, "safe_to_heal", false);
        } else {
            prediction.risk = stod(text);
            prediction.confidence = 55.0;
            prediction.predictedClass = prediction.risk >= 75.0 ? "CRITICAL" : (prediction.risk >= 55.0 ? "WARNING" : "NORMAL");
            prediction.reason = "legacy model probability";
        }

        if (prediction.risk < 0.0 || prediction.risk > 100.0) return failed;
        prediction.confidence = ClampDouble(prediction.confidence, 0.0, 100.0);
        return prediction;
    } catch (...) {
        return failed;
    }
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

void DrawTextAt(HDC hdc, int x, int y, const string& text, COLORREF color, HFONT font) {
    HGDIOBJ oldFont = SelectObject(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    TextOutA(hdc, x, y, text.c_str(), static_cast<int>(text.size()));
    SelectObject(hdc, oldFont);
}

void DrawRoundedPanel(HDC hdc, const RECT& rc, COLORREF fill, COLORREF border, int radius = 18) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);

    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);

    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawProgressBar(HDC hdc, int x, int y, int w, int h, double value, COLORREF fillColor) {
    value = ClampDouble(value, 0.0, 100.0);

    RECT bg{ x, y, x + w, y + h };
    RECT fg{ x, y, x + static_cast<int>((value / 100.0) * w), y + h };

    HBRUSH bgBrush = CreateSolidBrush(RGB(42, 49, 62));
    HBRUSH fgBrush = CreateSolidBrush(fillColor);

    FillRect(hdc, &bg, bgBrush);
    FillRect(hdc, &fg, fgBrush);

    DeleteObject(bgBrush);
    DeleteObject(fgBrush);
}

void DrawMetricCard(HDC hdc, int x, int y, int w, int h,
                    const string& title,
                    const string& valueText,
                    const string& subText,
                    double progressValue,
                    COLORREF accent,
                    bool alertCard) {
    RECT rc{ x, y, x + w, y + h };

    COLORREF fill = alertCard ? RGB(64, 24, 32) : RGB(26, 32, 43);
    COLORREF border = alertCard ? RGB(231, 76, 60) : accent;
    DrawRoundedPanel(hdc, rc, fill, border, 24);

    DrawTextAt(hdc, x + 18, y + 16, title, RGB(225, 232, 242), gFontSection);
    DrawTextAt(hdc, x + 18, y + 48, valueText, RGB(255, 255, 255), gFontValue);
    DrawTextAt(hdc, x + 18, y + h - 48, subText, RGB(176, 186, 199), gFontSmall);

    DrawProgressBar(hdc, x + 18, y + h - 26, w - 36, 10, progressValue, accent);
}

void DrawSectionPanel(HDC hdc, const RECT& rc, const string& title, COLORREF border = RGB(56, 66, 82)) {
    DrawRoundedPanel(hdc, rc, RGB(18, 22, 31), border, 18);
    DrawTextAt(hdc, rc.left + 14, rc.top + 12, title, RGB(235, 240, 248), gFontSection);
}

void DrawDashboardTab(HDC hdc, int x, int y, const string& text, bool active) {
    int w = static_cast<int>(text.size()) * 9 + 28;
    RECT rc{ x, y, x + w, y + 34 };
    DrawRoundedPanel(hdc, rc, active ? RGB(30, 44, 62) : RGB(18, 22, 31), active ? RGB(52, 152, 219) : RGB(55, 65, 82), 14);
    DrawTextAt(hdc, x + 14, y + 9, text, active ? RGB(230, 242, 255) : RGB(145, 155, 170), gFontSmall);
}

RECT DashboardTabRect(int mainX, int y, DashboardView view) {
    switch (view) {
    case DashboardView::Overview:
        return RECT{ mainX, y, mainX + 124, y + 34 };
    case DashboardView::Reliability:
        return RECT{ mainX + 134, y, mainX + 318, y + 34 };
    case DashboardView::Runtime:
        return RECT{ mainX + 328, y, mainX + 444, y + 34 };
    case DashboardView::AutoHeal:
        return RECT{ mainX + 454, y, mainX + 590, y + 34 };
    default:
        return RECT{ mainX, y, mainX, y };
    }
}

void DrawDashboardTabRect(HDC hdc, const RECT& rc, const string& text, bool active) {
    DrawRoundedPanel(hdc, rc, active ? RGB(30, 44, 62) : RGB(18, 22, 31), active ? RGB(52, 152, 219) : RGB(55, 65, 82), 14);
    DrawTextAt(hdc, rc.left + 14, rc.top + 9, text, active ? RGB(230, 242, 255) : RGB(145, 155, 170), gFontSmall);
}

bool PtInRectSimple(const RECT& rc, int x, int y) {
    return x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom;
}

void DrawGraph(HDC hdc, const RECT& rc,
               const deque<double>& cpuHist,
               const deque<double>& memHist,
               const deque<double>& diskHist) {
    DrawRoundedPanel(hdc, rc, RGB(18, 22, 31), RGB(56, 66, 82), 24);

    DrawTextAt(hdc, rc.left + 16, rc.top + 14, "Live Trend Graph", RGB(235, 240, 248), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 38, "Blue = CPU   Green = Memory   Yellow = Disk", RGB(155, 165, 180), gFontSmall);

    RECT inner{ rc.left + 18, rc.top + 68, rc.right - 18, rc.bottom - 20 };

    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(35, 43, 54));
    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(80, 90, 104));

    HGDIOBJ oldPen = SelectObject(hdc, gridPen);

    for (int i = 0; i <= 4; ++i) {
        int y = inner.top + (i * (inner.bottom - inner.top) / 4);
        MoveToEx(hdc, inner.left, y, NULL);
        LineTo(hdc, inner.right, y);

        string label = to_string(100 - i * 25);
        DrawTextAt(hdc, inner.left - 28, y - 8, label, RGB(120, 130, 145), gFontSmall);
    }

    SelectObject(hdc, axisPen);
    Rectangle(hdc, inner.left, inner.top, inner.right, inner.bottom);

    auto drawSeries = [&](const deque<double>& hist, COLORREF color) {
        if (hist.size() < 2) return;

        HPEN pen = CreatePen(PS_SOLID, 3, color);
        HGDIOBJ old = SelectObject(hdc, pen);

        int n = static_cast<int>(hist.size());
        int w = inner.right - inner.left;
        int h = inner.bottom - inner.top;

        for (int i = 0; i < n; ++i) {
            double v = ClampDouble(hist[i], 0.0, 100.0);
            int x = inner.left + (i * w) / max(1, n - 1);
            int y = inner.bottom - static_cast<int>((v / 100.0) * h);

            if (i == 0) MoveToEx(hdc, x, y, NULL);
            else LineTo(hdc, x, y);
        }

        SelectObject(hdc, old);
        DeleteObject(pen);
    };

    drawSeries(cpuHist, RGB(52, 152, 219));
    drawSeries(memHist, RGB(46, 204, 113));
    drawSeries(diskHist, RGB(241, 196, 15));

    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);
    DeleteObject(axisPen);
}

void DrawReliabilityPanel(HDC hdc, const RECT& rc,
                          double aiProb,
                          double aiConfidence,
                          const string& source,
                          const string& aiClass,
                          const string& aiReason,
                          RiskLevel decisionLevel) {
    DrawSectionPanel(hdc, rc, "AI Reliability", LevelAccentColor(decisionLevel));

    DrawTextAt(hdc, rc.left + 16, rc.top + 48, "Risk", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 72, to_string(static_cast<int>(aiProb)) + "%", RGB(255, 255, 255), gFontValue);
    DrawProgressBar(hdc, rc.left + 16, rc.top + 118, rc.right - rc.left - 32, 12, aiProb, LevelAccentColor(decisionLevel));

    DrawTextAt(hdc, rc.left + 16, rc.top + 152, "Confidence", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 176, to_string(static_cast<int>(aiConfidence)) + "%", RGB(235, 240, 248), gFontSection);
    DrawProgressBar(hdc, rc.left + 16, rc.top + 204, rc.right - rc.left - 32, 10, aiConfidence, RGB(52, 152, 219));

    DrawTextAt(hdc, rc.left + 16, rc.top + 238, "Source", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 90, rc.top + 238, source, RGB(180, 220, 255), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 264, "Class", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 90, rc.top + 264, aiClass, RGB(235, 240, 248), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 300, "Why", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 58, rc.top + 300, ShortenText(aiReason, 42), RGB(235, 240, 248), gFontSmall);
}

void DrawDecisionPanel(HDC hdc, const RECT& rc,
                       RiskLevel decisionLevel,
                       double riskScore,
                       double anomalyScore,
                       double pressureScore,
                       const string& decisionSummary) {
    DrawSectionPanel(hdc, rc, "Decision Engine", LevelAccentColor(decisionLevel));
    RECT badge{ rc.left + 14, rc.top + 48, rc.right - 14, rc.top + 84 };
    DrawRoundedPanel(hdc, badge, LevelFillColor(decisionLevel), LevelAccentColor(decisionLevel), 14);
    DrawTextAt(hdc, badge.left + 14, badge.top + 9, DecisionEngine::ToString(decisionLevel), RGB(255, 255, 255), gFontSection);
    DrawTextAt(hdc, rc.left + 14, rc.top + 108, "Risk " + to_string(static_cast<int>(riskScore)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 112, rc.top + 108, "Anomaly " + to_string(static_cast<int>(anomalyScore)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 134, "Pressure " + to_string(static_cast<int>(pressureScore)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 160, ShortenText(decisionSummary, 38), RGB(180, 220, 255), gFontSmall);
}

void DrawRuntimePanel(HDC hdc, const RECT& rc,
                      const string& performanceMode,
                      int aiPredictIntervalTicks,
                      int modelCacheTtlTicks,
                      size_t pendingWrites,
                      bool storageReady) {
    DrawSectionPanel(hdc, rc, "Runtime & Storage", storageReady ? RGB(46, 204, 113) : RGB(231, 76, 60));
    DrawTextAt(hdc, rc.left + 14, rc.top + 50, "Mode", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 86, rc.top + 50, FormatModeLabel(performanceMode), RGB(235, 240, 248), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 78, "Model", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 86, rc.top + 78, "every " + to_string(aiPredictIntervalTicks) + "s", RGB(235, 240, 248), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 106, "Cache", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 86, rc.top + 106, to_string(modelCacheTtlTicks) + "s", RGB(235, 240, 248), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 134, "SQLite", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 86, rc.top + 134, storageReady ? "ACTIVE" : "OFFLINE", storageReady ? RGB(120, 220, 160) : RGB(240, 110, 95), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 162, "Queue", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 86, rc.top + 162, to_string(pendingWrites), RGB(235, 240, 248), gFontSmall);
}

void DrawAutoHealPanel(HDC hdc, const RECT& rc,
                       const string& recommendedAction,
                       bool safeToHeal,
                       const SystemSnapshot& snapshot) {
    DrawSectionPanel(hdc, rc, "Auto-Heal Readiness", safeToHeal ? RGB(46, 204, 113) : RGB(241, 196, 15));
    RECT status{ rc.left + 14, rc.top + 48, rc.right - 14, rc.top + 88 };
    DrawRoundedPanel(hdc, status, safeToHeal ? RGB(25, 51, 44) : RGB(68, 50, 20), safeToHeal ? RGB(46, 204, 113) : RGB(241, 196, 15), 14);
    DrawTextAt(hdc, status.left + 14, status.top + 11, string("HEAL READY: ") + (safeToHeal ? "YES" : "NO"), RGB(255, 255, 255), gFontSection);
    DrawTextAt(hdc, rc.left + 14, rc.top + 112, "Action", RGB(160, 170, 185), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 138, ShortenText(ToUpperAscii(ToDisplayToken(recommendedAction)), 30), RGB(235, 240, 248), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 164, "Top process: " + ShortenText(snapshot.topProcess.name, 24), RGB(170, 180, 195), gFontSmall);
}

void DrawThresholdPanel(HDC hdc, const RECT& rc,
                        int cpuTh, int memTh, int diskTh,
                        double aiAlertTh,
                        double warningRiskThreshold,
                        double criticalRiskThreshold) {
    DrawSectionPanel(hdc, rc, "Thresholds", RGB(90, 108, 132));
    DrawTextAt(hdc, rc.left + 14, rc.top + 52, "CPU " + to_string(cpuTh) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 112, rc.top + 52, "MEM " + to_string(memTh) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 84, "DISK " + to_string(diskTh) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 112, rc.top + 84, "AI " + to_string(static_cast<int>(aiAlertTh)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 14, rc.top + 124, "WARN " + to_string(static_cast<int>(warningRiskThreshold)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 112, rc.top + 124, "CRIT " + to_string(static_cast<int>(criticalRiskThreshold)) + "%", RGB(170, 180, 195), gFontSmall);
}

void DrawPlaceholderWidePanel(HDC hdc, const RECT& rc, const string& title, const string& line1, const string& line2) {
    DrawSectionPanel(hdc, rc, title, RGB(52, 152, 219));
    DrawTextAt(hdc, rc.left + 18, rc.top + 56, line1, RGB(210, 220, 235), gFontSection);
    DrawTextAt(hdc, rc.left + 18, rc.top + 92, line2, RGB(155, 165, 180), gFontSmall);
}

void DrawInsightPanel(HDC hdc, const RECT& rc,
                      const SystemSnapshot& snapshot,
                      double aiProb, const string& source,
                      double aiConfidence,
                      const string& aiClass,
                      const string& aiReason,
                      const string& recommendedAction,
                      bool safeToHeal,
                      RiskLevel decisionLevel,
                      double riskScore,
                      double anomalyScore,
                      double pressureScore,
                      const string& decisionSummary,
                      int cpuTh, int memTh, int diskTh,
                      double aiAlertTh,
                      double warningRiskThreshold,
                      double criticalRiskThreshold,
                      const string& performanceMode,
                      int aiPredictIntervalTicks,
                      int modelCacheTtlTicks,
                      size_t pendingWrites,
                      bool storageReady) {
    COLORREF bg = (decisionLevel == RiskLevel::Normal) ? RGB(20, 26, 36) : LevelFillColor(decisionLevel);
    COLORREF border = (decisionLevel == RiskLevel::Normal) ? RGB(62, 76, 94) : LevelAccentColor(decisionLevel);
    DrawRoundedPanel(hdc, rc, bg, border, 24);

    DrawTextAt(hdc, rc.left + 16, rc.top + 14, "AI Insights", RGB(235, 240, 248), gFontSection);

    RECT badge{ rc.left + 16, rc.top + 48, rc.right - 16, rc.top + 92 };
    DrawRoundedPanel(hdc, badge, LevelAccentColor(decisionLevel), LevelAccentColor(decisionLevel), 18);
    DrawTextAt(hdc, rc.left + 30, rc.top + 60, DecisionEngine::ToString(decisionLevel), RGB(255, 255, 255), gFontSection);

    DrawTextAt(hdc, rc.left + 16, rc.top + 112, "AI Risk", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 134, to_string(static_cast<int>(aiProb)) + "%", RGB(255, 255, 255), gFontValue);
    DrawTextAt(hdc, rc.left + 120, rc.top + 142, "CONF " + to_string(static_cast<int>(aiConfidence)) + "%", RGB(180, 220, 255), gFontSmall);
    DrawProgressBar(hdc, rc.left + 16, rc.top + 178, rc.right - rc.left - 32, 14, aiProb, LevelAccentColor(decisionLevel));

    RECT modelBox{ rc.left + 16, rc.top + 214, rc.right - 16, rc.top + 318 };
    DrawRoundedPanel(hdc, modelBox, RGB(18, 24, 34), RGB(55, 70, 88), 16);
    DrawTextAt(hdc, modelBox.left + 14, modelBox.top + 12, "Model Health", RGB(235, 240, 248), gFontSection);
    DrawTextAt(hdc, modelBox.left + 14, modelBox.top + 42, "SRC " + source + "   CLASS " + aiClass, RGB(180, 220, 255), gFontSmall);
    DrawTextAt(hdc, modelBox.left + 14, modelBox.top + 68, "WHY " + ShortenText(aiReason, 31), RGB(170, 180, 195), gFontSmall);

    RECT actionBox{ rc.left + 16, rc.top + 334, rc.right - 16, rc.top + 452 };
    DrawRoundedPanel(hdc, actionBox, RGB(18, 24, 34), safeToHeal ? RGB(46, 204, 113) : RGB(75, 90, 110), 16);
    DrawTextAt(hdc, actionBox.left + 14, actionBox.top + 12, "Auto-Heal Readiness", RGB(235, 240, 248), gFontSection);
    DrawTextAt(hdc, actionBox.left + 14, actionBox.top + 44, string("HEAL READY: ") + (safeToHeal ? "YES" : "NO"), safeToHeal ? RGB(120, 220, 160) : RGB(255, 210, 120), gFontSection);
    DrawTextAt(hdc, actionBox.left + 14, actionBox.top + 78, "ACTION " + ShortenText(ToUpperAscii(ToDisplayToken(recommendedAction)), 25), RGB(170, 180, 195), gFontSmall);

    DrawTextAt(hdc, rc.left + 16, rc.top + 478, "Decision", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 508, "Risk " + to_string(static_cast<int>(riskScore)) + "%  Anom " + to_string(static_cast<int>(anomalyScore)) + "%  Press " + to_string(static_cast<int>(pressureScore)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 532, ShortenText(decisionSummary, 34), RGB(180, 220, 255), gFontSmall);

    DrawTextAt(hdc, rc.left + 16, rc.top + 576, "Runtime", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 606, "MODE " + FormatModeLabel(performanceMode), RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 630, "MODEL " + to_string(aiPredictIntervalTicks) + "s   CACHE " + to_string(modelCacheTtlTicks) + "s", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 654, string("SQLITE ") + (storageReady ? "ACTIVE" : "OFFLINE") + "   QUEUE " + to_string(pendingWrites), storageReady ? RGB(120, 220, 160) : RGB(240, 110, 95), gFontSmall);

    DrawTextAt(hdc, rc.left + 16, rc.top + 698, "Thresholds", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 728, "CPU " + to_string(cpuTh) + "%  MEM " + to_string(memTh) + "%  DISK " + to_string(diskTh) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 752, "AI " + to_string(static_cast<int>(aiAlertTh)) + "%  WARN " + to_string(static_cast<int>(warningRiskThreshold)) + "%  CRIT " + to_string(static_cast<int>(criticalRiskThreshold)) + "%", RGB(170, 180, 195), gFontSmall);
}

void MonitorThread(HWND hwnd) {
    WindowsMetricsCollector collector;
    if (!collector.Initialize()) return;

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
    DecisionThresholds decisionThresholds;
    decisionThresholds.cpuThreshold = cpuTh;
    decisionThresholds.memThreshold = memTh;
    decisionThresholds.diskThreshold = diskTh;
    decisionThresholds.warningRiskThreshold = ClampDouble(g_config.GetDouble("DECISION_WARNING_THRESHOLD", 55.0), 0.0, 100.0);
    decisionThresholds.criticalRiskThreshold = ClampDouble(g_config.GetDouble("DECISION_CRITICAL_THRESHOLD", 75.0), decisionThresholds.warningRiskThreshold, 100.0);
    const wstring runtimeFeaturesPath = (fs::path(GetExecutableDir()) / L"runtime_features.json").wstring();

    bool lastAlert = false;
    bool alertState = false;
    int highRiskStreak = 0;
    int lowRiskStreak = 0;
    ModelPrediction lastModelPrediction;
    long long lastModelTick = -modelCacheTtlTicks;

    while (g_running) {
        this_thread::sleep_for(seconds(1));
        if (!g_running) break;

        SystemSnapshot snapshot = collector.Collect();
        snapshot.scenarioLabel = ReadScenarioLabel();

        deque<double> cpuCopy, memCopy, diskCopy, netCopy, processCopy;
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
            g_disk = snapshot.diskFree;
            g_netDown = snapshot.netDownKBps;
            g_netUp = snapshot.netUpKBps;
            g_processCount = snapshot.processCount;
            g_scenarioLabel = snapshot.scenarioLabel;
            g_topProcessPid = snapshot.topProcess.pid;
            g_topProcessName = snapshot.topProcess.name;
            g_topProcessCpu = snapshot.topProcess.cpuPercent;
            g_topProcessMem = snapshot.topProcess.memoryMB;

            PushHistory(g_cpuHist, snapshot.cpuUsage, HISTORY_SIZE);
            PushHistory(g_memHist, snapshot.memoryUsage, HISTORY_SIZE);
            PushHistory(g_diskHist, snapshot.diskFree, HISTORY_SIZE);
            PushHistory(g_netHist, snapshot.netDownKBps + snapshot.netUpKBps, HISTORY_SIZE);
            PushHistory(g_processHist, static_cast<double>(snapshot.processCount), HISTORY_SIZE);

            cpuCopy = g_cpuHist;
            memCopy = g_memHist;
            diskCopy = g_diskHist;
            netCopy = g_netHist;
            processCopy = g_processHist;
        }

        g_pipeline.Enqueue(snapshot);
        WriteRuntimeFeaturesJson(runtimeFeaturesPath, cpuCopy, memCopy, diskCopy, netCopy, processCopy, cpuTh, memTh, diskTh);

        ModelPrediction modelPrediction;
        const bool hasModelWindow =
            cpuCopy.size() >= AI_WINDOW &&
            memCopy.size() >= AI_WINDOW &&
            diskCopy.size() >= AI_WINDOW;

        const bool shouldRunModel =
            hasModelWindow &&
            ((g_tick % aiPredictIntervalTicks) == 0 || g_aiSource == "WARMING UP");

        if (shouldRunModel) {
            modelPrediction = ReadModelPredictionWithRetry();
        }

        double currentAiProb = 0.0;
        double currentAiConfidence = 0.0;
        string currentSource = "WARMING UP";
        string currentAiReason = "Collecting baseline";

        {
            lock_guard<mutex> lock(g_dataMutex);

            if (!hasModelWindow) {
                g_aiProb = ComputeFallbackProbability(snapshot.cpuUsage, snapshot.memoryUsage, snapshot.diskFree, cpuTh, memTh, diskTh);
                g_aiConfidence = 35.0;
                g_aiClass = "WARMING_UP";
                g_aiReason = "Collecting baseline window";
                g_recommendedAction = "monitor_only";
                g_safeToHeal = false;
                g_aiSource = "WARMING UP";
            } else if (modelPrediction.risk >= 0.0) {
                lastModelPrediction = modelPrediction;
                lastModelTick = g_tick;
                g_aiProb = modelPrediction.risk;
                g_aiConfidence = modelPrediction.confidence;
                g_aiClass = modelPrediction.predictedClass;
                g_aiReason = modelPrediction.reason;
                g_recommendedAction = modelPrediction.recommendedAction;
                g_safeToHeal = modelPrediction.safeToHeal;
                g_aiSource = "MODEL";
            } else if (lastModelPrediction.risk >= 0.0 && (g_tick - lastModelTick) <= modelCacheTtlTicks) {
                g_aiProb = lastModelPrediction.risk;
                g_aiConfidence = lastModelPrediction.confidence;
                g_aiClass = lastModelPrediction.predictedClass;
                g_aiReason = lastModelPrediction.reason;
                g_recommendedAction = lastModelPrediction.recommendedAction;
                g_safeToHeal = lastModelPrediction.safeToHeal;
                g_aiSource = "MODEL";
            } else {
                g_aiProb = ComputeFallbackProbability(snapshot.cpuUsage, snapshot.memoryUsage, snapshot.diskFree, cpuTh, memTh, diskTh);
                g_aiConfidence = 40.0;
                g_aiClass = g_aiProb >= decisionThresholds.criticalRiskThreshold ? "CRITICAL" : (g_aiProb >= decisionThresholds.warningRiskThreshold ? "WARNING" : "NORMAL");
                g_aiReason = "fallback threshold pressure";
                g_recommendedAction = g_aiProb >= decisionThresholds.warningRiskThreshold ? "increase_observation" : "monitor_only";
                g_safeToHeal = false;
                g_aiSource = "FALLBACK";
            }

            currentAiProb = g_aiProb;
            currentAiConfidence = g_aiConfidence;
            currentSource = g_aiSource;
            currentAiReason = g_aiReason;
        }

        DecisionContext decisionContext;
        decisionContext.cpuHistory = cpuCopy;
        decisionContext.memHistory = memCopy;
        decisionContext.diskHistory = diskCopy;
        decisionContext.netHistory = netCopy;
        decisionContext.processHistory = processCopy;
        decisionResult = g_decisionEngine.Evaluate(snapshot, currentAiProb, currentAiConfidence, currentSource, currentAiReason, decisionThresholds, decisionContext);

        {
            lock_guard<mutex> lock(g_dataMutex);
            g_riskScore = decisionResult.riskScore;
            g_anomalyScore = decisionResult.anomalyScore;
            g_pressureScore = decisionResult.pressureScore;
            g_decisionLevel = decisionResult.level;
            g_decisionSummary = decisionResult.summary;
            g_decisionReason = decisionResult.reason;
            g_recommendedAction = decisionResult.recommendedAction;
            g_safeToHeal = decisionResult.safeToHeal;

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

        if (alertLocal && !lastAlert) {
            string msg =
                "System Risk Detected!\n\n" +
                string("Risk Score: ") + to_string(static_cast<int>(decisionResult.riskScore)) + "%\n" +
                string("AI Probability: ") + to_string(static_cast<int>(aiProbLocal)) + "%\n" +
                string("Confidence: ") + to_string(static_cast<int>(aiConfidenceLocal)) + "%\n" +
                string("Source: ") + sourceLocal + "\n" +
                string("Reason: ") + reasonLocal + "\n" +
                string("Top Process: ") + ShortenText(snapshot.topProcess.name, 28) + "\n" +
                string("Decision: ") + decisionResult.summary;

            ShowAlert(msg);
        }

        lastAlert = alertLocal;
        ++g_tick;

        InvalidateRect(hwnd, NULL, TRUE);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
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
        string recommendedAction;
        string decisionSummary;
        bool safeToHeal = false;
        RiskLevel decisionLevel = RiskLevel::Normal;
        double riskScore = 0.0;
        double anomalyScore = 0.0;
        double pressureScore = 0.0;
        deque<double> cpuHist, memHist, diskHist;
        int cpuTh = g_config.GetInt("CPU_THRESHOLD", 80);
        int memTh = g_config.GetInt("MEM_THRESHOLD", 85);
        int diskTh = g_config.GetInt("DISK_THRESHOLD", 10);
        double aiAlertTh = ClampDouble(g_config.GetDouble("AI_ALERT_THRESHOLD", DEFAULT_AI_ALERT_THRESHOLD), 0.0, 100.0);
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
            snapshot.diskFree = g_disk;
            snapshot.netDownKBps = g_netDown;
            snapshot.netUpKBps = g_netUp;
            snapshot.processCount = g_processCount;
            snapshot.scenarioLabel = g_scenarioLabel;
            snapshot.topProcess.pid = g_topProcessPid;
            snapshot.topProcess.name = g_topProcessName;
            snapshot.topProcess.cpuPercent = g_topProcessCpu;
            snapshot.topProcess.memoryMB = g_topProcessMem;
            aiProb = g_aiProb;
            aiConfidence = g_aiConfidence;
            riskScore = g_riskScore;
            anomalyScore = g_anomalyScore;
            pressureScore = g_pressureScore;
            criticalAlertActive = g_alert;
            source = g_aiSource;
            aiClass = g_aiClass;
            aiReason = g_aiReason;
            recommendedAction = g_recommendedAction;
            safeToHeal = g_safeToHeal;
            decisionSummary = g_decisionSummary;
            decisionLevel = g_decisionLevel;
            cpuHist = g_cpuHist;
            memHist = g_memHist;
            diskHist = g_diskHist;
        }

        const int sidebarW = 238;
        const int pad = 16;
        const int headerH = 88;

        RECT sidebar{ 0, 0, sidebarW, client.bottom };
        DrawRoundedPanel(memDC, sidebar, RGB(11, 14, 20), RGB(22, 27, 35), 0);

        DrawTextAt(memDC, 18, 18, "AI Monitor", RGB(255, 255, 255), gFontTitle);
        DrawTextAt(memDC, 18, 56, "Predictive Failure", RGB(160, 170, 185), gFontSmall);
        DrawTextAt(memDC, 18, 76, "Observability", RGB(160, 170, 185), gFontSmall);

        RECT modeBadge{ 16, 112, sidebarW - 16, 160 };
        DrawRoundedPanel(memDC, modeBadge, LevelFillColor(decisionLevel), LevelAccentColor(decisionLevel), 18);
        DrawTextAt(memDC, 32, 128, string(DecisionEngine::ToString(decisionLevel)) + " MODE", RGB(255, 255, 255), gFontSection);

        DrawTextAt(memDC, 18, 190, "Current", RGB(230, 235, 245), gFontSection);
        DrawTextAt(memDC, 18, 222, "CPU   " + to_string(static_cast<int>(snapshot.cpuUsage)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 246, "MEM   " + to_string(static_cast<int>(snapshot.memoryUsage)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 270, "DISK  " + to_string(static_cast<int>(snapshot.diskFree)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 294, "RISK  " + to_string(static_cast<int>(riskScore)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 318, "AI    " + to_string(static_cast<int>(aiProb)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 342, "CONF  " + to_string(static_cast<int>(aiConfidence)) + "%", RGB(170, 180, 195), gFontSmall);

        DrawTextAt(memDC, 18, 390, "Telemetry", RGB(230, 235, 245), gFontSection);
        DrawTextAt(memDC, 18, 422, "SRC   " + source, RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 446, "DOWN  " + FormatRate(snapshot.netDownKBps), RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 470, "UP    " + FormatRate(snapshot.netUpKBps), RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 494, "PROC  " + to_string(snapshot.processCount), RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 518, "TOP   " + ShortenText(snapshot.topProcess.name, 17), RGB(170, 180, 195), gFontSmall);

        DrawTextAt(memDC, 18, 574, "Runtime", RGB(230, 235, 245), gFontSection);
        DrawTextAt(memDC, 18, 606, "MODE  " + FormatModeLabel(performanceMode), RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 630, "MODEL " + to_string(aiPredictIntervalTicks) + " sec", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 18, 654, string("DB    ") + (storageReady ? "ACTIVE" : "OFFLINE"), storageReady ? RGB(120, 220, 160) : RGB(240, 110, 95), gFontSmall);
        DrawTextAt(memDC, 18, 678, "LABEL " + snapshot.scenarioLabel, RGB(170, 180, 195), gFontSmall);

        int mainX = sidebarW + pad;
        int mainW = client.right - mainX - pad;

        DrawTextAt(memDC, mainX, 16, "AI Monitoring Dashboard", RGB(238, 242, 249), gFontTitle);
        DrawTextAt(memDC, mainX, 42, "Grafana-style view for live metrics, AI reliability, storage, and auto-heal readiness", RGB(140, 150, 165), gFontSmall);
        RECT overviewTab = DashboardTabRect(mainX, 62, DashboardView::Overview);
        RECT reliabilityTab = DashboardTabRect(mainX, 62, DashboardView::Reliability);
        RECT runtimeTab = DashboardTabRect(mainX, 62, DashboardView::Runtime);
        RECT autoHealTab = DashboardTabRect(mainX, 62, DashboardView::AutoHeal);
        DrawDashboardTabRect(memDC, overviewTab, "Overview", g_dashboardView == DashboardView::Overview);
        DrawDashboardTabRect(memDC, reliabilityTab, "AI Reliability", g_dashboardView == DashboardView::Reliability);
        DrawDashboardTabRect(memDC, runtimeTab, "Runtime", g_dashboardView == DashboardView::Runtime);
        DrawDashboardTabRect(memDC, autoHealTab, "Auto-Heal", g_dashboardView == DashboardView::AutoHeal);

        RECT statusBadge{ client.right - 220, 18, client.right - 20, 56 };
        DrawRoundedPanel(memDC, statusBadge, LevelFillColor(decisionLevel), LevelAccentColor(decisionLevel), 18);
        DrawTextAt(memDC, client.right - 190, 29, DecisionEngine::ToString(decisionLevel), RGB(255, 255, 255), gFontSection);

        int rowY = headerH + 12;
        int cardH = 132;
        int gap = 12;
        int cardW = (mainW - (gap * 4)) / 5;

        DrawMetricCard(memDC, mainX + (cardW + gap) * 0, rowY, cardW, cardH, "CPU USAGE", to_string(static_cast<int>(snapshot.cpuUsage)) + "%", "Live CPU", snapshot.cpuUsage, RGB(52, 152, 219), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 1, rowY, cardW, cardH, "MEMORY", to_string(static_cast<int>(snapshot.memoryUsage)) + "%", "Live memory", snapshot.memoryUsage, RGB(46, 204, 113), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 2, rowY, cardW, cardH, "DISK FREE", to_string(static_cast<int>(snapshot.diskFree)) + "%", "Free disk", 100.0 - snapshot.diskFree, RGB(241, 196, 15), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 3, rowY, cardW, cardH, "NETWORK", FormatRate(snapshot.netDownKBps), "Up: " + FormatRate(snapshot.netUpKBps), ClampDouble((snapshot.netDownKBps / 1024.0) * 100.0, 0.0, 100.0), RGB(155, 89, 182), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 4, rowY, cardW, cardH, "RISK SCORE", to_string(static_cast<int>(riskScore)) + "%", string(DecisionEngine::ToString(decisionLevel)), riskScore, LevelAccentColor(decisionLevel), criticalAlertActive);

        int middleTop = rowY + cardH + 14;
        int bottomH = 188;
        int bottomTop = max(middleTop + 260, static_cast<int>(client.bottom) - bottomH - pad);
        int middleH = max(260, bottomTop - middleTop - 14);
        int reliabilityW = min(390, max(330, mainW / 4));
        int graphW = mainW - reliabilityW - gap;

        RECT graphRc{ mainX, middleTop, mainX + graphW, middleTop + middleH };
        RECT reliabilityRc{ mainX + graphW + gap, middleTop, client.right - pad, middleTop + middleH };

        int bottomGap = 12;
        int bottomW = (mainW - (bottomGap * 3)) / 4;
        RECT decisionRc{ mainX, bottomTop, mainX + bottomW, bottomTop + bottomH };
        RECT runtimeRc{ decisionRc.right + bottomGap, bottomTop, decisionRc.right + bottomGap + bottomW, bottomTop + bottomH };
        RECT healRc{ runtimeRc.right + bottomGap, bottomTop, runtimeRc.right + bottomGap + bottomW, bottomTop + bottomH };
        RECT thresholdRc{ healRc.right + bottomGap, bottomTop, client.right - pad, bottomTop + bottomH };

        if (g_dashboardView == DashboardView::Overview) {
            DrawGraph(memDC, graphRc, cpuHist, memHist, diskHist);
            DrawReliabilityPanel(memDC, reliabilityRc, aiProb, aiConfidence, source, aiClass, aiReason, decisionLevel);
            DrawDecisionPanel(memDC, decisionRc, decisionLevel, riskScore, anomalyScore, pressureScore, decisionSummary);
            DrawRuntimePanel(memDC, runtimeRc, performanceMode, aiPredictIntervalTicks, modelCacheTtlTicks, pendingWrites, storageReady);
            DrawAutoHealPanel(memDC, healRc, recommendedAction, safeToHeal, snapshot);
            DrawThresholdPanel(memDC, thresholdRc, cpuTh, memTh, diskTh, aiAlertTh, warningRiskThreshold, criticalRiskThreshold);
        } else if (g_dashboardView == DashboardView::Reliability) {
            RECT bigReliability{ mainX, middleTop, mainX + reliabilityW + 120, middleTop + middleH };
            RECT decisionWide{ bigReliability.right + gap, middleTop, client.right - pad, middleTop + middleH };
            DrawReliabilityPanel(memDC, bigReliability, aiProb, aiConfidence, source, aiClass, aiReason, decisionLevel);
            DrawDecisionPanel(memDC, decisionWide, decisionLevel, riskScore, anomalyScore, pressureScore, decisionSummary);
            DrawPlaceholderWidePanel(memDC, decisionRc, "Model Contract", "Contract ai_reliability_v2", "50 features: resources, trends, spikes, recovery");
            DrawThresholdPanel(memDC, runtimeRc, cpuTh, memTh, diskTh, aiAlertTh, warningRiskThreshold, criticalRiskThreshold);
            DrawAutoHealPanel(memDC, healRc, recommendedAction, safeToHeal, snapshot);
            DrawRuntimePanel(memDC, thresholdRc, performanceMode, aiPredictIntervalTicks, modelCacheTtlTicks, pendingWrites, storageReady);
        } else if (g_dashboardView == DashboardView::Runtime) {
            RECT runtimeWide{ mainX, middleTop, mainX + (mainW / 2) - gap, middleTop + middleH };
            RECT thresholdWide{ runtimeWide.right + gap, middleTop, client.right - pad, middleTop + middleH };
            DrawRuntimePanel(memDC, runtimeWide, performanceMode, aiPredictIntervalTicks, modelCacheTtlTicks, pendingWrites, storageReady);
            DrawThresholdPanel(memDC, thresholdWide, cpuTh, memTh, diskTh, aiAlertTh, warningRiskThreshold, criticalRiskThreshold);
            DrawGraph(memDC, decisionRc, cpuHist, memHist, diskHist);
            DrawReliabilityPanel(memDC, runtimeRc, aiProb, aiConfidence, source, aiClass, aiReason, decisionLevel);
            DrawDecisionPanel(memDC, healRc, decisionLevel, riskScore, anomalyScore, pressureScore, decisionSummary);
            DrawAutoHealPanel(memDC, thresholdRc, recommendedAction, safeToHeal, snapshot);
        } else {
            RECT healWide{ mainX, middleTop, mainX + (mainW / 2) - gap, middleTop + middleH };
            RECT decisionWide{ healWide.right + gap, middleTop, client.right - pad, middleTop + middleH };
            DrawAutoHealPanel(memDC, healWide, recommendedAction, safeToHeal, snapshot);
            DrawDecisionPanel(memDC, decisionWide, decisionLevel, riskScore, anomalyScore, pressureScore, decisionSummary);
            DrawPlaceholderWidePanel(memDC, decisionRc, "Safety Policy", "Auto-heal execution is disabled", "Future healing needs allowlist, cooldown, confidence, and rollback checks");
            DrawReliabilityPanel(memDC, runtimeRc, aiProb, aiConfidence, source, aiClass, aiReason, decisionLevel);
            DrawRuntimePanel(memDC, healRc, performanceMode, aiPredictIntervalTicks, modelCacheTtlTicks, pendingWrites, storageReady);
            DrawThresholdPanel(memDC, thresholdRc, cpuTh, memTh, diskTh, aiAlertTh, warningRiskThreshold, criticalRiskThreshold);
        }

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
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT client;
        GetClientRect(hwnd, &client);
        const int sidebarW = 238;
        const int pad = 16;
        int mainX = sidebarW + pad;

        DashboardView nextView = g_dashboardView;
        if (PtInRectSimple(DashboardTabRect(mainX, 62, DashboardView::Overview), x, y)) {
            nextView = DashboardView::Overview;
        } else if (PtInRectSimple(DashboardTabRect(mainX, 62, DashboardView::Reliability), x, y)) {
            nextView = DashboardView::Reliability;
        } else if (PtInRectSimple(DashboardTabRect(mainX, 62, DashboardView::Runtime), x, y)) {
            nextView = DashboardView::Runtime;
        } else if (PtInRectSimple(DashboardTabRect(mainX, 62, DashboardView::AutoHeal), x, y)) {
            nextView = DashboardView::AutoHeal;
        }

        if (nextView != g_dashboardView) {
            g_dashboardView = nextView;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_DESTROY:
        g_running = false;
        if (g_monitorThread.joinable()) {
            g_monitorThread.join();
        }
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
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    gFontSection = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    gFontValue = CreateFontW(-34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    gFontSmall = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"AI Monitoring Dashboard",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1440, 1020,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        MessageBoxA(NULL, "Failed to create window.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    g_monitorThread = thread(MonitorThread, hwnd);

    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
