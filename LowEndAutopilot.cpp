#include "LowEndAutopilot.h"

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace std;

namespace {
double ClampPercent(double value) {
    return max(0.0, min(100.0, value));
}

string Lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });
    return value;
}

bool Contains(const string& value, const string& token) {
    return Lower(value).find(Lower(token)) != string::npos;
}

bool IsForegroundProtected(const ProcessSnapshot& process) {
    return process.isForeground ||
           process.isRecentlyActive ||
           process.matchesUserIntent ||
           process.safety == "USER_ACTIVE" ||
           process.category == "FOREGROUND_APP" ||
           process.category == "FOREGROUND_FULLSCREEN_APP" ||
           process.category == "ACTIVE_APP_FAMILY" ||
           process.category == "RECENT_USER_APP";
}

bool IsUserVisibleApp(const ProcessSnapshot& process) {
    return process.hasVisibleWindow ||
           Contains(process.category, "USER_APP") ||
           Contains(process.category, "FOREGROUND") ||
           Contains(process.category, "RECENT");
}

double CandidateRank(const ProcessSnapshot& process, const LowEndAutopilotConfig& config) {
    double rank = process.wasteScore * 1.25;
    rank += process.safetyScore * 0.70;
    rank += min(28.0, process.privateBytesMB / 18.0);
    rank += min(18.0, process.cpuPercent * 1.1);

    if (process.category == "UPDATER_SYNC" && config.delayUpdaterAndSyncApps) rank += 28.0;
    if (process.category == "BROWSER_CHILD" && config.sleepUnusedBrowserHelpers) rank += 24.0;
    if (process.category == "BACKGROUND_HELPER") rank += 14.0;
    if (!process.hasVisibleWindow) rank += 10.0;
    if (process.safety == "CANDIDATE") rank += 10.0;
    if (process.safety == "CAUTIOUS") rank -= 10.0;
    if (process.isSignedTrusted || process.isTrustedPath) rank += 4.0;

    return max(0.0, rank);
}

AutopilotRecommendation BuildRecommendation(
    const ProcessSnapshot& process,
    int rank,
    const LowEndAutopilotConfig& config
) {
    AutopilotRecommendation recommendation;
    recommendation.rank = rank;
    recommendation.targetPid = process.pid;
    recommendation.targetName = process.name;
    recommendation.category = process.category;
    recommendation.safety = process.safety;
    recommendation.safetyScore = process.safetyScore;
    recommendation.expectedRecoveredRamMB = max(0.0, process.expectedGainMB);
    recommendation.expectedCpuDropPercent = min(18.0, max(0.0, process.cpuPercent * 0.55));
    recommendation.foregroundProtected = true;
    recommendation.userAppTouched = IsUserVisibleApp(process);
    recommendation.reversible = true;
    recommendation.reversibility = "reversible";

    if (process.category == "UPDATER_SYNC" && config.delayUpdaterAndSyncApps) {
        recommendation.actionType = "sync_control";
        recommendation.actionName = "delay_background_updater";
        recommendation.reason = "Updater/sync work is background pressure and can usually be delayed safely.";
    } else if (process.category == "BROWSER_CHILD" && config.sleepUnusedBrowserHelpers) {
        recommendation.actionType = "browser_helper";
        recommendation.actionName = "sleep_unused_browser_helper";
        recommendation.reason = "Browser helper is not foreground; sleeping it is more reversible than closing the browser.";
    } else if (process.privateBytesMB >= 180.0 || process.workingSetMB >= 260.0) {
        recommendation.actionType = "memory_pressure";
        recommendation.actionName = "trim_background_working_set";
        recommendation.reason = "Background process has reclaimable memory pressure.";
    } else if (process.cpuPercent >= 8.0) {
        recommendation.actionType = "cpu_pressure";
        recommendation.actionName = "lower_background_priority";
        recommendation.reason = "Background CPU pressure can be reduced without touching the active app.";
    } else {
        recommendation.actionType = "observation";
        recommendation.actionName = "observe_background_candidate";
        recommendation.reason = "Candidate is safe enough to watch, but pressure is not high enough yet.";
    }

    if (!config.preferReversibleActions) {
        recommendation.reversibility = "policy_reversible_preferred";
    }

    return recommendation;
}

string BuildSummary(const LowEndAutopilotResult& result) {
    if (!result.enabled) return "Low-end autopilot disabled in config";
    if (!result.lowEndDevice && !result.active) return "Device is not under low-end pressure; observing only";
    if (result.actionsRecommended == 0) return "Foreground protected; no safe background optimization target found";

    ostringstream oss;
    oss << "Prepared " << result.actionsRecommended << " reversible background action";
    if (result.actionsRecommended != 1) oss << "s";
    oss << ", estimated RAM recovery " << static_cast<int>(result.estimatedRecoveredRamMB)
        << " MB, user apps touched " << result.userAppsTouched;
    return oss.str();
}
}

