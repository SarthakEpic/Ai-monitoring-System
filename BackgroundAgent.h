#pragma once

#include <string>

#include "LowEndAutopilot.h"
#include "SystemMetrics.h"

struct BackgroundAgentConfig {
    bool enabled = true;
    bool trayIconEnabled = true;
    bool silentMonitoring = true;
    bool startOnBootConfigured = false;
    bool startMinimized = false;
    bool hideOnMinimize = true;
    bool quickRestoreEnabled = true;
    bool trayIconInstalled = false;
    bool dashboardVisible = true;
    bool quickRestoreRequested = false;
    std::string quickRestoreStatus = "IDLE";
};

struct BackgroundAgentResult {
    long long timestamp = 0;
    bool enabled = false;
    bool trayIconReady = false;
    bool silentMonitoring = false;
    bool startOnBootConfigured = false;
    bool dashboardVisible = true;
    bool quickRestoreAvailable = false;
    bool quickRestoreRequested = false;
    std::string mode = "DASHBOARD";
    std::string status = "DASHBOARD_ONLY";
    std::string summary = "Dashboard is the active control center";
    std::string quickRestoreStatus = "IDLE";
    std::string controlCenter = "dashboard";
};

class BackgroundAgentEngine {
public:
    BackgroundAgentResult Evaluate(
        const SystemSnapshot& snapshot,
        const LowEndAutopilotResult& autopilot,
        const BackgroundAgentConfig& config
    ) const;
};
