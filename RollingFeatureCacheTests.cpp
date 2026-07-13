#include <stdexcept>
#include <string>

#include "RollingFeatureCache.h"

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

RollingFeatureFrame Frame(double value) {
    return {value, value + 1.0, value + 2.0, value + 3.0, value + 4.0, value + 5.0, value + 6.0, value + 7.0};
}

void TestBoundedO1RollingWindow() {
    RollingFeatureCache cache(3);
    cache.Push(Frame(1.0));
    cache.Push(Frame(2.0));
    const RollingFeatureSnapshot third = cache.Push(Frame(3.0));
    Require(third.cpu.size() == 3 && third.cpu.front() == 1.0 && third.cpu.back() == 3.0, "initial rolling window incorrect");
    Require(third.means.cpu == 2.0 && third.means.memory == 3.0, "rolling means incorrect");

    const RollingFeatureSnapshot fourth = cache.Push(Frame(4.0));
    Require(cache.Size() == 3 && fourth.cpu.front() == 2.0 && fourth.cpu.back() == 4.0, "cache did not evict oldest value");
    Require(fourth.means.cpu == 3.0 && fourth.means.responsivenessProxy == 10.0, "means were not updated after eviction");
}

void TestSnapshotIsolatedFromCache() {
    RollingFeatureCache cache(2);
    RollingFeatureSnapshot snapshot = cache.Push(Frame(1.0));
    snapshot.cpu.push_back(99.0);
    Require(cache.Snapshot().cpu.size() == 1, "snapshot mutated cache state");
}
}  // namespace

int main() {
    try {
        TestBoundedO1RollingWindow();
        TestSnapshotIsolatedFromCache();
        return 0;
    } catch (const std::exception&) {
        return 1;
    }
}
