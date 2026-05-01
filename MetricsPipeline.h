#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>

#include "MetricsStorage.h"

class MetricsPipeline {
public:
    MetricsPipeline() = default;
    ~MetricsPipeline();

    bool Start(MetricsStorage* storage, std::size_t batchSize, std::chrono::milliseconds flushInterval);
    void Stop();
    void Enqueue(const SystemSnapshot& snapshot);
    std::size_t PendingCount() const;

private:
    void WorkerLoop();

    MetricsStorage* storage_ = nullptr;
    std::size_t batchSize_ = 1;
    std::chrono::milliseconds flushInterval_{1000};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<SystemSnapshot> queue_;
    bool running_ = false;
    std::thread worker_;
};
