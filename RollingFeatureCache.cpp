#include "RollingFeatureCache.h"

#include <algorithm>

RollingFeatureCache::RollingFeatureCache(std::size_t capacity)
    : capacity_(std::max<std::size_t>(1, capacity)) {}

void RollingFeatureCache::Push(Series& series, double value) {
    series.values.push_back(value);
    series.sum += value;
    if (series.values.size() > capacity_) {
        series.sum -= series.values.front();
        series.values.pop_front();
    }
}

double RollingFeatureCache::Mean(const Series& series) {
    return series.values.empty() ? 0.0 : series.sum / static_cast<double>(series.values.size());
}

RollingFeatureSnapshot RollingFeatureCache::Push(const RollingFeatureFrame& frame) {
    Push(cpu_, frame.cpu);
    Push(memory_, frame.memory);
    Push(diskFree_, frame.diskFree);
    Push(network_, frame.network);
    Push(processCount_, frame.processCount);
    Push(topProcessCpu_, frame.topProcessCpu);
    Push(topProcessMemory_, frame.topProcessMemory);
    Push(responsivenessProxy_, frame.responsivenessProxy);
    return Snapshot();
}

RollingFeatureSnapshot RollingFeatureCache::Snapshot() const {
    RollingFeatureSnapshot snapshot;
    snapshot.cpu = cpu_.values;
    snapshot.memory = memory_.values;
    snapshot.diskFree = diskFree_.values;
    snapshot.network = network_.values;
    snapshot.processCount = processCount_.values;
    snapshot.topProcessCpu = topProcessCpu_.values;
    snapshot.topProcessMemory = topProcessMemory_.values;
    snapshot.responsivenessProxy = responsivenessProxy_.values;
    snapshot.means = {
        Mean(cpu_), Mean(memory_), Mean(diskFree_), Mean(network_), Mean(processCount_),
        Mean(topProcessCpu_), Mean(topProcessMemory_), Mean(responsivenessProxy_),
    };
    return snapshot;
}
