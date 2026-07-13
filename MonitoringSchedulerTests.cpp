#include <cassert>
#include <atomic>
#include <cstdint>
#include <memory>

#include "MonitoringScheduler.h"

namespace {
class FakeClock final : public IClock {
public:
    std::int64_t NowMs() const override { return nowMs; }

    bool WaitFor(std::atomic_bool& running, std::int64_t durationMs) override {
        ++waitCount;
        lastDurationMs = durationMs;
        nowMs += durationMs;
        return running.load() && allowWait;
    }

    mutable std::int64_t nowMs = 0;
    int waitCount = 0;
    std::int64_t lastDurationMs = 0;
    bool allowWait = true;
};

void TestSchedulerWaitsAtRequestedInterval() {
    auto clock = std::make_unique<FakeClock>();
    FakeClock* rawClock = clock.get();
    MonitoringScheduler scheduler(std::move(clock));
    std::atomic_bool running{true};

    assert(scheduler.WaitForNextTick(running, 1000));
    assert(rawClock->waitCount == 1);
    assert(rawClock->lastDurationMs == 1000);
    assert(scheduler.LastWakeMs() == 1000);
}

void TestSchedulerHonorsCancellation() {
    auto clock = std::make_unique<FakeClock>();
    FakeClock* rawClock = clock.get();
    rawClock->allowWait = false;
    MonitoringScheduler scheduler(std::move(clock));
    std::atomic_bool running{true};

    assert(!scheduler.WaitForNextTick(running, 1000));
    assert(scheduler.LastWakeMs() == 0);
}

void TestSchedulerClampsInvalidInterval() {
    auto clock = std::make_unique<FakeClock>();
    FakeClock* rawClock = clock.get();
    MonitoringScheduler scheduler(std::move(clock));
    std::atomic_bool running{true};

    assert(scheduler.WaitForNextTick(running, 0));
    assert(rawClock->lastDurationMs == 1);
}
}  // namespace

int main() {
    TestSchedulerWaitsAtRequestedInterval();
    TestSchedulerHonorsCancellation();
    TestSchedulerClampsInvalidInterval();
    return 0;
}