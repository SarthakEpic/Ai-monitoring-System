#define UNICODE
#define _UNICODE
#define NOMINMAX

#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <thread>
#include <chrono>
#include <iomanip>
#include <map>
#include <string>
#include <mutex>
#include <algorithm>
#include <sstream>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "sqlite3.h"

using namespace std;
using namespace std::chrono;
namespace fs = std::filesystem;

// ================= GLOBAL STATE =================
constexpr size_t HISTORY_SIZE = 30;
constexpr size_t AI_WINDOW = 8;
constexpr int AI_PREDICT_INTERVAL_TICKS = 2;
constexpr double AI_ALERT_THRESHOLD = 65.0;

double g_cpu = 0.0;
double g_mem = 0.0;
double g_disk = 0.0;
double g_aiProb = 0.0;

bool g_alert = false;
string g_aiSource = "WARMING UP";

deque<double> g_cpuHist;
deque<double> g_memHist;
deque<double> g_diskHist;

mutex g_dataMutex;

HFONT gFontTitle = NULL;
HFONT gFontSection = NULL;
HFONT gFontValue = NULL;
HFONT gFontSmall = NULL;

sqlite3* g_db = nullptr;
bool g_dbReady = false;
long long g_tick = 0;

// ================= CONFIG =================
map<string, string> config;

static string Trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void LoadConfig() {
    ifstream file("config.txt");
    string line;
    while (getline(file, line)) {
        size_t pos = line.find('=');
        if (pos != string::npos) {
            string key = Trim(line.substr(0, pos));
            string value = Trim(line.substr(pos + 1));
            if (!key.empty()) config[key] = value;
        }
    }
}

int GetInt(const string& key, int def) {
    try {
        auto it = config.find(key);
        if (it != config.end() && !it->second.empty()) {
            return stoi(it->second);
        }
    } catch (...) {
    }
    return def;
}

