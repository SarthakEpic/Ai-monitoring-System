#include "ThreadedRuntimeComponent.h"

#include <utility>

ThreadedRuntimeComponent::ThreadedRuntimeComponent(RuntimeComponent id, StartFunction start, StopFunction stop)
    : id_(id), start_(std::move(start)), stop_(std::move(stop)) {}

RuntimeComponent ThreadedRuntimeComponent::Id() const { return id_; }

RuntimeStatus ThreadedRuntimeComponent::Start() {
    if (started_) return {RuntimeStatusCode::AlreadyRunning, "component already started"};
    if (!start_) return {RuntimeStatusCode::StartupFailed, "missing start callback"};
    RuntimeStatus status = start_();
    started_ = status.Succeeded();
    return status;
}

void ThreadedRuntimeComponent::Stop() noexcept {
    if (!started_) return;
    try { if (stop_) stop_(); } catch (...) {}
    started_ = false;
}