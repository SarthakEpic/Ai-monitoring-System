#include "DashboardCommandController.h"
#include <iostream>
#include <stdexcept>

namespace { void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); } }

int main() {
    try {
        DashboardCommandController controller;
        ManualCanaryCommand command{"MANUAL_CANARY", "cert-1", "pid:42", "lower_priority", "causal evidence", 1000, true, false, true};
        const auto ready = controller.PrepareManualCanary(command);
        Require(ready.needsConfirmation && ready.maySubmitToActuator, "valid exact manual canary rejected");
        command.target.clear();
        Require(!controller.PrepareManualCanary(command).maySubmitToActuator, "targetless approval accepted");
        command.target = "pid:42"; command.proofReferenceAvailable = false;
        Require(!controller.PrepareManualCanary(command).maySubmitToActuator, "untrusted proof accepted");
        std::cout << "DashboardCommandControllerTests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
