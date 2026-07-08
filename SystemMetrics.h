#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "ProcessSnapshot.h"
#include "ProcessTelemetry.h"

struct ProcessSummary {
    unsigned long pid = 0;
    unsigned long parentPid = 0;
    std::string name = "N/A";
    std::string exePath;
    double cpuPercent = 0.0;
    double memoryMB = 0.0;
    double privateMemoryMB = 0.0;
    double ioReadKBps = 0.0;
    double ioWriteKBps = 0.0;
    double lifetimeSeconds = 0.0;
    double wasteScore = 0.0;
    double importanceScore = 50.0;
    double safetyScore = 0.0;
    double expectedGainMB = 0.0;
    std::string category = "UNKNOWN";
    std::string safety = "UNKNOWN";
    std::string recommendation = "observe";
    std::string reason = "insufficient process context";
    bool isForeground = false;
    bool hasVisibleWindow = false;
    bool isTrustedPath = false;
    bool isSignedTrusted = false;
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
    std::vector<ProcessSnapshot> processGenome;
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
    void ReadProcessGenome(SystemSnapshot& snapshot);

    CpuTimes previousCpuTimes_{};
    bool hasPreviousCpuTimes_ = false;

    unsigned long long previousBytesIn_ = 0;
    unsigned long long previousBytesOut_ = 0;
    bool hasPreviousNetworkTotals_ = false;
    long long previousNetworkSampleMs_ = 0;

    unsigned long processorCount_ = 1;
    ProcessTelemetryCollector processTelemetry_;
};
