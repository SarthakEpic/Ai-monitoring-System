#pragma once

#include <functional>

#include "RuntimeOrchestrator.h"

class ThreadedRuntimeComponent final : public IRuntimeComponent {
public:
    using StartFunction = std::function<RuntimeStatus()>;
    using StopFunction = std::function<void()>;

    ThreadedRuntimeComponent(RuntimeComponent id, StartFunction start, StopFunction stop);
    RuntimeComponent Id() const override;
    RuntimeStatus Start() override;
    void Stop() noexcept override;

private:
    RuntimeComponent id_;
    StartFunction start_;
    StopFunction stop_;
    bool started_ = false;
};