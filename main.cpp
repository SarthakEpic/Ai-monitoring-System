#include <windows.h>

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
constexpr int DEFAULT_AI_PREDICT_INTERVAL_TICKS = 2;
constexpr double DEFAULT_AI_ALERT_THRESHOLD = 65.0;
constexpr double DEFAULT_AI_ALERT_CLEAR_THRESHOLD = 55.0;
constexpr int DEFAULT_AI_ALERT_TRIGGER_STREAK = 3;
constexpr int DEFAULT_AI_ALERT_CLEAR_STREAK = 3;
constexpr int AI_MODEL_RETRY_COUNT = 2;
constexpr int AI_MODEL_RETRY_DELAY_MS = 60;

double g_cpu = 0.0;
double g_mem = 0.0;
double g_disk = 0.0;
double g_aiProb = 0.0;
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
string g_topProcessName = "N/A";
string g_decisionSummary = "System stable";
RiskLevel g_decisionLevel = RiskLevel::Normal;

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

void WriteRuntimeFeaturesJson(
    const wstring& outputPath,
    const deque<double>& cpuHist,
    const deque<double>& memHist,
    const deque<double>& diskHist,
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
    file << "  \"disk_history\": " << DequeToJsonArray(diskHist) << "\n";
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

double ReadModelProbability() {
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

    if (!ok) return -1.0;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        DeleteFileW(outputPath.c_str());
        return -1.0;
    }

    ifstream file(outputPath);
    string text;
    getline(file, text);

    DeleteFileW(outputPath.c_str());

    try {
        double prob = stod(text);
        if (prob < 0.0 || prob > 100.0) return -1.0;
        return prob;
    } catch (...) {
        return -1.0;
    }
}

