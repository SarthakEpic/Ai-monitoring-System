#include "UserIntent.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <unordered_set>

using namespace std;
namespace fs = std::filesystem;

namespace {
constexpr long long RECENT_APP_WINDOW_MS = 5LL * 60LL * 1000LL;
constexpr size_t MAX_RECENT_APPS = 8;

bool Contains(const string& text, const string& token) {
    return text.find(token) != string::npos;
}

string Join(const vector<string>& values) {
    ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) oss << ", ";
        oss << values[i];
    }
    return oss.str();
}
}

UserIntentSnapshot UserIntentEngine::Capture() {
    UserIntentSnapshot intent;
    intent.timestamp = static_cast<long long>(time(nullptr));
    intent.idleSeconds = ReadIdleSeconds();

    if (intent.idleSeconds < 30.0) {
        intent.userState = "ACTIVE";
    } else if (intent.idleSeconds < 300.0) {
        intent.userState = "IDLE";
    } else {
        intent.userState = "AWAY";
    }

    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        intent.reason = BuildReason(intent);
        return intent;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    intent.foregroundPid = pid;
    intent.isFullscreen = IsFullscreenWindow(foreground);

    wchar_t title[512]{};
    if (GetWindowTextW(foreground, title, 512) > 0) {
        intent.foregroundTitle = Narrow(title);
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        wchar_t pathBuffer[MAX_PATH * 4]{};
        DWORD pathSize = MAX_PATH * 4;
        if (QueryFullProcessImageNameW(process, 0, pathBuffer, &pathSize)) {
            intent.foregroundPath = Narrow(pathBuffer);
            intent.foregroundProcess = Basename(intent.foregroundPath);
        }
        CloseHandle(process);
    }

    if (intent.foregroundProcess.empty() || intent.foregroundProcess == "N/A") {
        intent.foregroundProcess = "pid_" + to_string(pid);
    }

    intent.appKind = ClassifyAppKind(intent.foregroundProcess, intent.foregroundTitle);
    intent.protectForegroundFamily =
        intent.appKind == "BROWSER" ||
        intent.appKind == "IDE" ||
        intent.appKind == "COMMUNICATION" ||
        intent.appKind == "GAME" ||
        intent.appKind == "MEDIA";

    const long long nowMs = NowMs();
    if (pid != previousForegroundPid_ || intent.foregroundTitle != previousForegroundTitle_) {
        previousForegroundPid_ = pid;
        previousForegroundTitle_ = intent.foregroundTitle;
        focusStartedMs_ = nowMs;
    }
    intent.focusDurationSeconds = focusStartedMs_ > 0 ? max(0.0, static_cast<double>(nowMs - focusStartedMs_) / 1000.0) : 0.0;

    RememberRecentApp(pid, intent.foregroundProcess, nowMs);
    FillRecentApps(intent, nowMs);

    intent.reason = BuildReason(intent);
    return intent;
}

long long UserIntentEngine::NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()
    ).count();
}

string UserIntentEngine::Narrow(const wchar_t* text) {
    if (!text || !*text) return "";

    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return "";

    string buffer(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer.data(), size, nullptr, nullptr);
    return string(buffer.c_str());
}

string UserIntentEngine::Narrow(const wstring& text) {
    return Narrow(text.c_str());
}

string UserIntentEngine::Lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });
    return value;
}

string UserIntentEngine::Basename(const string& path) {
    if (path.empty()) return "N/A";
    try {
        return fs::path(path).filename().string();
    } catch (...) {
        const size_t pos = path.find_last_of("\\/");
        return pos == string::npos ? path : path.substr(pos + 1);
    }
}

