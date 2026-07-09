#pragma once

#include <string>
#include <vector>

struct ProcessSnapshot {
    unsigned long pid = 0;
    unsigned long parentPid = 0;
    std::string name = "N/A";
    std::string exePath;
    std::string windowTitle;

    double cpuPercent = 0.0;
    double workingSetMB = 0.0;
    double privateBytesMB = 0.0;
    double ioReadKBps = 0.0;
    double ioWriteKBps = 0.0;
    double lifetimeSeconds = 0.0;

    int threadCount = 0;
    int handleCount = 0;
    unsigned long priorityClass = 0;
    unsigned long sessionId = 0;

    bool isForeground = false;
    bool isRecentlyActive = false;
    bool matchesUserIntent = false;
    bool hasVisibleWindow = false;
    bool isTrustedPath = false;
    bool isSignedTrusted = false;
    bool isSystemCritical = false;

    std::string signatureStatus = "not_checked";
    std::string category = "UNKNOWN";
    std::string safety = "UNKNOWN";
    std::string intentRole = "none";
    std::string recommendation = "observe";
    std::string reason = "insufficient process context";
    std::vector<std::string> reasons;

    double wasteScore = 0.0;
    double importanceScore = 50.0;
    double safetyScore = 0.0;
    double expectedGainMB = 0.0;
};
