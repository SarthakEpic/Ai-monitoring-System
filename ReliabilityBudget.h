#pragma once

#include <string>
#include <vector>

struct ComponentRisk {
    std::string component;
    double measuredErrorUpperBound = 1.0;
    double requestedBudget = 0.0;
    bool measured = false;
};

struct ReliabilityBudgetDecision {
    bool accepted = false;
    double totalAllocatedBudget = 0.0;
    std::string reason;
    std::vector<std::string> violations;
};

// A certificate can only consume a pre-declared error budget when every
// component is backed by a measured upper bound. No default/hardcoded pass.
class ReliabilityBudgetCompiler {
public:
    ReliabilityBudgetDecision Compile(
        const std::vector<ComponentRisk>& components,
        double globalBudget = 0.01
    ) const;
};
