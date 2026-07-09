#include "DecisionEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <sstream>
#include <vector>

using namespace std;

namespace {
struct CauseSignal {
    string name = "none";
    string detail = "No dominant pressure source";
    double score = 0.0;
};

double ClampPercent(double value) {
    return max(0.0, min(100.0, value));
}

string Lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });
    return value;
}

string Trim(const string& value) {
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

vector<string> SplitCsv(const string& csv) {
    vector<string> values;
    string token;
    istringstream stream(csv);
    while (getline(stream, token, ',')) {
        token = Lower(Trim(token));
        if (!token.empty()) values.push_back(token);
    }
    return values;
}

bool ContainsProcessName(const vector<string>& names, const string& processName) {
    const string lowered = Lower(processName);
    return find(names.begin(), names.end(), lowered) != names.end();
}

double ComputeThresholdPressure(double cpu, double mem, double disk, int cpuTh, int memTh, int diskTh) {
    double cpuScore = ClampPercent((cpu / max(1, cpuTh)) * 100.0);
    double memScore = ClampPercent((mem / max(1, memTh)) * 100.0);
    double diskScore = 0.0;
    if (disk < diskTh) {
        diskScore = ClampPercent(((diskTh - disk) / max(1, diskTh)) * 100.0);
    }
    return ClampPercent((cpuScore * 0.42) + (memScore * 0.42) + (diskScore * 0.16));
}

double ComputeDeviationScore(const deque<double>& history) {
    if (history.size() < 5) return 0.0;

    vector<double> values(history.begin(), history.end() - 1);
    if (values.empty()) return 0.0;

    const double mean = accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    double variance = 0.0;
    for (double value : values) {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size());
    const double stddev = sqrt(max(variance, 0.0));
    const double current = history.back();
    const double deviation = abs(current - mean);

    if (stddev < 1e-6) {
        return deviation > 0.0 ? 50.0 : 0.0;
    }

    const double normalized = deviation / max(1.0, stddev * 2.0);
    return ClampPercent(normalized * 100.0);
}

double ComputeAnomalyScore(const SystemSnapshot& snapshot, const DecisionContext& context) {
    const double cpuDeviation = ComputeDeviationScore(context.cpuHistory);
    const double memDeviation = ComputeDeviationScore(context.memHistory);
    const double diskDeviation = ComputeDeviationScore(context.diskHistory);
    const double netDeviation = ComputeDeviationScore(context.netHistory);
    const double procDeviation = ComputeDeviationScore(context.processHistory);

    double topProcessBoost = 0.0;
    if (snapshot.topProcess.cpuPercent >= 45.0) topProcessBoost += 15.0;
    if (snapshot.topProcess.memoryMB >= 500.0) topProcessBoost += 10.0;
    if (snapshot.topProcess.wasteScore >= 70.0) topProcessBoost += 8.0;

    return ClampPercent(
        (cpuDeviation * 0.28) +
        (memDeviation * 0.24) +
        (diskDeviation * 0.18) +
        (netDeviation * 0.15) +
        (procDeviation * 0.15) +
        topProcessBoost
    );
}

double ResourcePressureAbove(double value, double threshold) {
    if (threshold <= 0.0) return 0.0;
    if (value < threshold) return ClampPercent((value / threshold) * 45.0);
    return ClampPercent(55.0 + ((value - threshold) / max(1.0, 100.0 - threshold)) * 45.0);
}

CauseSignal DetectRootCause(
    const SystemSnapshot& snapshot,
    const DecisionContext& context,
    const DecisionThresholds& thresholds
) {
    vector<CauseSignal> signals;

    const double cpuPressure = ResourcePressureAbove(snapshot.cpuUsage, thresholds.cpuThreshold);
    signals.push_back({
        "cpu",
        "CPU " + to_string(static_cast<int>(snapshot.cpuUsage)) + "% vs threshold " + to_string(thresholds.cpuThreshold) + "%",
        cpuPressure
    });

    const double memoryPressure = ResourcePressureAbove(snapshot.memoryUsage, thresholds.memThreshold);
    signals.push_back({
        "memory",
        "Memory " + to_string(static_cast<int>(snapshot.memoryUsage)) + "% vs threshold " + to_string(thresholds.memThreshold) + "%",
        memoryPressure
    });

    double diskPressure = 0.0;
    if (snapshot.diskFree <= thresholds.diskThreshold) {
        diskPressure = ClampPercent(55.0 + ((thresholds.diskThreshold - snapshot.diskFree) / max(1, thresholds.diskThreshold)) * 45.0);
    } else {
        diskPressure = ClampPercent(((100.0 - snapshot.diskFree) / 100.0) * 35.0);
    }
    signals.push_back({
        "disk",
        "Disk free " + to_string(static_cast<int>(snapshot.diskFree)) + "% vs minimum " + to_string(thresholds.diskThreshold) + "%",
        diskPressure
    });

    const double networkTotal = snapshot.netDownKBps + snapshot.netUpKBps;
    const double networkAnomaly = ComputeDeviationScore(context.netHistory);
    const double networkPressure = ClampPercent(networkAnomaly + min(35.0, networkTotal / 512.0));
    signals.push_back({
        "network",
        "Network " + to_string(static_cast<int>(networkTotal)) + " KB/s with anomaly " + to_string(static_cast<int>(networkAnomaly)) + "%",
        networkPressure
    });

    const double processAnomaly = ComputeDeviationScore(context.processHistory);
    const double processPressure = ClampPercent(processAnomaly + max(0.0, (snapshot.processCount - 180) / 2.0));
    signals.push_back({
        "process_count",
        "Process count " + to_string(snapshot.processCount) + " with anomaly " + to_string(static_cast<int>(processAnomaly)) + "%",
        processPressure
    });

    const double topProcessPressure = ClampPercent(
        (snapshot.topProcess.cpuPercent * 0.9) +
        min(35.0, snapshot.topProcess.privateMemoryMB / 20.0) +
        (snapshot.topProcess.wasteScore * 0.45)
    );
    signals.push_back({
        "process",
        "Top pressure process " + snapshot.topProcess.name + " waste " + to_string(static_cast<int>(snapshot.topProcess.wasteScore)) + "%",
        topProcessPressure
    });

    return *max_element(signals.begin(), signals.end(), [](const CauseSignal& a, const CauseSignal& b) {
        return a.score < b.score;
    });
}

bool IsPolicyProtected(const ProcessSnapshot& process) {
    return process.safety == "PROTECTED" ||
           process.safety == "USER_ACTIVE" ||
           process.isSystemCritical ||
           process.isForeground ||
           process.matchesUserIntent ||
           process.isRecentlyActive;
}

double CandidateRank(const ProcessSnapshot& process) {
    double rank = process.wasteScore;
    rank += process.safetyScore * 0.65;
    rank += min(18.0, process.expectedGainMB / 32.0);
    rank += min(16.0, process.cpuPercent * 0.8);
    rank += min(14.0, process.privateBytesMB / 90.0);
    rank -= process.importanceScore * 0.40;

    if (process.safety == "CAUTIOUS") rank -= 14.0;
    if (process.safety == "OBSERVE_ONLY") rank -= 28.0;
    if (IsPolicyProtected(process)) rank -= 150.0;
    if (process.expectedGainMB <= 0.0 && process.cpuPercent < 12.0) rank -= 10.0;

    return rank;
}

OptimizationCandidate ToCandidate(
    const ProcessSnapshot& process,
    const vector<string>& allowlist,
    const vector<string>& denylist
) {
    OptimizationCandidate candidate;
    candidate.pid = process.pid;
    candidate.name = process.name.empty() ? "N/A" : process.name;
    candidate.category = process.category;
    candidate.safety = process.safety;
    candidate.intentRole = process.intentRole;
    candidate.reason = process.reason;
    candidate.cpuPercent = process.cpuPercent;
    candidate.memoryMB = process.workingSetMB;
    candidate.privateMemoryMB = process.privateBytesMB;
    candidate.wasteScore = process.wasteScore;
    candidate.safetyScore = process.safetyScore;
    candidate.expectedGainMB = process.expectedGainMB;
    candidate.rank = CandidateRank(process);
    candidate.protectedByUserIntent = process.isForeground || process.matchesUserIntent || process.isRecentlyActive;
    candidate.deniedByPolicy = ContainsProcessName(denylist, process.name);
    candidate.allowedByPolicy = !allowlist.empty() && ContainsProcessName(allowlist, process.name);
    return candidate;
}

OptimizationCandidate SelectOptimizationCandidate(
    const SystemSnapshot& snapshot,
    const vector<string>& allowlist,
    const vector<string>& denylist,
    int& candidateCount
) {
    OptimizationCandidate best;
    bool hasBest = false;
    candidateCount = 0;

    for (const auto& process : snapshot.processGenome) {
        OptimizationCandidate candidate = ToCandidate(process, allowlist, denylist);
        if (candidate.deniedByPolicy || candidate.protectedByUserIntent || IsPolicyProtected(process)) {
            continue;
        }
        if (candidate.safetyScore < 35.0 && process.safety != "CANDIDATE") {
            continue;
        }
        ++candidateCount;
        if (!hasBest || candidate.rank > best.rank) {
            best = candidate;
            hasBest = true;
        }
    }

    return hasBest ? best : OptimizationCandidate{};
}

string BuildSummary(const DecisionResult& result) {
    ostringstream oss;
    if (result.level == RiskLevel::Critical) {
        oss << "Critical risk";
    } else if (result.level == RiskLevel::Warning) {
        oss << "Warning risk";
    } else {
        oss << "System stable";
    }

    oss << ": " << result.rootCause;
    if (!result.actionTarget.empty() && result.actionTarget != "system") {
        oss << " -> " << result.actionTarget;
    }
    return oss.str();
}

string ActionForCause(const string& rootCause, const OptimizationCandidate& candidate) {
    if (rootCause == "disk") return "review_disk_cleanup";
    if (rootCause == "network") return "review_network_activity";
    if (candidate.pid == 0) return "increase_observation";
    if (rootCause == "cpu") return "dry_run_lower_priority";
    if (rootCause == "memory") return "dry_run_memory_trim";
    if (rootCause == "process" || rootCause == "process_count") return "dry_run_sleep_candidate";
    return "increase_observation";
}

string CooldownKey(const DecisionResult& result) {
    return result.recommendedAction + ":" + Lower(result.actionTarget);
}
}

