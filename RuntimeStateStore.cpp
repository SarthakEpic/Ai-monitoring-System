#include "RuntimeStateStore.h"

#include <algorithm>

RuntimeSnapshot RuntimeStateStore::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void RuntimeStateStore::SetMode(RuntimeMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.mode = mode;
    IncrementRevision();
}

void RuntimeStateStore::SetRuntimeStarted(bool started) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.runtimeStarted = started;
    IncrementRevision();
}

void RuntimeStateStore::SetComponentHealth(ComponentHealth health) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(snapshot_.componentHealth.begin(), snapshot_.componentHealth.end(), [&](const ComponentHealth& current) {
        return current.component == health.component;
    });
    if (it == snapshot_.componentHealth.end()) {
        snapshot_.componentHealth.push_back(std::move(health));
    } else {
        *it = std::move(health);
    }
    IncrementRevision();
}

void RuntimeStateStore::SetErrorCosts(AsymmetricErrorCostMatrix errorCosts) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.errorCosts = errorCosts;
    IncrementRevision();
}

void RuntimeStateStore::IncrementRevision() {
    ++snapshot_.revision;
}
