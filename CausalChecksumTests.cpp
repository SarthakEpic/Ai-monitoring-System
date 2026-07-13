#include "CausalChecksum.h"

#include <iostream>
#include <stdexcept>

namespace { void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); } }

int main() {
    try {
        CausalChecksum checksum;
        CausalEvidenceInput paging;
        paging.telemetryComplete = true;
        paging.foregroundQoeDegraded = true;
        paging.interventionReversible = true;
        paging.memoryUtilizationPercent = 91.0;
        paging.hardFaultsPerSecond = 40.0;
        const auto supported = checksum.Evaluate(paging);
        Require(supported.rootCause == RootCauseClass::MemoryPaging, "paging root cause not identified");
        Require(supported.actionSupportAllowed, "reversible supported action was blocked");

        paging.hardFaultsPerSecond = 0.0;
        const auto weak = checksum.Evaluate(paging);
        Require(!weak.actionSupportAllowed, "single weak signal supported an action");

        paging.telemetryComplete = false;
        const auto incomplete = checksum.Evaluate(paging);
        Require(!incomplete.sufficientEvidence && !incomplete.actionSupportAllowed, "missing telemetry did not fail closed");
        std::cout << "CausalChecksumTests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CausalChecksumTests failed: " << error.what() << '\n';
        return 1;
    }
}
