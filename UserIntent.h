#pragma once

#include <windows.h>

#include <deque>
#include <string>
#include <vector>

struct UserIntentSnapshot {
    long long timestamp = 0;
    unsigned long foregroundPid = 0;
    std::string foregroundProcess = "N/A";
    std::string foregroundPath;
    std::string foregroundTitle;
    std::string appKind = "UNKNOWN";
    std::string userState = "UNKNOWN";
    std::string reason = "No foreground intent captured";
    double idleSeconds = 0.0;
    double focusDurationSeconds = 0.0;
    bool isFullscreen = false;
    bool protectForegroundFamily = false;
    std::vector<unsigned long> recentPids;
    std::vector<std::string> recentProcesses;
};

class UserIntentEngine {
public:
    UserIntentSnapshot Capture();

private:
    struct RecentApp {
        unsigned long pid = 0;
        std::string process;
        long long lastSeenMs = 0;
    };

    static long long NowMs();
    static std::string Narrow(const wchar_t* text);
    static std::string Narrow(const std::wstring& text);
    static std::string Lower(std::string value);
    static std::string Basename(const std::string& path);
    static std::string ClassifyAppKind(const std::string& processName, const std::string& title);
    static std::string BuildReason(const UserIntentSnapshot& intent);
    static double ReadIdleSeconds();
    static bool IsFullscreenWindow(HWND hwnd);

    void RememberRecentApp(unsigned long pid, const std::string& processName, long long nowMs);
    void FillRecentApps(UserIntentSnapshot& intent, long long nowMs);

    unsigned long previousForegroundPid_ = 0;
    std::string previousForegroundTitle_;
    long long focusStartedMs_ = 0;
    std::deque<RecentApp> recentApps_;
};
