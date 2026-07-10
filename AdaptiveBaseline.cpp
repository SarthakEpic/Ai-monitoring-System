#include "AdaptiveBaseline.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace std;

namespace {
double Clamp(double value, double low, double high) {
    return max(low, min(value, high));
}

string StatusForScore(bool ready, double score) {
    if (!ready) return "WARMING_UP";
    if (score >= 75.0) return "ANOMALOUS";
    if (score >= 45.0) return "SHIFTING";
    return "STABLE";
}

string BuildSummary(const string& status, const string& metric, double score) {
    if (status == "WARMING_UP") return "Learning this device baseline";
    if (status == "STABLE") return "Current load matches this device baseline";

    ostringstream oss;
    oss << metric << " is above this device baseline";
    if (status == "ANOMALOUS") {
        oss << " with strong deviation";
    } else {
        oss << " with moderate drift";
    }
    oss << " (" << static_cast<int>(score) << "%)";
    return oss.str();
}
}

AdaptiveBaselineEngine::MetricEvaluation AdaptiveBaselineEngine::EvaluateMetric(
    BaselineMetricState& state,
    double value,
    bool lowerIsRisk,
    double minStdDev,
    double sensitivity
) {
    if (state.samples == 0) {
        state.mean = value;
        state.variance = minStdDev * minStdDev;
        state.samples = 1;
        return { state.mean, 0.0, 0.0 };
    }

    const double stddev = max(minStdDev, sqrt(max(0.0, state.variance)));
    const double signedDelta = lowerIsRisk ? (state.mean - value) : (value - state.mean);
    const double pressure = max(0.0, signedDelta / max(1.0, stddev));
    const double deviation = Clamp((pressure / max(0.75, sensitivity)) * 35.0, 0.0, 100.0);

    double alpha = state.samples < 30 ? 0.08 : 0.025;
    if (deviation >= 70.0) {
        alpha = 0.006;
    } else if (deviation >= 45.0) {
        alpha = 0.012;
    }

    const double rawDelta = value - state.mean;
    state.mean += alpha * rawDelta;
    state.variance = ((1.0 - alpha) * state.variance) + (alpha * rawDelta * rawDelta);
    state.variance = max(state.variance, minStdDev * minStdDev);
    ++state.samples;

    return { state.mean, deviation, pressure };
}

AdaptiveBaselineResult AdaptiveBaselineEngine::EvaluateAndUpdate(
    const SystemSnapshot& snapshot,
    int minSamples,
    double sensitivity
) {
    sensitivity = Clamp(sensitivity, 0.6, 3.0);

    AdaptiveBaselineResult result;
    result.timestamp = snapshot.timestamp;

    const double networkTotal = snapshot.netDownKBps + snapshot.netUpKBps;
    const double topMemoryMB = max(snapshot.topProcess.privateMemoryMB, snapshot.topProcess.memoryMB);

    const MetricEvaluation cpu = EvaluateMetric(cpu_, snapshot.cpuUsage, false, 4.0, sensitivity);
    const MetricEvaluation memory = EvaluateMetric(memory_, snapshot.memoryUsage, false, 3.0, sensitivity);
    const MetricEvaluation disk = EvaluateMetric(diskFree_, snapshot.diskFree, true, 2.0, sensitivity);
    const MetricEvaluation network = EvaluateMetric(network_, networkTotal, false, 64.0, sensitivity);
    const MetricEvaluation process = EvaluateMetric(processCount_, static_cast<double>(snapshot.processCount), false, 6.0, sensitivity);
    const MetricEvaluation topCpu = EvaluateMetric(topCpu_, snapshot.topProcess.cpuPercent, false, 5.0, sensitivity);
    const MetricEvaluation topMemory = EvaluateMetric(topMemory_, topMemoryMB, false, 64.0, sensitivity);

    result.sampleCount = min({
        cpu_.samples,
        memory_.samples,
        diskFree_.samples,
        network_.samples,
        processCount_.samples,
        topCpu_.samples,
        topMemory_.samples
    });
    result.ready = result.sampleCount >= max(5, minSamples);
    result.confidence = result.ready ? Clamp((static_cast<double>(result.sampleCount) / max(1, minSamples * 3)) * 100.0, 45.0, 100.0) : Clamp((static_cast<double>(result.sampleCount) / max(1, minSamples)) * 45.0, 0.0, 45.0);

    result.cpuMean = cpu.mean;
    result.memoryMean = memory.mean;
    result.diskFreeMean = disk.mean;
    result.networkMean = network.mean;
    result.processCountMean = process.mean;
    result.topCpuMean = topCpu.mean;
    result.topMemoryMean = topMemory.mean;
    result.cpuDeviation = cpu.deviation;
    result.memoryDeviation = memory.deviation;
    result.diskDeviation = disk.deviation;
    result.networkDeviation = network.deviation;
    result.processDeviation = process.deviation;
    result.topCpuDeviation = topCpu.deviation;
    result.topMemoryDeviation = topMemory.deviation;

    struct MetricScore {
        string name;
        double score;
    };
    vector<MetricScore> scores = {
        { "cpu", result.cpuDeviation },
        { "memory", result.memoryDeviation },
        { "disk", result.diskDeviation },
        { "network", result.networkDeviation },
        { "process_count", result.processDeviation },
        { "top_process_cpu", result.topCpuDeviation },
        { "top_process_memory", result.topMemoryDeviation },
    };

    const auto dominant = max_element(scores.begin(), scores.end(), [](const MetricScore& a, const MetricScore& b) {
        return a.score < b.score;
    });

    result.dominantMetric = dominant == scores.end() ? "none" : dominant->name;
    result.anomalyScore = Clamp(
        (result.cpuDeviation * 0.18) +
        (result.memoryDeviation * 0.20) +
        (result.diskDeviation * 0.16) +
        (result.networkDeviation * 0.12) +
        (result.processDeviation * 0.12) +
        (result.topCpuDeviation * 0.12) +
        (result.topMemoryDeviation * 0.10),
        0.0,
        100.0
    );

    if (!result.ready) {
        result.anomalyScore = Clamp(result.anomalyScore * 0.35, 0.0, 35.0);
    }

    result.status = StatusForScore(result.ready, result.anomalyScore);
    result.riskHint = result.ready ? result.anomalyScore : 0.0;
    result.riskAdjustment = result.ready ? Clamp((result.anomalyScore - 40.0) * 0.18, -4.0, 10.0) : 0.0;
    result.summary = BuildSummary(result.status, result.dominantMetric, result.anomalyScore);

    return result;
}
