#include "ProcessClassifier.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

using namespace std;

namespace {
double ClampScore(double value) {
    return max(0.0, min(100.0, value));
}

void AddReason(ProcessSnapshot& process, const string& reason) {
    if (reason.empty()) return;
    process.reasons.push_back(reason);
}

string JoinReasons(const vector<string>& reasons) {
    if (reasons.empty()) return "no strong signal yet";

    ostringstream oss;
    for (size_t i = 0; i < reasons.size() && i < 3; ++i) {
        if (i) oss << ", ";
        oss << reasons[i];
    }
    return oss.str();
}

double ResourceWaste(const ProcessSnapshot& process) {
    const double ioTotal = process.ioReadKBps + process.ioWriteKBps;
    double score = 0.0;
    score += min(40.0, process.cpuPercent * 1.8);
    score += min(38.0, process.privateBytesMB / 18.0);
    score += min(22.0, ioTotal / 250.0);
    if (!process.hasVisibleWindow) score += 8.0;
    if (process.lifetimeSeconds > 300.0 && process.privateBytesMB > 250.0) score += 6.0;
    return ClampScore(score);
}
}

void ProcessClassifier::Classify(ProcessSnapshot& process, const UserIntentSnapshot& intent) {
    process.reasons.clear();

    const string name = Lower(process.name);
    const string path = Lower(process.exePath);

    const bool foregroundIntent = process.pid != 0 && process.pid == intent.foregroundPid;
    const bool recentIntent = IsRecentPid(process, intent);
    const bool sameIntentFamily = IsSameProcessFamily(process, intent);
    const bool critical = process.pid <= 4 || IsCriticalProcessName(name);
    const bool security = IsSecurityProcessName(name);
    const bool service = IsLikelyWindowsService(process);
    const bool browser = IsBrowserProcessName(name);
    const bool updater = IsUpdaterOrSyncName(name);
    const bool trusted = process.isTrustedPath || process.isSignedTrusted;

    process.isForeground = process.isForeground || foregroundIntent;
    process.isRecentlyActive = recentIntent;
    process.matchesUserIntent = foregroundIntent || recentIntent || sameIntentFamily;
    if (foregroundIntent) process.intentRole = intent.isFullscreen ? "foreground_fullscreen" : "foreground";
    else if (sameIntentFamily) process.intentRole = "active_app_family";
    else if (recentIntent) process.intentRole = "recent_app";
    else process.intentRole = "none";

    process.isSystemCritical = critical;
    process.wasteScore = ResourceWaste(process);

    if (process.cpuPercent >= 20.0) AddReason(process, "high cpu");
    if (process.privateBytesMB >= 350.0) AddReason(process, "high private memory");
    if (process.workingSetMB >= 500.0) AddReason(process, "large working set");
    if (process.ioReadKBps + process.ioWriteKBps >= 1024.0) AddReason(process, "active disk io");
    if (!process.hasVisibleWindow) AddReason(process, "background or hidden");
    if (process.isForeground) AddReason(process, "foreground user app");
    if (process.isRecentlyActive) AddReason(process, "recently active user app");
    if (sameIntentFamily) AddReason(process, "same family as active " + intent.appKind);
    if (intent.isFullscreen && foregroundIntent) AddReason(process, "fullscreen foreground app");
    if (trusted) AddReason(process, "trusted install path/signature");

    if (critical) {
        process.category = "SYSTEM_CRITICAL";
        process.safety = "PROTECTED";
        process.importanceScore = 100.0;
        process.safetyScore = 0.0;
        process.expectedGainMB = 0.0;
        process.recommendation = "protect";
        AddReason(process, "critical windows process");
    } else if (security) {
        process.category = "SECURITY";
        process.safety = "PROTECTED";
        process.importanceScore = 100.0;
        process.safetyScore = 0.0;
        process.expectedGainMB = 0.0;
        process.recommendation = "protect";
        AddReason(process, "security/antivirus process");
    } else if (foregroundIntent && intent.isFullscreen) {
        process.category = "FOREGROUND_FULLSCREEN_APP";
        process.safety = "USER_ACTIVE";
        process.importanceScore = 100.0;
        process.safetyScore = 0.0;
        process.expectedGainMB = 0.0;
        process.recommendation = "protect_fullscreen_app";
    } else if (process.isForeground) {
        process.category = "FOREGROUND_APP";
        process.safety = "USER_ACTIVE";
        process.importanceScore = 95.0;
        process.safetyScore = 5.0;
        process.expectedGainMB = 0.0;
        process.recommendation = "protect_foreground";
    } else if (sameIntentFamily) {
        process.category = "ACTIVE_APP_FAMILY";
        process.safety = "USER_ACTIVE";
        process.importanceScore = 88.0;
        process.safetyScore = 8.0;
        process.expectedGainMB = 0.0;
        process.recommendation = "protect_active_app_family";
    } else if (recentIntent) {
        process.category = "RECENT_USER_APP";
        process.safety = "OBSERVE_ONLY";
        process.importanceScore = 82.0;
        process.safetyScore = 18.0;
        process.expectedGainMB = 0.0;
        process.recommendation = "observe_recent_user_app";
    } else if (process.hasVisibleWindow) {
        process.category = "USER_APP";
        process.safety = "OBSERVE_ONLY";
        process.importanceScore = 78.0;
        process.safetyScore = 25.0;
        process.expectedGainMB = 0.0;
        process.recommendation = "observe_visible_app";
    } else if (browser) {
        process.category = "BROWSER_CHILD";
        process.safety = trusted ? "CANDIDATE" : "CAUTIOUS";
        process.importanceScore = 42.0;
        process.safetyScore = trusted ? 68.0 : 45.0;
        process.expectedGainMB = max(0.0, min(process.privateBytesMB * 0.55, process.workingSetMB * 0.45));
        process.recommendation = process.wasteScore >= 55.0 ? "sleep_or_trim_candidate" : "observe_browser_child";
        AddReason(process, "browser background process");
    } else if (updater) {
        process.category = "UPDATER_SYNC";
        process.safety = trusted ? "CANDIDATE" : "CAUTIOUS";
        process.importanceScore = 30.0;
        process.safetyScore = trusted ? 76.0 : 48.0;
        process.expectedGainMB = max(0.0, min(process.privateBytesMB * 0.60, process.workingSetMB * 0.50));
        process.recommendation = process.wasteScore >= 45.0 ? "delay_or_lower_priority_candidate" : "observe_updater";
        AddReason(process, "updater/sync style process");
    } else if (service) {
        process.category = "WINDOWS_SERVICE";
        process.safety = "OBSERVE_ONLY";
        process.importanceScore = 72.0;
        process.safetyScore = 20.0;
        process.expectedGainMB = 0.0;
        process.recommendation = "observe_service";
        AddReason(process, "windows service context");
    } else if (trusted) {
        process.category = "BACKGROUND_HELPER";
        process.safety = "CANDIDATE";
        process.importanceScore = 38.0;
        process.safetyScore = 62.0;
        process.expectedGainMB = max(0.0, min(process.privateBytesMB * 0.50, process.workingSetMB * 0.40));
        process.recommendation = process.wasteScore >= 50.0 ? "lower_priority_or_trim_candidate" : "observe_background_helper";
    } else {
        process.category = "UNKNOWN";
        process.safety = "CAUTIOUS";
        process.importanceScore = 58.0;
        process.safetyScore = 35.0;
        process.expectedGainMB = 0.0;
        process.recommendation = "observe_unknown";
        AddReason(process, "unknown process identity");
    }

    process.reason = JoinReasons(process.reasons);
}

