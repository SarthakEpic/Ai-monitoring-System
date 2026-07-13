#pragma once

#include <string>

struct ManualCanaryCommand {
    std::string runtimeMode;
    std::string certificateId;
    std::string target;
    std::string action;
    std::string evidenceSummary;
    int durationMs = 0;
    bool actionExecutionEnabled = false;
    bool globalDisable = true;
    bool proofReferenceAvailable = false;
};

struct DashboardCommandDecision {
    bool needsConfirmation = false;
    bool maySubmitToActuator = false;
    std::string reason;
};

class DashboardCommandController {
public:
    DashboardCommandDecision PrepareManualCanary(const ManualCanaryCommand& command) const;
};
