#include "ProcessTelemetry.h"

#include <psapi.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <wintrust.h>

#include <algorithm>
#include <chrono>
#include <cwctype>

using namespace std;

namespace {
constexpr int SIGNATURE_CHECK_BUDGET_PER_SAMPLE = 3;

struct EnumWindowState {
    HWND foreground = nullptr;
    unordered_map<unsigned long, ProcessTelemetryCollector::WindowDetails>* windows = nullptr;
};
}

ProcessTelemetryCollector::ProcessTelemetryCollector() {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    processorCount_ = max(1UL, systemInfo.dwNumberOfProcessors);
}

vector<ProcessSnapshot> ProcessTelemetryCollector::Collect() {
    vector<ProcessSnapshot> processes;

    unordered_map<unsigned long, WindowDetails> windowDetails;
    EnumWindowState windowState{ GetForegroundWindow(), &windowDetails };
    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&windowState));

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    unordered_map<unsigned long, ProcessCounters> currentCounters;
    const long long nowMs = NowMs();
    const double sampleSeconds =
        previousSampleMs_ > 0 ? max(0.001, static_cast<double>(nowMs - previousSampleMs_) / 1000.0) : 0.0;

    int signatureChecksRemaining = SIGNATURE_CHECK_BUDGET_PER_SAMPLE;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            ProcessSnapshot item;
            item.pid = entry.th32ProcessID;
            item.parentPid = entry.th32ParentProcessID;
            item.name = Narrow(entry.szExeFile);
            item.threadCount = static_cast<int>(entry.cntThreads);

            const auto windowIt = windowDetails.find(entry.th32ProcessID);
            if (windowIt != windowDetails.end()) {
                item.hasVisibleWindow = windowIt->second.hasVisibleWindow;
                item.isForeground = windowIt->second.isForeground;
                item.windowTitle = Narrow(windowIt->second.title);
            }

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
            if (process) {
                wchar_t pathBuffer[MAX_PATH * 4]{};
                DWORD pathSize = MAX_PATH * 4;
                if (QueryFullProcessImageNameW(process, 0, pathBuffer, &pathSize)) {
                    item.exePath = Narrow(pathBuffer);
                    const wstring path(pathBuffer);
                    item.isTrustedPath = IsTrustedInstallPath(path);

                    auto sigIt = signatureCache_.find(path);
                    if (sigIt == signatureCache_.end() && signatureChecksRemaining > 0) {
                        SignatureDetails details;
                        details.checked = true;
                        details.trusted = VerifyAuthenticodeTrust(path);
                        details.status = details.trusted ? "trusted_signature" : "untrusted_or_unsigned";
                        sigIt = signatureCache_.emplace(path, details).first;
                        --signatureChecksRemaining;
                    }

                    if (sigIt != signatureCache_.end()) {
                        item.isSignedTrusted = sigIt->second.trusted;
                        item.signatureStatus = sigIt->second.status;
                    }
                }

                FILETIME createTime{}, exitTime{}, kernelTime{}, userTime{};
                if (GetProcessTimes(process, &createTime, &exitTime, &kernelTime, &userTime)) {
                    const unsigned long long cpuTime = FileTimeToULL(kernelTime) + FileTimeToULL(userTime);
                    currentCounters[item.pid].cpuTime = cpuTime;

                    auto previousIt = previousCounters_.find(item.pid);
                    if (sampleSeconds > 0.0 && previousIt != previousCounters_.end() && cpuTime >= previousIt->second.cpuTime) {
                        const double deltaCpuSeconds = static_cast<double>(cpuTime - previousIt->second.cpuTime) / 10000000.0;
                        item.cpuPercent = ClampPercent(100.0 * deltaCpuSeconds / (sampleSeconds * static_cast<double>(processorCount_)));
                    }

                    FILETIME nowFileTime{};
                    GetSystemTimeAsFileTime(&nowFileTime);
                    const unsigned long long nowFt = FileTimeToULL(nowFileTime);
                    const unsigned long long createdFt = FileTimeToULL(createTime);
                    if (nowFt > createdFt) {
                        item.lifetimeSeconds = static_cast<double>(nowFt - createdFt) / 10000000.0;
                    }
                }

                PROCESS_MEMORY_COUNTERS_EX memoryCounters{};
                if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memoryCounters), sizeof(memoryCounters))) {
                    item.workingSetMB = static_cast<double>(memoryCounters.WorkingSetSize) / (1024.0 * 1024.0);
                    item.privateBytesMB = static_cast<double>(memoryCounters.PrivateUsage) / (1024.0 * 1024.0);
                }

                IO_COUNTERS ioCounters{};
                if (GetProcessIoCounters(process, &ioCounters)) {
                    currentCounters[item.pid].readBytes = ioCounters.ReadTransferCount;
                    currentCounters[item.pid].writeBytes = ioCounters.WriteTransferCount;

                    auto previousIt = previousCounters_.find(item.pid);
                    if (sampleSeconds > 0.0 && previousIt != previousCounters_.end()) {
                        if (ioCounters.ReadTransferCount >= previousIt->second.readBytes) {
                            item.ioReadKBps = static_cast<double>(ioCounters.ReadTransferCount - previousIt->second.readBytes) / 1024.0 / sampleSeconds;
                        }
                        if (ioCounters.WriteTransferCount >= previousIt->second.writeBytes) {
                            item.ioWriteKBps = static_cast<double>(ioCounters.WriteTransferCount - previousIt->second.writeBytes) / 1024.0 / sampleSeconds;
                        }
                    }
                }

                DWORD handleCount = 0;
                if (GetProcessHandleCount(process, &handleCount)) {
                    item.handleCount = static_cast<int>(handleCount);
                }

                DWORD sessionId = 0;
                if (ProcessIdToSessionId(item.pid, &sessionId)) {
                    item.sessionId = sessionId;
                }

                item.priorityClass = GetPriorityClass(process);
                CloseHandle(process);
            }

            processes.push_back(item);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);

    previousCounters_.swap(currentCounters);
    previousSampleMs_ = nowMs;

    return processes;
}

