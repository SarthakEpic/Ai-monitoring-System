#include "ProcessGenome.h"

#include <algorithm>

#include "ProcessClassifier.h"

using namespace std;

void ProcessGenomeEngine::Annotate(vector<ProcessSnapshot>& processes) {
    for (auto& process : processes) {
        ProcessClassifier::Classify(process);
    }
}

ProcessSummary ProcessGenomeEngine::SelectTopPressureProcess(const vector<ProcessSnapshot>& processes) {
    if (processes.empty()) return ProcessSummary{};

    const ProcessSnapshot* best = nullptr;
    double bestRank = -1.0;

    for (const auto& process : processes) {
        const double rank = CandidateRank(process);
        if (!best || rank > bestRank) {
            best = &process;
            bestRank = rank;
        }
    }

    return best ? ToSummary(*best) : ProcessSummary{};
}

vector<ProcessSnapshot> ProcessGenomeEngine::TopCandidates(const vector<ProcessSnapshot>& processes, size_t limit) {
    vector<ProcessSnapshot> ranked = processes;
    stable_sort(ranked.begin(), ranked.end(), [](const ProcessSnapshot& a, const ProcessSnapshot& b) {
        return CandidateRank(a) > CandidateRank(b);
    });

    if (ranked.size() > limit) {
        ranked.resize(limit);
    }
    return ranked;
}

double ProcessGenomeEngine::CandidateRank(const ProcessSnapshot& process) {
    double rank = process.wasteScore;
    rank += process.safetyScore * 0.55;
    rank -= process.importanceScore * 0.35;

    if (process.safety == "PROTECTED") rank -= 100.0;
    if (process.safety == "USER_ACTIVE") rank -= 80.0;
    if (process.safety == "OBSERVE_ONLY") rank -= 25.0;
    if (process.expectedGainMB >= 250.0) rank += 8.0;
    if (process.cpuPercent >= 15.0) rank += 6.0;

    return rank;
}

ProcessSummary ProcessGenomeEngine::ToSummary(const ProcessSnapshot& process) {
    ProcessSummary summary;
    summary.pid = process.pid;
    summary.parentPid = process.parentPid;
    summary.name = process.name.empty() ? "N/A" : process.name;
    summary.exePath = process.exePath;
    summary.cpuPercent = process.cpuPercent;
    summary.memoryMB = process.workingSetMB;
    summary.privateMemoryMB = process.privateBytesMB;
    summary.ioReadKBps = process.ioReadKBps;
    summary.ioWriteKBps = process.ioWriteKBps;
    summary.lifetimeSeconds = process.lifetimeSeconds;
    summary.category = process.category;
    summary.safety = process.safety;
    summary.recommendation = process.recommendation;
    summary.reason = process.reason;
    summary.wasteScore = process.wasteScore;
    summary.importanceScore = process.importanceScore;
    summary.safetyScore = process.safetyScore;
    summary.expectedGainMB = process.expectedGainMB;
    summary.isForeground = process.isForeground;
    summary.hasVisibleWindow = process.hasVisibleWindow;
    summary.isTrustedPath = process.isTrustedPath;
    summary.isSignedTrusted = process.isSignedTrusted;
    return summary;
}
