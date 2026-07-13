#include "ReliabilityBudget.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace { void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); } }

int main() {
    try {
        ReliabilityBudgetCompiler compiler;
        auto pass = compiler.Compile({{"sentinel", 0.001, 0.002, true}, {"specialist", 0.002, 0.003, true}, {"critic", 0.001, 0.002, true}});
        Require(pass.accepted, "measured budget should pass");
        auto failed = compiler.Compile({{"sentinel", 0.004, 0.003, true}, {"critic", 0.001, 0.002, false}});
        Require(!failed.accepted, "unmeasured or over-budget components must fail");
        std::cout << "ReliabilityBudgetTests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ReliabilityBudgetTests failed: " << error.what() << '\n';
        return 1;
    }
}