unsigned long long ProcessTelemetryCollector::FileTimeToULL(const FILETIME& time) {
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

long long ProcessTelemetryCollector::NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()
    ).count();
}

string ProcessTelemetryCollector::Narrow(const wchar_t* text) {
    if (!text || !*text) return "";

    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return "";

    string buffer(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer.data(), size, nullptr, nullptr);
    return string(buffer.c_str());
}

string ProcessTelemetryCollector::Narrow(const wstring& text) {
    return Narrow(text.c_str());
}

double ProcessTelemetryCollector::ClampPercent(double value) {
    return max(0.0, min(100.0, value));
}

wstring ProcessTelemetryCollector::ToLower(wstring value) {
    transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

bool ProcessTelemetryCollector::StartsWithInsensitive(const wstring& value, const wstring& prefix) {
    const wstring lowerValue = ToLower(value);
    const wstring lowerPrefix = ToLower(prefix);
    return lowerValue.rfind(lowerPrefix, 0) == 0;
}

bool ProcessTelemetryCollector::IsTrustedInstallPath(const wstring& path) {
    if (path.empty()) return false;

    wchar_t windowsDir[MAX_PATH]{};
    wchar_t programFiles[MAX_PATH]{};
    wchar_t programFilesX86[MAX_PATH]{};

    GetWindowsDirectoryW(windowsDir, MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86, MAX_PATH);

    return StartsWithInsensitive(path, windowsDir) ||
           (programFiles[0] && StartsWithInsensitive(path, programFiles)) ||
           (programFilesX86[0] && StartsWithInsensitive(path, programFilesX86));
}

bool ProcessTelemetryCollector::VerifyAuthenticodeTrust(const wstring& path) {
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.dwStateAction = WTD_STATEACTION_IGNORE;
    trustData.dwProvFlags = WTD_SAFER_FLAG | WTD_CACHE_ONLY_URL_RETRIEVAL;
    trustData.pFile = &fileInfo;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(nullptr, &action, &trustData);
    return status == ERROR_SUCCESS;
}

BOOL CALLBACK ProcessTelemetryCollector::EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    auto* state = reinterpret_cast<EnumWindowState*>(lParam);
    auto& details = (*state->windows)[pid];
    details.hasVisibleWindow = true;
    details.isForeground = details.isForeground || hwnd == state->foreground;

    if (details.title.empty()) {
        wchar_t title[256]{};
        if (GetWindowTextW(hwnd, title, 256) > 0) {
            details.title = title;
        }
    }

    return TRUE;
}
