#include "MonitoringScheduler.h"

#include <algorithm>
#include <chrono>
#include <thread>

std::int64_t SystemClock::NowMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool SystemClock::WaitFor(std::atomic_bool& running, std::int64_t durationMs) {
    const std::int64_t deadline = NowMs() + std::max<std::int64_t>(0, durationMs);
    while (running.load() && NowMs() < deadline) {
        const std::int64_t remaining = deadline - NowMs();
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<std::int64_t>(remaining, 50)));
    }
    return running.load();
}

MonitoringScheduler::MonitoringScheduler(std::unique_ptr<IClock> clock)
    : clock_(std::move(clock)) {
    if (!clock_) {
        clock_ = std::make_unique<SystemClock>();
    }
}

bool MonitoringScheduler::WaitForNextTick(std::atomic_bool& running, std::int64_t intervalMs) {
    const bool active = clock_->WaitFor(running, std::max<std::int64_t>(1, intervalMs));
    if (active) {
        lastWakeMs_ = clock_->NowMs();
    }
    return active;
}