LowEndAutopilotResult LowEndAutopilotEngine::Evaluate(
    const SystemSnapshot& snapshot,
    const DecisionResult& decision,
    const HealPlan& plan,
    const AdaptiveBaselineResult& baseline,
    const LowEndAutopilotConfig& config
) const {
    LowEndAutopilotResult result;
    result.timestamp = snapshot.timestamp;
    result.enabled = config.enabled;
    result.lowEndDevice =
        config.forceLowEndDevice ||
        (snapshot.totalMemoryMB > 0.0 && snapshot.totalMemoryMB <= config.weakDeviceMemoryMB);
    result.mode = result.lowEndDevice ? "LOW_END_AUTOPILOT" : "STANDARD_AUTOPILOT";
    result.memoryPressure = ClampPercent(snapshot.memoryUsage);
    result.cpuPressure = ClampPercent(snapshot.cpuUsage);
    result.diskPressure = snapshot.diskFree <= config.diskFreePressureThreshold
        ? ClampPercent((config.diskFreePressureThreshold - snapshot.diskFree) * 12.5)
        : 0.0;
    result.pressureScore = ClampPercent(
        (result.memoryPressure * 0.46) +
        (result.cpuPressure * 0.30) +
        (decision.riskScore * 0.16) +
        (baseline.anomalyScore * 0.08) +
        result.diskPressure
    );
    result.foregroundProtected = config.protectForegroundApp;

    if (!config.enabled) {
        result.status = "DISABLED";
        result.summary = BuildSummary(result);
        return result;
    }

    const bool pressureActive =
        result.lowEndDevice ||
        snapshot.memoryUsage >= config.memoryPressureThreshold ||
        snapshot.cpuUsage >= config.cpuPressureThreshold ||
        snapshot.diskFree <= config.diskFreePressureThreshold ||
        decision.level != RiskLevel::Normal;
    result.active = pressureActive;

    vector<pair<double, AutopilotRecommendation>> ranked;
    for (const auto& process : snapshot.processGenome) {
        if (process.pid == 0) continue;
        if (config.protectForegroundApp && IsForegroundProtected(process)) continue;
        if (process.safety == "PROTECTED" || process.isSystemCritical) continue;
        if (process.safetyScore < config.minCandidateSafetyScore) continue;

        const bool interestingCategory =
            process.category == "UPDATER_SYNC" ||
            process.category == "BROWSER_CHILD" ||
            process.category == "BACKGROUND_HELPER";
        const bool pressureCandidate =
            process.wasteScore >= 35.0 ||
            process.privateBytesMB >= 160.0 ||
            process.cpuPercent >= 8.0 ||
            interestingCategory;

        if (!pressureCandidate) continue;

        AutopilotRecommendation recommendation = BuildRecommendation(process, 0, config);
        ranked.push_back({ CandidateRank(process, config), recommendation });
    }

    sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });

    const int maxActions = max(1, config.maxActionsPerCycle);
    for (const auto& [score, recommendation] : ranked) {
        if (static_cast<int>(result.recommendations.size()) >= maxActions) break;
        AutopilotRecommendation copy = recommendation;
        copy.rank = static_cast<int>(result.recommendations.size()) + 1;
        result.estimatedRecoveredRamMB += copy.expectedRecoveredRamMB;
        result.estimatedCpuDropPercent += copy.expectedCpuDropPercent;
        result.userAppsTouched += copy.userAppTouched ? 1 : 0;
        if (copy.reversible) ++result.reversibleActionCount;
        result.recommendations.push_back(copy);
    }

    result.actionsRecommended = static_cast<int>(result.recommendations.size());
    result.estimatedRecoveredRamMB = min(result.estimatedRecoveredRamMB, plan.expectedGainMB + 1536.0);
    result.estimatedCpuDropPercent = ClampPercent(min(result.estimatedCpuDropPercent, 45.0));
    result.quickRestoreAvailable = result.actionsRecommended > 0 && result.reversibleActionCount == result.actionsRecommended;

    if (!result.recommendations.empty()) {
        result.primaryAction = result.recommendations.front().actionName;
        result.primaryTarget = result.recommendations.front().targetName;
    }

    if (result.actionsRecommended > 0) {
        result.status = result.userAppsTouched == 0 ? "READY_DRY_RUN" : "REVIEW_REQUIRED";
        result.safetyNotes = "Foreground app is protected; only reversible background recommendations are prepared.";
    } else if (pressureActive) {
        result.status = "NO_SAFE_TARGET";
        result.safetyNotes = "Autopilot refused to touch foreground, recent, protected, or low-confidence processes.";
    } else {
        result.status = "OBSERVING";
        result.safetyNotes = "No pressure high enough for optimization planning.";
    }

    result.summary = BuildSummary(result);
    return result;
}
