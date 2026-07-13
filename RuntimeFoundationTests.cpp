#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "RuntimeFoundation.h"
#include "RuntimeOrchestrator.h"
#include "RuntimeStateStore.h"

namespace {

class FakeComponent final : public IRuntimeComponent {
public:
    FakeComponent(RuntimeComponent id, bool shouldStart, std::vector<std::string>& events)
        : id_(id), shouldStart_(shouldStart), events_(events) {
    }

    RuntimeComponent Id() const override { return id_; }

    RuntimeStatus Start() override {
        events_.push_back(std::string("start:") + std::string(ToString(id_)));
        return shouldStart_
            ? RuntimeStatus{RuntimeStatusCode::Ok, "started"}
            : RuntimeStatus{RuntimeStatusCode::StartupFailed, "injected start failure"};
    }

    void Stop() noexcept override {
        events_.push_back(std::string("stop:") + std::string(ToString(id_)));
    }

private:
    RuntimeComponent id_;
    bool shouldStart_;
    std::vector<std::string>& events_;
};

void TestModesAndSeverity() {
    assert(ParseRuntimeMode("monitor_only") == RuntimeMode::MonitorOnly);
    assert(ParseRuntimeMode("MANUAL_CANARY") == RuntimeMode::ManualCanary);
    assert(ParseRuntimeMode("invalid", RuntimeMode::RecommendationOnly) == RuntimeMode::RecommendationOnly);
    assert(!RuntimeModePermitsAutomaticActions(RuntimeMode::RestrictedAutomatic, true));
    assert(!RuntimeModePermitsAutomaticActions(RuntimeMode::CertifiedAutomatic, false));
    assert(RuntimeModePermitsAutomaticActions(RuntimeMode::CertifiedAutomatic, true));
    assert(SeverityForLabel(ParseModelStateLabel("RECOVERY")) == SeverityRank::Normal);
    assert(SeverityForLabel(ParseModelStateLabel("CRITICAL")) == SeverityRank::Critical);
}

void TestStateStore() {
    RuntimeStateStore store;
    const RuntimeSnapshot initial = store.Snapshot();
    assert(initial.mode == RuntimeMode::MonitorOnly);
    assert(!initial.runtimeStarted);

    store.SetMode(RuntimeMode::RecommendationOnly);
    store.SetComponentHealth({RuntimeComponent::Storage, ComponentHealthState::Healthy, RuntimeStatusCode::Ok, "ready", 1});
    const RuntimeSnapshot updated = store.Snapshot();
    assert(updated.revision == 2);
    assert(updated.mode == RuntimeMode::RecommendationOnly);
    assert(updated.componentHealth.size() == 1);
}

void TestOrchestratorFailureRollsBackStartedComponents() {
    RuntimeStateStore store;
    std::vector<std::string> events;
    std::vector<std::unique_ptr<IRuntimeComponent>> components;
    components.push_back(std::make_unique<FakeComponent>(RuntimeComponent::Collectors, true, events));
    components.push_back(std::make_unique<FakeComponent>(RuntimeComponent::Inference, false, events));

    RuntimeOrchestrator orchestrator(store, std::move(components));
    const RuntimeStatus status = orchestrator.Start();
    assert(status.code == RuntimeStatusCode::StartupFailed);
    assert(!orchestrator.IsRunning());
    assert((events == std::vector<std::string>{"start:COLLECTORS", "start:INFERENCE", "stop:COLLECTORS"}));
    assert(!store.Snapshot().runtimeStarted);
}

void TestEachFailureReportsComponentHealthAndRollsBack() {
    const std::vector<std::pair<RuntimeComponent, RuntimeStatusCode>> failures = {
        {RuntimeComponent::Collectors, RuntimeStatusCode::CollectorUnavailable},
        {RuntimeComponent::Inference, RuntimeStatusCode::InferenceUnavailable},
        {RuntimeComponent::Storage, RuntimeStatusCode::StorageUnavailable},
        {RuntimeComponent::Policy, RuntimeStatusCode::PolicyUnavailable},
        {RuntimeComponent::Ui, RuntimeStatusCode::UiUnavailable},
    };

    for (const auto& [failedComponent, code] : failures) {
        RuntimeStateStore store;
        std::vector<std::string> events;
        std::vector<std::unique_ptr<IRuntimeComponent>> components;
        if (failedComponent != RuntimeComponent::Collectors) {
            components.push_back(std::make_unique<FakeComponent>(RuntimeComponent::Collectors, true, events));
        }
        components.push_back(std::make_unique<FakeComponent>(failedComponent, false, events));
        RuntimeOrchestrator orchestrator(store, std::move(components));
        const RuntimeStatus status = orchestrator.Start();
        assert(status.code == RuntimeStatusCode::StartupFailed);
        assert(!store.Snapshot().runtimeStarted);

        bool failureRecorded = false;
        for (const ComponentHealth& health : store.Snapshot().componentHealth) {
            if (health.component == failedComponent) {
                failureRecorded = health.state == ComponentHealthState::Unavailable;
            }
        }
        assert(failureRecorded);
    }
}
void TestOrchestratorStopsInReverseOrder() {
    RuntimeStateStore store;
    std::vector<std::string> events;
    std::vector<std::unique_ptr<IRuntimeComponent>> components;
    components.push_back(std::make_unique<FakeComponent>(RuntimeComponent::Collectors, true, events));
    components.push_back(std::make_unique<FakeComponent>(RuntimeComponent::Storage, true, events));

    RuntimeOrchestrator orchestrator(store, std::move(components));
    assert(orchestrator.Start().Succeeded());
    assert(orchestrator.IsRunning());
    orchestrator.Stop();
    assert((events == std::vector<std::string>{"start:COLLECTORS", "start:STORAGE", "stop:STORAGE", "stop:COLLECTORS"}));
}

}  // namespace

int main() {
    TestModesAndSeverity();
    TestStateStore();
    TestOrchestratorFailureRollsBackStartedComponents();
    TestEachFailureReportsComponentHealthAndRollsBack();
    TestOrchestratorStopsInReverseOrder();
    return 0;
}
