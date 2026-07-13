#include "RuntimeOrchestrator.h"

#include <chrono>

namespace {

long long CurrentTimestampMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

RuntimeOrchestrator::RuntimeOrchestrator(
    RuntimeStateStore& stateStore,
    std::vector<std::unique_ptr<IRuntimeComponent>> components)
    : stateStore_(stateStore), components_(std::move(components)) {
}

RuntimeOrchestrator::~RuntimeOrchestrator() {
    Stop();
}

RuntimeStatus RuntimeOrchestrator::Start() {
    if (running_) {
        return {RuntimeStatusCode::AlreadyRunning, "runtime is already running"};
    }

    for (const auto& component : components_) {
        RecordHealth(component->Id(), ComponentHealthState::Starting, {RuntimeStatusCode::Ok, "starting"});
        RuntimeStatus status = component->Start();
        if (!status.Succeeded()) {
            RecordHealth(component->Id(), ComponentHealthState::Unavailable, status);
            Stop();
            return status;
        }
        startedComponents_.push_back(component.get());
        RecordHealth(component->Id(), ComponentHealthState::Healthy, {RuntimeStatusCode::Ok, "healthy"});
    }

    running_ = true;
    stateStore_.SetRuntimeStarted(true);
    return {RuntimeStatusCode::Ok, "runtime started"};
}

void RuntimeOrchestrator::Stop() noexcept {
    for (auto it = startedComponents_.rbegin(); it != startedComponents_.rend(); ++it) {
        try {
            (*it)->Stop();
            RecordHealth((*it)->Id(), ComponentHealthState::Stopped, {RuntimeStatusCode::Ok, "stopped"});
        } catch (...) {
            RecordHealth((*it)->Id(), ComponentHealthState::Degraded, {RuntimeStatusCode::ShutdownFailed, "stop threw"});
        }
    }
    startedComponents_.clear();
    running_ = false;
    stateStore_.SetRuntimeStarted(false);
}

void RuntimeOrchestrator::RecordHealth(
    RuntimeComponent component,
    ComponentHealthState state,
    RuntimeStatus status) const {
    stateStore_.SetComponentHealth({component, state, status.code, std::move(status.detail), CurrentTimestampMs()});
}
