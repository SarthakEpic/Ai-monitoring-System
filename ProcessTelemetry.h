#pragma once

#include <windows.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "ProcessSnapshot.h"

class ProcessTelemetryCollector {
public:
    struct WindowDetails {
        bool hasVisibleWindow = false;
        bool isForeground = false;
        std::wstring title;
    };

    ProcessTelemetryCollector();

    std::vector<ProcessSnapshot> Collect();

private:
    struct ProcessCounters {
        unsigned long long cpuTime = 0;
        unsigned long long readBytes = 0;
        unsigned long long writeBytes = 0;
    };

    struct SignatureDetails {
        bool checked = false;
        bool trusted = false;
        std::string status = "not_checked";
    };

    static unsigned long long FileTimeToULL(const FILETIME& time);
    static long long NowMs();
    static std::string Narrow(const wchar_t* text);
    static std::string Narrow(const std::wstring& text);
    static double ClampPercent(double value);
    static std::wstring ToLower(std::wstring value);
    static bool StartsWithInsensitive(const std::wstring& value, const std::wstring& prefix);
    static bool IsTrustedInstallPath(const std::wstring& path);
    static bool VerifyAuthenticodeTrust(const std::wstring& path);
    static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam);

    std::unordered_map<unsigned long, ProcessCounters> previousCounters_;
    std::unordered_map<std::wstring, SignatureDetails> signatureCache_;
    long long previousSampleMs_ = 0;
    unsigned long processorCount_ = 1;
};
