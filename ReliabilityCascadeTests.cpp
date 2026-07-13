#include "ReliabilityCascade.h"
#include <iostream>
#include <stdexcept>
namespace { void Require(bool v, const char* m) { if (!v) throw std::runtime_error(m); } }
int main() {
    try {
        ReliabilityCascade cascade;
        CascadeTelemetry stable{20.0, 40.0, 2.0, 0.05, true};
        CausalEvidenceInput causal{};
        FailureCriticInput critic{};
        auto result = cascade.Evaluate(stable, causal, WorkloadClass::Browser, critic);
        Require(result.route == SentinelRoute::Stable && !result.specialistRequired, "stable sentinel routed expensively");
        CascadeTelemetry event{90.0, 91.0, 40.0, 0.8, true};
        causal = {true, true, true, 90.0, 0.9, 91.0, 30.0, 40.0, 2.0, 0.0, 0.0, 0.0};
        critic = {0.9, 0.05, 0.05, 0.02, true, true, true};
        result = cascade.Evaluate(event, causal, WorkloadClass::Compile, critic);
        Require(result.acceptedForRecommendation && result.temporalModelRequired, "supported event was not escalated");
        critic.modelDisagreement = 0.5;
        Require(!cascade.Evaluate(event, causal, WorkloadClass::Compile, critic).acceptedForRecommendation, "critic disagreement was ignored");
        std::cout << "ReliabilityCascadeTests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
