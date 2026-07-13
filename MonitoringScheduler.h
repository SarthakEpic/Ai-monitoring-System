#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::int64_t NowMs() const = 0;
    virtual bool WaitFor(std::atomic_bool& running, std::int64_t durationMs) = 0;
};

class SystemClock final : public IClock {
public:
    std::int64_t NowMs() const override;
    bool WaitFor(std::atomic_bool& running, std::int64_t durationMs) override;
};

// Owns periodic loop timing independently from the monitor body. Tests can
// inject a fake clock to verify cancellation and schedule behavior deterministically.
class MonitoringScheduler {
public:
    explicit MonitoringScheduler(std::unique_ptr<IClock> clock = std::make_unique<SystemClock>());

    bool WaitForNextTick(std::atomic_bool& running, std::int64_t intervalMs);
    [[nodiscard]] std::int64_t LastWakeMs() const { return lastWakeMs_; }

private:
    std::unique_ptr<IClock> clock_;
    std::int64_t lastWakeMs_ = 0;
};