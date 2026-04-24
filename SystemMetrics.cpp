#include "SystemMetrics.h"

#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cwchar>
#include <vector>

using namespace std;

namespace {
long long NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()
    ).count();
}
}

WindowsMetricsCollector::WindowsMetricsCollector() {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    processorCount_ = max(1UL, systemInfo.dwNumberOfProcessors);
}

bool WindowsMetricsCollector::Initialize() {
    return ReadCpuTimes(previousCpuTimes_);
}

SystemSnapshot WindowsMetricsCollector::Collect() {
    SystemSnapshot snapshot;
    snapshot.timestamp = static_cast<long long>(time(nullptr));

    CpuTimes currentCpuTimes{};
    if (ReadCpuTimes(currentCpuTimes)) {
        if (hasPreviousCpuTimes_) {
            unsigned long long idle = currentCpuTimes.idle - previousCpuTimes_.idle;
            unsigned long long total =
                (currentCpuTimes.kernel - previousCpuTimes_.kernel) +
                (currentCpuTimes.user - previousCpuTimes_.user);

            if (total > 0) {
                snapshot.cpuUsage = ClampPercent(100.0 * static_cast<double>(total - idle) / static_cast<double>(total));
            }
        }
        previousCpuTimes_ = currentCpuTimes;
        hasPreviousCpuTimes_ = true;
    }

    snapshot.memoryUsage = ReadMemoryUsage();
    snapshot.diskFree = ReadDiskFreePercent(L"C:\\");
    ReadNetworkRates(snapshot.netDownKBps, snapshot.netUpKBps);
    ReadProcessMetrics(snapshot.processCount, snapshot.topProcess);
    return snapshot;
}

unsigned long long WindowsMetricsCollector::FileTimeToULL(const FILETIME& time) {
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

string WindowsMetricsCollector::Narrow(const wchar_t* text) {
    if (!text || !*text) return "N/A";

    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return "N/A";

    string buffer(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer.data(), size, nullptr, nullptr);
    return string(buffer.c_str());
}

double WindowsMetricsCollector::ClampPercent(double value) {
    return max(0.0, min(100.0, value));
}

bool WindowsMetricsCollector::ReadCpuTimes(CpuTimes& times) const {
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) return false;
    times.idle = FileTimeToULL(idle);
    times.kernel = FileTimeToULL(kernel);
    times.user = FileTimeToULL(user);
    return true;
}

double WindowsMetricsCollector::ReadMemoryUsage() const {
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) return 0.0;
    return ClampPercent(static_cast<double>(mem.dwMemoryLoad));
}

double WindowsMetricsCollector::ReadDiskFreePercent(const wchar_t* path) const {
    ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFree{};
    if (!GetDiskFreeSpaceExW(path, &freeBytes, &totalBytes, &totalFree)) return 0.0;
    if (totalBytes.QuadPart == 0) return 0.0;
    return ClampPercent(100.0 * static_cast<double>(totalFree.QuadPart) / static_cast<double>(totalBytes.QuadPart));
}

void WindowsMetricsCollector::ReadNetworkRates(double& downKBps, double& upKBps) {
    downKBps = 0.0;
    upKBps = 0.0;

    ULONG tableSize = 0;
    if (GetIfTable(nullptr, &tableSize, FALSE) != ERROR_INSUFFICIENT_BUFFER || tableSize == 0) {
        return;
    }

    vector<unsigned char> buffer(tableSize);
    auto* table = reinterpret_cast<MIB_IFTABLE*>(buffer.data());
    if (GetIfTable(table, &tableSize, FALSE) != NO_ERROR) {
        return;
    }

    unsigned long long bytesIn = 0;
    unsigned long long bytesOut = 0;

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IFROW& row = table->table[i];
        if (row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL) continue;
        if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        bytesIn += row.dwInOctets;
        bytesOut += row.dwOutOctets;
    }

    long long nowMs = NowMs();
    if (hasPreviousNetworkTotals_) {
        double seconds = max(0.001, static_cast<double>(nowMs - previousNetworkSampleMs_) / 1000.0);
        downKBps = max(0.0, static_cast<double>(bytesIn - previousBytesIn_) / 1024.0 / seconds);
        upKBps = max(0.0, static_cast<double>(bytesOut - previousBytesOut_) / 1024.0 / seconds);
    }

    previousBytesIn_ = bytesIn;
    previousBytesOut_ = bytesOut;
    previousNetworkSampleMs_ = nowMs;
    hasPreviousNetworkTotals_ = true;
}

void WindowsMetricsCollector::ReadProcessMetrics(int& processCount, ProcessSummary& topProcess) {
    processCount = 0;
    topProcess = ProcessSummary{};

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    unordered_map<unsigned long, unsigned long long> currentProcessTimes;
    const long long nowMs = NowMs();
    const double sampleSeconds =
        previousProcessSampleMs_ > 0 ? max(0.001, static_cast<double>(nowMs - previousProcessSampleMs_) / 1000.0) : 0.0;

    auto maybeUpdateTopProcess = [&](const ProcessSummary& candidate) {
        if (candidate.cpuPercent > topProcess.cpuPercent + 0.001) {
            topProcess = candidate;
            return;
        }
        if (candidate.cpuPercent > 0.0 && abs(candidate.cpuPercent - topProcess.cpuPercent) <= 0.001 &&
            candidate.memoryMB > topProcess.memoryMB) {
            topProcess = candidate;
            return;
        }
        if (topProcess.cpuPercent <= 0.0 && candidate.memoryMB > topProcess.memoryMB) {
            topProcess = candidate;
        }
    };

    if (Process32FirstW(snapshot, &entry)) {
        do {
            ++processCount;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
            if (!process) continue;

            FILETIME createTime{}, exitTime{}, kernelTime{}, userTime{};
            if (!GetProcessTimes(process, &createTime, &exitTime, &kernelTime, &userTime)) {
                CloseHandle(process);
                continue;
            }

            const unsigned long long cpuTime = FileTimeToULL(kernelTime) + FileTimeToULL(userTime);
            currentProcessTimes[entry.th32ProcessID] = cpuTime;

            PROCESS_MEMORY_COUNTERS_EX memoryCounters{};
            double memoryMB = 0.0;
            if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memoryCounters), sizeof(memoryCounters))) {
                memoryMB = static_cast<double>(memoryCounters.WorkingSetSize) / (1024.0 * 1024.0);
            }

            double cpuPercent = 0.0;
            auto previousIt = previousProcessTimes_.find(entry.th32ProcessID);
            if (sampleSeconds > 0.0 && previousIt != previousProcessTimes_.end() && cpuTime >= previousIt->second) {
                const double deltaCpuSeconds = static_cast<double>(cpuTime - previousIt->second) / 10000000.0;
                cpuPercent = ClampPercent(100.0 * deltaCpuSeconds / (sampleSeconds * static_cast<double>(processorCount_)));
            }

            ProcessSummary candidate;
            candidate.pid = entry.th32ProcessID;
            candidate.name = Narrow(entry.szExeFile);
            candidate.cpuPercent = cpuPercent;
            candidate.memoryMB = memoryMB;
            maybeUpdateTopProcess(candidate);

            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);

    previousProcessTimes_.swap(currentProcessTimes);
    previousProcessSampleMs_ = nowMs;
}
