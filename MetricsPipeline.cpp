#include "MetricsPipeline.h"

#include <utility>
#include <vector>

using namespace std;

MetricsPipeline::~MetricsPipeline() {
    Stop();
}

bool MetricsPipeline::Start(MetricsStorage* storage, size_t batchSize, chrono::milliseconds flushInterval) {
    Stop();

    if (!storage || !storage->IsReady()) return false;

    {
        lock_guard<mutex> lock(mutex_);
        storage_ = storage;
        batchSize_ = max<size_t>(1, batchSize);
        flushInterval_ = max(chrono::milliseconds(100), flushInterval);
        running_ = true;
    }

    worker_ = thread(&MetricsPipeline::WorkerLoop, this);
    return true;
}

void MetricsPipeline::Stop() {
    {
        lock_guard<mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }

    lock_guard<mutex> lock(mutex_);
    queue_.clear();
    storage_ = nullptr;
}

void MetricsPipeline::Enqueue(const SystemSnapshot& snapshot) {
    {
        lock_guard<mutex> lock(mutex_);
        if (!running_) return;
        queue_.push_back(snapshot);
    }
    cv_.notify_one();
}

size_t MetricsPipeline::PendingCount() const {
    lock_guard<mutex> lock(mutex_);
    return queue_.size();
}

void MetricsPipeline::WorkerLoop() {
    vector<SystemSnapshot> batch;
    batch.reserve(batchSize_);

    unique_lock<mutex> lock(mutex_);
    while (running_ || !queue_.empty()) {
        cv_.wait_for(lock, flushInterval_, [&] {
            return !running_ || queue_.size() >= batchSize_;
        });

        if (queue_.empty()) {
            continue;
        }

        while (!queue_.empty() && batch.size() < batchSize_) {
            batch.push_back(queue_.front());
            queue_.pop_front();
        }

        MetricsStorage* storage = storage_;
        lock.unlock();
        if (storage && !batch.empty()) {
            storage->LogBatch(batch);
        }
        batch.clear();
        lock.lock();
    }
}
