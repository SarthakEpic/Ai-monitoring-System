#include "BackgroundAgent.h"

using namespace std;

BackgroundAgentResult BackgroundAgentEngine::Evaluate(
    const SystemSnapshot& snapshot,
    const LowEndAutopilotResult& autopilot,
    const BackgroundAgentConfig& config
) const {
    BackgroundAgentResult result;
    result.timestamp = snapshot.timestamp;
    result.enabled = config.enabled;
    result.trayIconReady = config.trayIconEnabled && config.trayIconInstalled;
    result.silentMonitoring = config.enabled && config.silentMonitoring;
    result.startOnBootConfigured = config.startOnBootConfigured;
    result.dashboardVisible = config.dashboardVisible;
    result.quickRestoreAvailable = config.quickRestoreEnabled && autopilot.quickRestoreAvailable;
    result.quickRestoreRequested = config.quickRestoreRequested;
    result.quickRestoreStatus = config.quickRestoreStatus;
    result.controlCenter = config.dashboardVisible ? "dashboard" : "tray";

    if (!config.enabled) {
        result.mode = "DASHBOARD";
        result.status = "DASHBOARD_ONLY";
        result.summary = "Background agent disabled; dashboard owns monitoring.";
        return result;
    }

    result.mode = config.startMinimized || !config.dashboardVisible ? "BACKGROUND_AGENT" : "CONTROL_CENTER";

    if (!result.trayIconReady) {
        result.status = "TRAY_PENDING";
        result.summary = "Agent monitoring is active, but tray icon is not installed.";
    } else if (result.quickRestoreRequested) {
        result.status = "RESTORE_REQUESTED";
        result.summary = "Quick restore was requested; reversible actions should be restored before new recommendations.";
    } else if (result.quickRestoreAvailable) {
        result.status = "AGENT_READY";
        result.summary = "Silent monitoring and quick restore are ready from the tray.";
    } else {
        result.status = "MONITORING";
        result.summary = "Silent background monitoring is active; no reversible action needs restore.";
    }

    return result;
}
