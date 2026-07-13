#include "ReliabilityBudget.h"

#include <cmath>

ReliabilityBudgetDecision ReliabilityBudgetCompiler::Compile(
    const std::vector<ComponentRisk>& components,
    double globalBudget
) const {
    ReliabilityBudgetDecision decision;
    if (!std::isfinite(globalBudget) || globalBudget <= 0.0 || components.empty()) {
        decision.reason = "invalid_budget_or_empty_components";
        return decision;
    }
    for (const ComponentRisk& component : components) {
        if (component.component.empty() || !component.measured || !std::isfinite(component.measuredErrorUpperBound) ||
            !std::isfinite(component.requestedBudget) || component.measuredErrorUpperBound < 0.0 || component.requestedBudget <= 0.0) {
            decision.violations.push_back(component.component.empty() ? "unnamed_component" : component.component + ":unmeasured_or_invalid");
            continue;
        }
        decision.totalAllocatedBudget += component.requestedBudget;
        if (component.measuredErrorUpperBound > component.requestedBudget) {
            decision.violations.push_back(component.component + ":upper_bound_exceeds_budget");
        }
    }
    if (decision.totalAllocatedBudget > globalBudget + 1e-12) {
        decision.violations.push_back("global_budget_exceeded");
    }
    decision.accepted = decision.violations.empty();
    decision.reason = decision.accepted ? "measured_component_bounds_fit_global_budget" : "reliability_budget_rejected";
    return decision;
}