bool ProcessClassifier::ContainsToken(const string& text, const string& token) {
    return text.find(token) != string::npos;
}

bool ProcessClassifier::IsCriticalProcessName(const string& name) {
    static const unordered_set<string> names = {
        "system", "registry", "idle", "smss.exe", "csrss.exe", "wininit.exe",
        "winlogon.exe", "services.exe", "lsass.exe", "svchost.exe", "fontdrvhost.exe",
        "dwm.exe", "spoolsv.exe", "audiodg.exe", "conhost.exe", "wudfhost.exe"
    };
    return names.find(name) != names.end();
}

bool ProcessClassifier::IsSecurityProcessName(const string& name) {
    static const unordered_set<string> names = {
        "msmpeng.exe", "securityhealthservice.exe", "securityhealthsystray.exe",
        "nissrv.exe", "senseir.exe", "mssense.exe", "mpdefendercoreservice.exe"
    };
    return names.find(name) != names.end() ||
           ContainsToken(name, "antivirus") ||
           ContainsToken(name, "defender") ||
           ContainsToken(name, "firewall");
}

bool ProcessClassifier::IsBrowserProcessName(const string& name) {
    static const unordered_set<string> names = {
        "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", "opera.exe",
        "vivaldi.exe", "browser.exe"
    };
    return names.find(name) != names.end();
}

bool ProcessClassifier::IsUpdaterOrSyncName(const string& name) {
    return ContainsToken(name, "update") ||
           ContainsToken(name, "updater") ||
           ContainsToken(name, "onedrive") ||
           ContainsToken(name, "googledrive") ||
           ContainsToken(name, "dropbox") ||
           ContainsToken(name, "steam") ||
           ContainsToken(name, "adobe") ||
           ContainsToken(name, "teams") ||
           ContainsToken(name, "slack") ||
           ContainsToken(name, "sync");
}

bool ProcessClassifier::IsLikelyWindowsService(const ProcessSnapshot& process) {
    const string path = Lower(process.exePath);
    return process.sessionId == 0 ||
           ContainsToken(path, "\\windows\\system32\\") ||
           ContainsToken(path, "\\windows\\syswow64\\");
}

bool ProcessClassifier::IsRecentPid(const ProcessSnapshot& process, const UserIntentSnapshot& intent) {
    return find(intent.recentPids.begin(), intent.recentPids.end(), process.pid) != intent.recentPids.end();
}

bool ProcessClassifier::IsSameProcessFamily(const ProcessSnapshot& process, const UserIntentSnapshot& intent) {
    if (!intent.protectForegroundFamily) return false;
    if (intent.foregroundProcess.empty() || process.name.empty()) return false;
    if (process.pid == intent.foregroundPid) return true;
    return Lower(process.name) == Lower(intent.foregroundProcess);
}

string ProcessClassifier::Lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });
    return value;
}
