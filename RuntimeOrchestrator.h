#pragma once

#include <memory>
#include <vector>

#include "RuntimeFoundation.h"
#include "RuntimeStateStore.h"

class IRuntimeComponent {
public:
    virtual ~IRuntimeComponent() = default;

    virtual RuntimeComponent Id() const = 0;
    virtual RuntimeStatus Start() = 0;
    virtual void Stop() noexcept = 0;
};

class RuntimeOrchestrator {
public:
    RuntimeOrchestrator(RuntimeStateStore& stateStore, std::vector<std::unique_ptr<IRuntimeComponent>> components);
    ~RuntimeOrchestrator();

    RuntimeOrchestrator(const RuntimeOrchestrator&) = delete;
    RuntimeOrchestrator& operator=(const RuntimeOrchestrator&) = delete;

    RuntimeStatus Start();
    void Stop() noexcept;
    [[nodiscard]] bool IsRunning() const { return running_; }

private:
    void RecordHealth(RuntimeComponent component, ComponentHealthState state, RuntimeStatus status) const;

    RuntimeStateStore& stateStore_;
    std::vector<std::unique_ptr<IRuntimeComponent>> components_;
    std::vector<IRuntimeComponent*> startedComponents_;
    bool running_ = false;
};
