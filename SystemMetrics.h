#pragma once

#include <windows.h>

#include <string>
#include <unordered_map>

struct ProcessSummary {
    unsigned long pid = 0;
    std::string name = "N/A";
    double cpuPercent = 0.0;
    double memoryMB = 0.0;
};

struct SystemSnapshot {
    long long timestamp = 0;
    double cpuUsage = 0.0;
    double memoryUsage = 0.0;
    double diskFree = 0.0;
    double netDownKBps = 0.0;
    double netUpKBps = 0.0;
    int processCount = 0;
    std::string scenarioLabel = "auto";
    ProcessSummary topProcess;
};

class WindowsMetricsCollector {
public:
    WindowsMetricsCollector();
    bool Initialize();
    SystemSnapshot Collect();

private:
    struct CpuTimes {
        unsigned long long idle = 0;
        unsigned long long kernel = 0;
        unsigned long long user = 0;
    };

    static unsigned long long FileTimeToULL(const FILETIME& time);
    static std::string Narrow(const wchar_t* text);
    static double ClampPercent(double value);
    bool ReadCpuTimes(CpuTimes& times) const;
    double ReadMemoryUsage() const;
    double ReadDiskFreePercent(const wchar_t* path) const;
    void ReadNetworkRates(double& downKBps, double& upKBps);
    void ReadProcessMetrics(int& processCount, ProcessSummary& topProcess);

    CpuTimes previousCpuTimes_{};
    bool hasPreviousCpuTimes_ = false;

    unsigned long long previousBytesIn_ = 0;
    unsigned long long previousBytesOut_ = 0;
    bool hasPreviousNetworkTotals_ = false;
    long long previousNetworkSampleMs_ = 0;

    unsigned long processorCount_ = 1;
    long long previousProcessSampleMs_ = 0;
    std::unordered_map<unsigned long, unsigned long long> previousProcessTimes_;
};