DecisionResult DecisionEngine::Evaluate(
    const SystemSnapshot& snapshot,
    double aiProbability,
    double aiConfidence,
    const string& aiSource,
    const string& aiReason,
    const DecisionThresholds& thresholds,
    const DecisionContext& context,
    const DecisionPolicy& policy
) {
    DecisionResult result;

    result.pressureScore = ComputeThresholdPressure(
        snapshot.cpuUsage,
        snapshot.memoryUsage,
        snapshot.diskFree,
        thresholds.cpuThreshold,
        thresholds.memThreshold,
        thresholds.diskThreshold
    );
    result.anomalyScore = ComputeAnomalyScore(snapshot, context);

    const double confidenceFactor = ClampPercent(aiConfidence) / 100.0;
    double aiWeight = 0.20;
    if (aiSource == "MODEL") aiWeight = 0.35 + (0.30 * confidenceFactor);
    else if (aiSource == "FALLBACK") aiWeight = 0.35;

    const double anomalyWeight = 0.25;
    const double pressureWeight = max(0.0, 1.0 - aiWeight - anomalyWeight);

    result.riskScore =
        ClampPercent((aiProbability * aiWeight) + (result.anomalyScore * anomalyWeight) + (result.pressureScore * pressureWeight));

    const bool emergency =
        snapshot.cpuUsage >= 97.0 ||
        snapshot.memoryUsage >= 97.0 ||
        snapshot.diskFree <= 3.0;

    if (emergency || result.riskScore >= thresholds.criticalRiskThreshold) {
        result.level = RiskLevel::Critical;
    } else if (result.riskScore >= thresholds.warningRiskThreshold) {
        result.level = RiskLevel::Warning;
    } else {
        result.level = RiskLevel::Normal;
    }

    const CauseSignal rootCause = DetectRootCause(snapshot, context, thresholds);
    result.rootCause = rootCause.name;
    result.rootCauseDetail = rootCause.detail;
    result.reason = (!aiReason.empty() && aiReason != "N/A") ? aiReason : rootCause.detail;

    const vector<string> allowlist = SplitCsv(policy.allowlistCsv);
    const vector<string> denylist = SplitCsv(policy.denylistCsv);
    result.candidate = SelectOptimizationCandidate(snapshot, allowlist, denylist, result.candidateCount);
    result.candidateSafetyScore = result.candidate.safetyScore;
    result.expectedGainMB = result.candidate.expectedGainMB;
    result.actionConfidence = ClampPercent((aiConfidence * 0.45) + (result.candidate.safetyScore * 0.35) + (result.riskScore * 0.20));

    if (result.level == RiskLevel::Normal) {
        result.recommendedAction = "monitor_only";
        result.actionTarget = "system";
        result.safetyGate = "OBSERVE_ONLY";
        result.blockedReason = "risk below warning threshold";
    } else {
        result.recommendedAction = ActionForCause(result.rootCause, result.candidate);
        result.actionTarget = result.candidate.pid ? result.candidate.name : result.rootCause;
        result.actionTargetPid = result.candidate.pid;

        if (result.rootCause == "disk" || result.rootCause == "network") {
            result.safetyGate = "USER_REVIEW_REQUIRED";
            result.blockedReason = "resource-level action needs user approval";
        } else if (result.candidate.pid == 0) {
            result.safetyGate = "NO_SAFE_TARGET";
            result.blockedReason = "no safe background process candidate found";
        } else if (result.candidate.deniedByPolicy) {
            result.safetyGate = "DENYLIST_BLOCKED";
            result.blockedReason = "target is denied by safety policy";
        } else if (policy.autoHealEnabled && !result.candidate.allowedByPolicy) {
            result.safetyGate = "ALLOWLIST_REQUIRED";
            result.blockedReason = "target is not in allowlist";
        } else if (!policy.autoHealEnabled) {
            result.safetyGate = "DRY_RUN_ONLY";
            result.blockedReason = "auto-heal disabled in config";
        } else if (policy.safeMode || policy.dryRun) {
            result.safetyGate = "DRY_RUN_ONLY";
            result.blockedReason = "safe mode or dry-run mode is enabled";
        } else if (result.level != RiskLevel::Critical) {
            result.safetyGate = "WARNING_OBSERVE";
            result.blockedReason = "warning level cannot execute healing";
        } else if (aiSource != "MODEL" || aiConfidence < 85.0) {
            result.safetyGate = "MODEL_CONFIDENCE_BLOCKED";
            result.blockedReason = "model confidence below healing gate";
        } else if (result.candidate.safetyScore < 80.0) {
            result.safetyGate = "PROCESS_SAFETY_BLOCKED";
            result.blockedReason = "candidate process safety score below healing gate";
        } else {
            result.safetyGate = "READY";
            result.blockedReason = "all safety gates passed";
            result.safeToHeal = true;
            result.dryRun = false;
        }
    }

    const bool hasCooldownTarget =
        result.actionTargetPid != 0 &&
        result.recommendedAction != "monitor_only" &&
        result.safetyGate != "NO_SAFE_TARGET";

    if (hasCooldownTarget && policy.cooldownSeconds > 0) {
        const string key = CooldownKey(result);
        const auto it = lastRecommendationAt_.find(key);
        if (it != lastRecommendationAt_.end()) {
            const long long elapsed = max(0LL, snapshot.timestamp - it->second);
            if (elapsed < policy.cooldownSeconds) {
                result.cooldownActive = true;
                result.cooldownRemainingSeconds = static_cast<int>(policy.cooldownSeconds - elapsed);
                result.safetyGate = "COOLDOWN";
                result.blockedReason = "cooldown active for this target";
                result.safeToHeal = false;
                result.dryRun = true;
            } else {
                lastRecommendationAt_[key] = snapshot.timestamp;
            }
        } else {
            lastRecommendationAt_[key] = snapshot.timestamp;
        }
    }

    result.summary = BuildSummary(result);
    return result;
}

const char* DecisionEngine::ToString(RiskLevel level) {
    switch (level) {
    case RiskLevel::Normal:
        return "NORMAL";
    case RiskLevel::Warning:
        return "WARNING";
    case RiskLevel::Critical:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}