string UserIntentEngine::ClassifyAppKind(const string& processName, const string& title) {
    const string name = Lower(processName);
    const string lowerTitle = Lower(title);

    if (Contains(name, "chrome") || Contains(name, "msedge") || Contains(name, "firefox") ||
        Contains(name, "brave") || Contains(name, "opera") || Contains(name, "vivaldi")) {
        return "BROWSER";
    }
    if (Contains(name, "code") || Contains(name, "devenv") || Contains(name, "clion") ||
        Contains(name, "pycharm") || Contains(name, "idea") || Contains(name, "studio")) {
        return "IDE";
    }
    if (Contains(name, "teams") || Contains(name, "zoom") || Contains(name, "discord") ||
        Contains(name, "slack") || Contains(name, "meet")) {
        return "COMMUNICATION";
    }
    if (Contains(name, "vlc") || Contains(name, "spotify") || Contains(name, "wmplayer") ||
        Contains(lowerTitle, "youtube") || Contains(lowerTitle, "netflix")) {
        return "MEDIA";
    }
    if (Contains(name, "winword") || Contains(name, "excel") || Contains(name, "powerpnt") ||
        Contains(name, "onenote") || Contains(name, "acrobat")) {
        return "OFFICE";
    }
    if (Contains(name, "powershell") || Contains(name, "pwsh") || Contains(name, "cmd") ||
        Contains(name, "windowsterminal")) {
        return "TERMINAL";
    }
    if (Contains(name, "explorer")) {
        return "SHELL";
    }
    if (Contains(name, "steam") || Contains(name, "epicgames") || Contains(name, "riot") ||
        Contains(name, "minecraft") || Contains(lowerTitle, "game")) {
        return "GAME";
    }
    return "UNKNOWN";
}

string UserIntentEngine::BuildReason(const UserIntentSnapshot& intent) {
    vector<string> parts;
    if (intent.userState == "ACTIVE") parts.push_back("recent keyboard/mouse input");
    if (intent.userState == "IDLE") parts.push_back("user idle briefly");
    if (intent.userState == "AWAY") parts.push_back("user appears away");
    if (intent.foregroundPid != 0) parts.push_back("foreground " + intent.foregroundProcess);
    if (intent.isFullscreen) parts.push_back("fullscreen app protected");
    if (intent.protectForegroundFamily) parts.push_back("protect active " + intent.appKind + " family");
    return parts.empty() ? "no user intent signal" : Join(parts);
}

double UserIntentEngine::ReadIdleSeconds() {
    LASTINPUTINFO info{};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info)) return 0.0;

    const ULONGLONG now = GetTickCount64();
    if (now < info.dwTime) return 0.0;
    return static_cast<double>(now - info.dwTime) / 1000.0;
}

bool UserIntentEngine::IsFullscreenWindow(HWND hwnd) {
    if (!hwnd || IsIconic(hwnd)) return false;

    RECT windowRect{};
    if (!GetWindowRect(hwnd, &windowRect)) return false;

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return false;

    const RECT& monitorRect = monitorInfo.rcMonitor;
    return abs(windowRect.left - monitorRect.left) <= 2 &&
           abs(windowRect.top - monitorRect.top) <= 2 &&
           abs(windowRect.right - monitorRect.right) <= 2 &&
           abs(windowRect.bottom - monitorRect.bottom) <= 2;
}

void UserIntentEngine::RememberRecentApp(unsigned long pid, const string& processName, long long nowMs) {
    if (pid == 0 || processName.empty()) return;

    for (auto& app : recentApps_) {
        if (app.pid == pid) {
            app.process = processName;
            app.lastSeenMs = nowMs;
            return;
        }
    }

    recentApps_.push_front(RecentApp{ pid, processName, nowMs });
    while (recentApps_.size() > MAX_RECENT_APPS) {
        recentApps_.pop_back();
    }
}

void UserIntentEngine::FillRecentApps(UserIntentSnapshot& intent, long long nowMs) {
    unordered_set<unsigned long> seenPids;
    unordered_set<string> seenProcesses;

    for (auto it = recentApps_.begin(); it != recentApps_.end();) {
        if (nowMs - it->lastSeenMs > RECENT_APP_WINDOW_MS) {
            it = recentApps_.erase(it);
            continue;
        }

        if (seenPids.insert(it->pid).second) {
            intent.recentPids.push_back(it->pid);
        }
        if (!it->process.empty() && seenProcesses.insert(Lower(it->process)).second) {
            intent.recentProcesses.push_back(it->process);
        }
        ++it;
    }
}