double ReadModelProbabilityWithRetry() {
    for (int attempt = 0; attempt < AI_MODEL_RETRY_COUNT; ++attempt) {
        double prob = ReadModelProbability();
        if (prob >= 0.0) return prob;

        if (attempt + 1 < AI_MODEL_RETRY_COUNT) {
            this_thread::sleep_for(milliseconds(AI_MODEL_RETRY_DELAY_MS));
        }
    }
    return -1.0;
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

    DrawTextAt(hdc, x + 18, y + 14, title, RGB(225, 232, 242), gFontSection);
    DrawTextAt(hdc, x + 18, y + 44, valueText, RGB(255, 255, 255), gFontValue);
    DrawTextAt(hdc, x + 18, y + 90, subText, RGB(176, 186, 199), gFontSmall);

    DrawProgressBar(hdc, x + 18, y + h - 24, w - 36, 10, progressValue, accent);
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

void DrawInsightPanel(HDC hdc, const RECT& rc,
                      const SystemSnapshot& snapshot,
                      double aiProb, const string& source,
                      RiskLevel decisionLevel,
                      double riskScore,
                      double anomalyScore,
                      double pressureScore,
                      const string& decisionSummary,
                      int cpuTh, int memTh, int diskTh,
                      double aiAlertTh,
                      double warningRiskThreshold,
                      double criticalRiskThreshold) {
    COLORREF bg = (decisionLevel == RiskLevel::Normal) ? RGB(20, 26, 36) : LevelFillColor(decisionLevel);
    COLORREF border = (decisionLevel == RiskLevel::Normal) ? RGB(62, 76, 94) : LevelAccentColor(decisionLevel);
    DrawRoundedPanel(hdc, rc, bg, border, 24);

    DrawTextAt(hdc, rc.left + 16, rc.top + 14, "AI Insights", RGB(235, 240, 248), gFontSection);

    RECT badge{ rc.left + 16, rc.top + 50, rc.right - 16, rc.top + 96 };
    DrawRoundedPanel(hdc, badge, LevelAccentColor(decisionLevel), LevelAccentColor(decisionLevel), 18);
    DrawTextAt(hdc, rc.left + 30, rc.top + 63, DecisionEngine::ToString(decisionLevel), RGB(255, 255, 255), gFontSection);

    DrawTextAt(hdc, rc.left + 16, rc.top + 118, "AI Probability", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 138, to_string(static_cast<int>(aiProb)) + "%", RGB(255, 255, 255), gFontValue);

    DrawProgressBar(hdc, rc.left + 16, rc.top + 188, rc.right - rc.left - 32, 14, aiProb, LevelAccentColor(decisionLevel));

    DrawTextAt(hdc, rc.left + 16, rc.top + 224, "Model Source", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 252, source, RGB(180, 220, 255), gFontSmall);

    DrawTextAt(hdc, rc.left + 16, rc.top + 288, "Thresholds", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 320, "CPU   " + to_string(cpuTh) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 346, "MEM   " + to_string(memTh) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 372, "DISK  " + to_string(diskTh) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 398, "AI    " + to_string(static_cast<int>(aiAlertTh)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 424, "WARN  " + to_string(static_cast<int>(warningRiskThreshold)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 450, "CRIT  " + to_string(static_cast<int>(criticalRiskThreshold)) + "%", RGB(170, 180, 195), gFontSmall);

    DrawTextAt(hdc, rc.left + 16, rc.top + 486, "Decision", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 518, "Risk     " + to_string(static_cast<int>(riskScore)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 544, "Anomaly  " + to_string(static_cast<int>(anomalyScore)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 570, "Pressure " + to_string(static_cast<int>(pressureScore)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 596, ShortenText(decisionSummary, 30), RGB(180, 220, 255), gFontSmall);

    DrawTextAt(hdc, rc.left + 16, rc.top + 632, "Live Context", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 664, "CPU " + to_string(static_cast<int>(snapshot.cpuUsage)) + "%  MEM " + to_string(static_cast<int>(snapshot.memoryUsage)) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 690, "DISK " + to_string(static_cast<int>(snapshot.diskFree)) + "%  NET " + FormatRate(snapshot.netDownKBps), RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 716, ShortenText(snapshot.topProcess.name, 26), RGB(180, 220, 255), gFontSmall);
}

void MonitorThread(HWND hwnd) {
    WindowsMetricsCollector collector;
    if (!collector.Initialize()) return;

    int cpuTh = g_config.GetInt("CPU_THRESHOLD", 80);
    int memTh = g_config.GetInt("MEM_THRESHOLD", 85);
    int diskTh = g_config.GetInt("DISK_THRESHOLD", 10);
    int aiPredictIntervalTicks = max(1, g_config.GetInt("AI_PREDICT_INTERVAL_TICKS", DEFAULT_AI_PREDICT_INTERVAL_TICKS));
    double aiAlertThreshold = ClampDouble(g_config.GetDouble("AI_ALERT_THRESHOLD", DEFAULT_AI_ALERT_THRESHOLD), 0.0, 100.0);
    double aiAlertClearThreshold = ClampDouble(g_config.GetDouble("AI_ALERT_CLEAR_THRESHOLD", DEFAULT_AI_ALERT_CLEAR_THRESHOLD), 0.0, aiAlertThreshold);
    int aiAlertTriggerStreak = max(1, g_config.GetInt("AI_ALERT_TRIGGER_STREAK", DEFAULT_AI_ALERT_TRIGGER_STREAK));
    int aiAlertClearStreak = max(1, g_config.GetInt("AI_ALERT_CLEAR_STREAK", DEFAULT_AI_ALERT_CLEAR_STREAK));
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

    while (g_running) {
        this_thread::sleep_for(seconds(1));
        if (!g_running) break;

        SystemSnapshot snapshot = collector.Collect();

        deque<double> cpuCopy, memCopy, diskCopy, netCopy, processCopy;
        double aiProbLocal = 0.0;
        string sourceLocal = "WARMING UP";
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
        WriteRuntimeFeaturesJson(runtimeFeaturesPath, cpuCopy, memCopy, diskCopy, cpuTh, memTh, diskTh);

        double modelProb = -1.0;
        const bool hasModelWindow =
            cpuCopy.size() >= AI_WINDOW &&
            memCopy.size() >= AI_WINDOW &&
            diskCopy.size() >= AI_WINDOW;

        if (hasModelWindow && ((g_tick % aiPredictIntervalTicks) == 0 || g_aiSource == "WARMING UP")) {
            modelProb = ReadModelProbabilityWithRetry();
        }

        double currentAiProb = 0.0;
        string currentSource = "WARMING UP";

        {
            lock_guard<mutex> lock(g_dataMutex);

            if (!hasModelWindow) {
                g_aiProb = ComputeFallbackProbability(snapshot.cpuUsage, snapshot.memoryUsage, snapshot.diskFree, cpuTh, memTh, diskTh);
                g_aiSource = "WARMING UP";
            } else if (modelProb >= 0.0) {
                g_aiProb = modelProb;
                g_aiSource = "MODEL";
            } else {
                g_aiProb = ComputeFallbackProbability(snapshot.cpuUsage, snapshot.memoryUsage, snapshot.diskFree, cpuTh, memTh, diskTh);
                g_aiSource = "FALLBACK";
            }

            currentAiProb = g_aiProb;
            currentSource = g_aiSource;
        }

        DecisionContext decisionContext;
        decisionContext.cpuHistory = cpuCopy;
        decisionContext.memHistory = memCopy;
        decisionContext.diskHistory = diskCopy;
        decisionContext.netHistory = netCopy;
        decisionContext.processHistory = processCopy;
        decisionResult = g_decisionEngine.Evaluate(snapshot, currentAiProb, currentSource, decisionThresholds, decisionContext);

        {
            lock_guard<mutex> lock(g_dataMutex);
            g_riskScore = decisionResult.riskScore;
            g_anomalyScore = decisionResult.anomalyScore;
            g_pressureScore = decisionResult.pressureScore;
            g_decisionLevel = decisionResult.level;
            g_decisionSummary = decisionResult.summary;

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
            sourceLocal = g_aiSource;
            alertLocal = alertState;
            g_alert = alertLocal;
        }

        if (alertLocal && !lastAlert) {
            string msg =
                "System Risk Detected!\n\n" +
                string("Risk Score: ") + to_string(static_cast<int>(decisionResult.riskScore)) + "%\n" +
                string("AI Probability: ") + to_string(static_cast<int>(aiProbLocal)) + "%\n" +
                string("Source: ") + sourceLocal + "\n" +
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
        bool criticalAlertActive = false;
        string source;
        string decisionSummary;
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

        {
            lock_guard<mutex> lock(g_dataMutex);
            snapshot.cpuUsage = g_cpu;
            snapshot.memoryUsage = g_mem;
            snapshot.diskFree = g_disk;
            snapshot.netDownKBps = g_netDown;
            snapshot.netUpKBps = g_netUp;
            snapshot.processCount = g_processCount;
            snapshot.topProcess.pid = g_topProcessPid;
            snapshot.topProcess.name = g_topProcessName;
            snapshot.topProcess.cpuPercent = g_topProcessCpu;
            snapshot.topProcess.memoryMB = g_topProcessMem;
            aiProb = g_aiProb;
            riskScore = g_riskScore;
            anomalyScore = g_anomalyScore;
            pressureScore = g_pressureScore;
            criticalAlertActive = g_alert;
            source = g_aiSource;
            decisionSummary = g_decisionSummary;
            decisionLevel = g_decisionLevel;
            cpuHist = g_cpuHist;
            memHist = g_memHist;
            diskHist = g_diskHist;
        }

        const int sidebarW = 270;
        const int pad = 18;
        const int headerH = 82;

        RECT sidebar{ 0, 0, sidebarW, client.bottom };
        DrawRoundedPanel(memDC, sidebar, RGB(11, 14, 20), RGB(22, 27, 35), 0);

        DrawTextAt(memDC, 20, 18, "AI Monitor", RGB(255, 255, 255), gFontTitle);
        DrawTextAt(memDC, 20, 56, "Predictive Server", RGB(160, 170, 185), gFontSmall);
        DrawTextAt(memDC, 20, 76, "Monitoring", RGB(160, 170, 185), gFontSmall);

        RECT modeBadge{ 18, 112, sidebarW - 18, 160 };
        DrawRoundedPanel(memDC, modeBadge, LevelFillColor(decisionLevel), LevelAccentColor(decisionLevel), 18);
        DrawTextAt(memDC, 36, 128, string(DecisionEngine::ToString(decisionLevel)) + " MODE", RGB(255, 255, 255), gFontSection);

        DrawTextAt(memDC, 20, 190, "Thresholds", RGB(230, 235, 245), gFontSection);
        DrawTextAt(memDC, 20, 222, "CPU   " + to_string(cpuTh) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 246, "MEM   " + to_string(memTh) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 270, "DISK  " + to_string(diskTh) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 294, "AI    " + to_string(static_cast<int>(aiAlertTh)) + "%", RGB(170, 180, 195), gFontSmall);

        DrawTextAt(memDC, 20, 334, "Current", RGB(230, 235, 245), gFontSection);
        DrawTextAt(memDC, 20, 366, "CPU   " + to_string(static_cast<int>(snapshot.cpuUsage)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 390, "MEM   " + to_string(static_cast<int>(snapshot.memoryUsage)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 414, "DISK  " + to_string(static_cast<int>(snapshot.diskFree)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 438, "AI    " + to_string(static_cast<int>(aiProb)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 462, "RISK  " + to_string(static_cast<int>(riskScore)) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 486, "LVL   " + string(DecisionEngine::ToString(decisionLevel)), RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 510, "SRC   " + source, RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 534, "DOWN  " + FormatRate(snapshot.netDownKBps), RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 558, "PROC  " + to_string(snapshot.processCount), RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 582, "TOP   " + ShortenText(snapshot.topProcess.name, 18), RGB(170, 180, 195), gFontSmall);

        int mainX = sidebarW + pad;
        int mainW = client.right - mainX - pad;

        DrawTextAt(memDC, mainX, 16, "AI Monitoring Dashboard", RGB(238, 242, 249), gFontTitle);
        DrawTextAt(memDC, mainX, 42, "Live metrics, queued storage, decision scoring, and Windows process/network telemetry", RGB(140, 150, 165), gFontSmall);

        RECT statusBadge{ client.right - 220, 18, client.right - 20, 56 };
        DrawRoundedPanel(memDC, statusBadge, LevelFillColor(decisionLevel), LevelAccentColor(decisionLevel), 18);
        DrawTextAt(memDC, client.right - 190, 29, DecisionEngine::ToString(decisionLevel), RGB(255, 255, 255), gFontSection);

        int rowY = headerH + 14;
        int cardH = 138;
        int gap = 12;
        int cardW = (mainW - (gap * 4)) / 5;

        DrawMetricCard(memDC, mainX + (cardW + gap) * 0, rowY, cardW, cardH, "CPU USAGE", to_string(static_cast<int>(snapshot.cpuUsage)) + "%", "Live CPU", snapshot.cpuUsage, RGB(52, 152, 219), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 1, rowY, cardW, cardH, "MEMORY", to_string(static_cast<int>(snapshot.memoryUsage)) + "%", "Live memory", snapshot.memoryUsage, RGB(46, 204, 113), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 2, rowY, cardW, cardH, "DISK FREE", to_string(static_cast<int>(snapshot.diskFree)) + "%", "Free disk", 100.0 - snapshot.diskFree, RGB(241, 196, 15), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 3, rowY, cardW, cardH, "NETWORK", FormatRate(snapshot.netDownKBps), "Up: " + FormatRate(snapshot.netUpKBps), ClampDouble((snapshot.netDownKBps / 1024.0) * 100.0, 0.0, 100.0), RGB(155, 89, 182), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 4, rowY, cardW, cardH, "RISK SCORE", to_string(static_cast<int>(riskScore)) + "%", string(DecisionEngine::ToString(decisionLevel)), riskScore, LevelAccentColor(decisionLevel), criticalAlertActive);

        int contentTop = rowY + cardH + 16;
        int graphW = mainW - 320 - 16;
        int graphH = client.bottom - contentTop - 20;

        RECT graphRc{ mainX, contentTop, mainX + graphW, contentTop + graphH };
        RECT insightRc{ mainX + graphW + 16, contentTop, client.right - pad, contentTop + graphH };

        DrawGraph(memDC, graphRc, cpuHist, memHist, diskHist);
        DrawInsightPanel(memDC, insightRc, snapshot, aiProb, source, decisionLevel, riskScore, anomalyScore, pressureScore, decisionSummary, cpuTh, memTh, diskTh, aiAlertTh, warningRiskThreshold, criticalRiskThreshold);

        DrawTextAt(memDC, mainX, client.bottom - 16, "Press Ctrl+C in the terminal to stop monitoring", RGB(120, 130, 145), gFontSmall);

        BitBlt(hdc, 0, 0, client.right, client.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

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
