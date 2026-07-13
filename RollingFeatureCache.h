#pragma once

#include <cstddef>
#include <deque>

struct RollingFeatureFrame {
    double cpu = 0.0;
    double memory = 0.0;
    double diskFree = 0.0;
    double network = 0.0;
    double processCount = 0.0;
    double topProcessCpu = 0.0;
    double topProcessMemory = 0.0;
    double responsivenessProxy = 0.0;
};

struct RollingFeatureMeans {
    double cpu = 0.0;
    double memory = 0.0;
    double diskFree = 0.0;
    double network = 0.0;
    double processCount = 0.0;
    double topProcessCpu = 0.0;
    double topProcessMemory = 0.0;
    double responsivenessProxy = 0.0;
};

struct RollingFeatureSnapshot {
    std::deque<double> cpu;
    std::deque<double> memory;
    std::deque<double> diskFree;
    std::deque<double> network;
    std::deque<double> processCount;
    std::deque<double> topProcessCpu;
    std::deque<double> topProcessMemory;
    std::deque<double> responsivenessProxy;
    RollingFeatureMeans means;
};

// Bounded O(1) feature updates for the monitor and inference input. Snapshot
// copies are intentionally isolated so UI rendering cannot mutate the cache.
class RollingFeatureCache {
public:
    explicit RollingFeatureCache(std::size_t capacity);

    RollingFeatureSnapshot Push(const RollingFeatureFrame& frame);
    [[nodiscard]] RollingFeatureSnapshot Snapshot() const;
    [[nodiscard]] std::size_t Size() const { return cpu_.values.size(); }

private:
    struct Series {
        std::deque<double> values;
        double sum = 0.0;
    };

    void Push(Series& series, double value);
    [[nodiscard]] static double Mean(const Series& series);

    std::size_t capacity_;
    Series cpu_;
    Series memory_;
    Series diskFree_;
    Series network_;
    Series processCount_;
    Series topProcessCpu_;
    Series topProcessMemory_;
    Series responsivenessProxy_;
};