// ================= LOG / DB =================
bool InitDB() {
    if (sqlite3_open("monitor.db", &g_db) != SQLITE_OK) {
        g_db = nullptr;
        return false;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS metrics ("
        "time INTEGER, "
        "cpu REAL, "
        "mem REAL, "
        "disk REAL);";

    char* errMsg = nullptr;
    if (sqlite3_exec(g_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    g_dbReady = true;
    return true;
}

void CloseDB() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
    g_dbReady = false;
}

void LogToDB(double cpu, double mem, double disk) {
    if (!g_dbReady) return;

    const char* sql = "INSERT INTO metrics(time, cpu, mem, disk) VALUES(?1, ?2, ?3, ?4);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(time(nullptr)));
    sqlite3_bind_double(stmt, 2, cpu);
    sqlite3_bind_double(stmt, 3, mem);
    sqlite3_bind_double(stmt, 4, disk);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

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

void WriteRuntimeFeaturesJson(
    const wstring& outputPath,
    const deque<double>& cpuHist,
    const deque<double>& memHist,
    const deque<double>& diskHist,
    int cpuTh,
    int memTh,
    int diskTh
) {
    ofstream file(outputPath, ios::trunc);
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
}

// ================= ALERT =================
void ShowAlert(const string& message) {
    MessageBeep(MB_ICONWARNING);
    MessageBoxA(NULL, message.c_str(), "AI Alert", MB_OK | MB_ICONWARNING);
}

// ================= SYSTEM METRICS =================
struct CpuTimes {
    ULONGLONG idle, kernel, user;
};

ULONGLONG FileTimeToULL(const FILETIME& ft) {
    ULARGE_INTEGER ui;
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return ui.QuadPart;
}

bool GetCpuTimes(CpuTimes& t) {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return false;
    t.idle = FileTimeToULL(idle);
    t.kernel = FileTimeToULL(kernel);
    t.user = FileTimeToULL(user);
    return true;
}

double CalculateCPU(const CpuTimes& prev, const CpuTimes& curr) {
    ULONGLONG idle = curr.idle - prev.idle;
    ULONGLONG total = (curr.kernel - prev.kernel) + (curr.user - prev.user);
    if (total == 0) return 0.0;
    return 100.0 * (double)(total - idle) / (double)total;
}

double GetMemoryUsage() {
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) return 0.0;
    return (double)mem.dwMemoryLoad;
}

double GetDiskFree(const wstring& path) {
    ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFree{};
    if (!GetDiskFreeSpaceExW(path.c_str(), &freeBytes, &totalBytes, &totalFree)) return 0.0;
    if (totalBytes.QuadPart == 0) return 0.0;
    return (100.0 * (double)totalFree.QuadPart) / (double)totalBytes.QuadPart;
}

// ================= AI HELPERS =================
void PushHistory(deque<double>& hist, double value, size_t maxSize) {
    hist.push_back(value);
    if (hist.size() > maxSize) hist.pop_front();
}

double ClampDouble(double v, double lo, double hi) {
    return max(lo, min(hi, v));
}

double Slope(const deque<double>& hist) {
    if (hist.size() < 2) return 0.0;
    double first = hist.front();
    double last = hist.back();
    return (last - first) / (double)(hist.size() - 1);
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

    const wstring command =
        L"cmd /C python \"" + scriptPath +
        L"\" --input \"" + inputPath +
        L"\" --model \"" + modelPath +
        L"\" > \"" + outputPath + L"\"";

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
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    ifstream file(outputPath);
    string text;
    getline(file, text);

    DeleteFileW(outputPath.c_str());

    text = Trim(text);
    try {
        double prob = stod(text);
        if (prob < 0.0 || prob > 100.0) return -1.0;
        return prob;
    } catch (...) {
        return -1.0;
    }
}

// ================= DRAW HELPERS =================
void DrawTextAt(HDC hdc, int x, int y, const string& text, COLORREF color, HFONT font) {
    HGDIOBJ oldFont = SelectObject(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    TextOutA(hdc, x, y, text.c_str(), (int)text.size());
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
    RECT fg{ x, y, x + (int)((value / 100.0) * w), y + h };

    HBRUSH bgBrush = CreateSolidBrush(RGB(42, 49, 62));
    HBRUSH fgBrush = CreateSolidBrush(fillColor);

    FillRect(hdc, &bg, bgBrush);
    FillRect(hdc, &fg, fgBrush);

    DeleteObject(bgBrush);
    DeleteObject(fgBrush);
}

void DrawMetricCard(HDC hdc, int x, int y, int w, int h,
                    const string& title,
                    double current,
                    double predicted,
                    COLORREF accent,
                    bool alertCard) {
    RECT rc{ x, y, x + w, y + h };

    COLORREF fill = alertCard ? RGB(64, 24, 32) : RGB(26, 32, 43);
    COLORREF border = alertCard ? RGB(231, 76, 60) : accent;
    DrawRoundedPanel(hdc, rc, fill, border, 24);

    DrawTextAt(hdc, x + 18, y + 14, title, RGB(225, 232, 242), gFontSection);
    DrawTextAt(hdc, x + 18, y + 44, to_string((int)current) + "%", RGB(255, 255, 255), gFontValue);
    DrawTextAt(hdc, x + 18, y + 90, "Predicted: " + to_string((int)predicted) + "%", RGB(176, 186, 199), gFontSmall);

    DrawProgressBar(hdc, x + 18, y + h - 24, w - 36, 10, current, accent);
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

        int n = (int)hist.size();
        int w = inner.right - inner.left;
        int h = inner.bottom - inner.top;

        for (int i = 0; i < n; ++i) {
            double v = ClampDouble(hist[i], 0.0, 100.0);
            int x = inner.left + (i * w) / max(1, n - 1);
            int y = inner.bottom - (int)((v / 100.0) * h);

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
                      double cpu, double mem, double disk,
                      double aiProb, const string& source,
                      bool alertNow,
                      int cpuTh, int memTh, int diskTh) {
    COLORREF bg = alertNow ? RGB(64, 24, 32) : RGB(20, 26, 36);
    COLORREF border = alertNow ? RGB(231, 76, 60) : RGB(62, 76, 94);
    DrawRoundedPanel(hdc, rc, bg, border, 24);

    DrawTextAt(hdc, rc.left + 16, rc.top + 14, "AI Insights", RGB(235, 240, 248), gFontSection);

    RECT badge{ rc.left + 16, rc.top + 50, rc.right - 16, rc.top + 96 };
    DrawRoundedPanel(hdc, badge, alertNow ? RGB(231, 76, 60) : RGB(39, 174, 96), alertNow ? RGB(231, 76, 60) : RGB(39, 174, 96), 18);
    DrawTextAt(hdc, rc.left + 30, rc.top + 63, alertNow ? "ALERT ACTIVE" : "SYSTEM STABLE", RGB(255, 255, 255), gFontSection);

    DrawTextAt(hdc, rc.left + 16, rc.top + 118, "AI Probability", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 138, to_string((int)aiProb) + "%", RGB(255, 255, 255), gFontValue);

    DrawProgressBar(hdc, rc.left + 16, rc.top + 188, rc.right - rc.left - 32, 14, aiProb, alertNow ? RGB(231, 76, 60) : RGB(46, 204, 113));

    DrawTextAt(hdc, rc.left + 16, rc.top + 224, "Model Source", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 252, source, RGB(180, 220, 255), gFontSmall);

    DrawTextAt(hdc, rc.left + 16, rc.top + 300, "Thresholds", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 332, "CPU   " + to_string(cpuTh) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 358, "MEM   " + to_string(memTh) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 384, "DISK  " + to_string(diskTh) + "%", RGB(170, 180, 195), gFontSmall);

    DrawTextAt(hdc, rc.left + 16, rc.top + 432, "Current Values", RGB(230, 235, 245), gFontSection);
    DrawTextAt(hdc, rc.left + 16, rc.top + 464, "CPU   " + to_string((int)cpu) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 490, "MEM   " + to_string((int)mem) + "%", RGB(170, 180, 195), gFontSmall);
    DrawTextAt(hdc, rc.left + 16, rc.top + 516, "DISK  " + to_string((int)disk) + "%", RGB(170, 180, 195), gFontSmall);
}

// ================= MONITOR THREAD =================
void MonitorThread(HWND hwnd) {
    CpuTimes prev{}, curr{};
    if (!GetCpuTimes(prev)) return;

    int cpuTh = GetInt("CPU_THRESHOLD", 80);
    int memTh = GetInt("MEM_THRESHOLD", 85);
    int diskTh = GetInt("DISK_THRESHOLD", 10);
    const wstring runtimeFeaturesPath = (fs::path(GetExecutableDir()) / L"runtime_features.json").wstring();

    bool lastAlert = false;

    while (true) {
        this_thread::sleep_for(seconds(1));

        if (!GetCpuTimes(curr)) continue;

        double cpu = CalculateCPU(prev, curr);
        double mem = GetMemoryUsage();
        double disk = GetDiskFree(L"C:\\");

        prev = curr;

        deque<double> cpuCopy, memCopy, diskCopy;
        double aiProbLocal = 0.0;
        string sourceLocal = "WARMING UP";
        bool alertLocal = false;

        {
            lock_guard<mutex> lock(g_dataMutex);

            g_cpu = cpu;
            g_mem = mem;
            g_disk = disk;

            PushHistory(g_cpuHist, cpu, HISTORY_SIZE);
            PushHistory(g_memHist, mem, HISTORY_SIZE);
            PushHistory(g_diskHist, disk, HISTORY_SIZE);

            cpuCopy = g_cpuHist;
            memCopy = g_memHist;
            diskCopy = g_diskHist;
        }

        LogToDB(cpu, mem, disk);
        WriteRuntimeFeaturesJson(runtimeFeaturesPath, cpuCopy, memCopy, diskCopy, cpuTh, memTh, diskTh);

        double modelProb = -1.0;
        if ((g_tick % AI_PREDICT_INTERVAL_TICKS) == 0 || g_aiSource == "WARMING UP") {
            modelProb = ReadModelProbability();
        }

        {
            lock_guard<mutex> lock(g_dataMutex);

            if (modelProb >= 0.0) {
                g_aiProb = modelProb;
                g_aiSource = "MODEL";
            } else {
                g_aiProb = ComputeFallbackProbability(cpu, mem, disk, cpuTh, memTh, diskTh);
                g_aiSource = "FALLBACK";
            }

            aiProbLocal = g_aiProb;
            sourceLocal = g_aiSource;
            alertLocal = (g_aiProb >= AI_ALERT_THRESHOLD);
            g_alert = alertLocal;
        }

        if (alertLocal && !lastAlert) {
            string msg =
                "System Risk Detected!\n\n" +
                string("AI Probability: ") + to_string((int)aiProbLocal) + "%\n" +
                string("Source: ") + sourceLocal;

            ShowAlert(msg);
        }

        lastAlert = alertLocal;
        ++g_tick;

        InvalidateRect(hwnd, NULL, TRUE);
    }
}

// ================= WINDOW PROC =================
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

        double cpu, mem, disk, aiProb;
        bool alertNow;
        string source;
        deque<double> cpuHist, memHist, diskHist;
        int cpuTh = GetInt("CPU_THRESHOLD", 80);
        int memTh = GetInt("MEM_THRESHOLD", 85);
        int diskTh = GetInt("DISK_THRESHOLD", 10);

        {
            lock_guard<mutex> lock(g_dataMutex);
            cpu = g_cpu;
            mem = g_mem;
            disk = g_disk;
            aiProb = g_aiProb;
            alertNow = g_alert;
            source = g_aiSource;
            cpuHist = g_cpuHist;
            memHist = g_memHist;
            diskHist = g_diskHist;
        }

        const int sidebarW = 250;
        const int pad = 18;
        const int headerH = 82;

        RECT sidebar{ 0, 0, sidebarW, client.bottom };
        DrawRoundedPanel(memDC, sidebar, RGB(11, 14, 20), RGB(22, 27, 35), 0);

        DrawTextAt(memDC, 20, 18, "AI Monitor", RGB(255, 255, 255), gFontTitle);
        DrawTextAt(memDC, 20, 56, "Predictive Server", RGB(160, 170, 185), gFontSmall);
        DrawTextAt(memDC, 20, 76, "Monitoring", RGB(160, 170, 185), gFontSmall);

        RECT modeBadge{ 18, 112, sidebarW - 18, 160 };
        DrawRoundedPanel(memDC, modeBadge, alertNow ? RGB(64, 24, 32) : RGB(25, 51, 44), alertNow ? RGB(231, 76, 60) : RGB(46, 204, 113), 18);
        DrawTextAt(memDC, 36, 128, alertNow ? "ALERT MODE" : "NORMAL MODE", RGB(255, 255, 255), gFontSection);

        DrawTextAt(memDC, 20, 190, "Thresholds", RGB(230, 235, 245), gFontSection);
        DrawTextAt(memDC, 20, 222, "CPU   " + to_string(cpuTh) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 246, "MEM   " + to_string(memTh) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 270, "DISK  " + to_string(diskTh) + "%", RGB(170, 180, 195), gFontSmall);

        DrawTextAt(memDC, 20, 334, "Current", RGB(230, 235, 245), gFontSection);
        DrawTextAt(memDC, 20, 366, "CPU   " + to_string((int)cpu) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 390, "MEM   " + to_string((int)mem) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 414, "DISK  " + to_string((int)disk) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 438, "AI    " + to_string((int)aiProb) + "%", RGB(170, 180, 195), gFontSmall);
        DrawTextAt(memDC, 20, 462, "SRC   " + source, RGB(170, 180, 195), gFontSmall);

        int mainX = sidebarW + pad;
        int mainW = client.right - mainX - pad;

        DrawTextAt(memDC, mainX, 16, "AI Monitoring Dashboard", RGB(238, 242, 249), gFontTitle);
        DrawTextAt(memDC, mainX, 42, "Live metrics, model prediction, alerts, and logs", RGB(140, 150, 165), gFontSmall);

        RECT statusBadge{ client.right - 220, 18, client.right - 20, 56 };
        DrawRoundedPanel(memDC, statusBadge, alertNow ? RGB(64, 24, 32) : RGB(25, 51, 44), alertNow ? RGB(231, 76, 60) : RGB(46, 204, 113), 18);
        DrawTextAt(memDC, client.right - 190, 29, alertNow ? "LIVE ALERT" : "SYSTEM OK", RGB(255, 255, 255), gFontSection);

        int rowY = headerH + 14;
        int cardH = 138;
        int gap = 14;
        int cardW = (mainW - (gap * 3)) / 4;

        DrawMetricCard(memDC, mainX + (cardW + gap) * 0, rowY, cardW, cardH, "CPU USAGE", cpu, cpu, RGB(52, 152, 219), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 1, rowY, cardW, cardH, "MEMORY", mem, mem, RGB(46, 204, 113), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 2, rowY, cardW, cardH, "DISK FREE", disk, disk, RGB(241, 196, 15), false);
        DrawMetricCard(memDC, mainX + (cardW + gap) * 3, rowY, cardW, cardH, "AI RISK", aiProb, aiProb, RGB(231, 76, 60), alertNow);

        int contentTop = rowY + cardH + 16;
        int graphW = mainW - 300 - 16;
        int graphH = client.bottom - contentTop - 20;

        RECT graphRc{ mainX, contentTop, mainX + graphW, contentTop + graphH };
        RECT insightRc{ mainX + graphW + 16, contentTop, client.right - pad, contentTop + graphH };

        DrawGraph(memDC, graphRc, cpuHist, memHist, diskHist);
        DrawInsightPanel(memDC, insightRc, cpu, mem, disk, aiProb, source, alertNow, cpuTh, memTh, diskTh);

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
        if (gFontTitle) DeleteObject(gFontTitle);
        if (gFontSection) DeleteObject(gFontSection);
        if (gFontValue) DeleteObject(gFontValue);
        if (gFontSmall) DeleteObject(gFontSmall);
        CloseDB();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ================= MAIN =================
int main() {
    LoadConfig();
    InitDB();

    HINSTANCE hInstance = GetModuleHandle(NULL);
    const wchar_t CLASS_NAME[] = L"MonitorWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

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
        CW_USEDEFAULT, CW_USEDEFAULT, 1260, 800,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        MessageBoxA(NULL, "Failed to create window.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    thread t(MonitorThread, hwnd);
    t.detach();

    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
