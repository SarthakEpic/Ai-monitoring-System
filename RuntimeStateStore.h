#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "RuntimeFoundation.h"

struct RuntimeSnapshot {
    std::uint64_t revision = 0;
    RuntimeMode mode = RuntimeMode::MonitorOnly;
    std::vector<ComponentHealth> componentHealth;
    AsymmetricErrorCostMatrix errorCosts;
    bool runtimeStarted = false;
};

class RuntimeStateStore {
public:
    RuntimeSnapshot Snapshot() const;
    void SetMode(RuntimeMode mode);
    void SetRuntimeStarted(bool started);
    void SetComponentHealth(ComponentHealth health);
    void SetErrorCosts(AsymmetricErrorCostMatrix errorCosts);

private:
    void IncrementRevision();

    mutable std::mutex mutex_;
    RuntimeSnapshot snapshot_;
};
