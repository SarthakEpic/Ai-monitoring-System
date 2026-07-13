#include "DashboardCommandController.h"

DashboardCommandDecision DashboardCommandController::PrepareManualCanary(const ManualCanaryCommand& command) const {
    if (command.runtimeMode != "MANUAL_CANARY") return {false, false, "manual_canary_requires_manual_canary_runtime_mode"};
    if (command.globalDisable || !command.actionExecutionEnabled) return {false, false, "action_execution_is_globally_disabled"};
    if (command.certificateId.empty() || command.certificateId == "NOT_CERTIFIED") return {false, false, "valid_certificate_required"};
    if (command.target.empty() || command.action.empty() || command.evidenceSummary.empty()) return {false, false, "exact_target_action_and_evidence_required"};
    if (command.durationMs < 100 || command.durationMs > 30000) return {false, false, "bounded_canary_duration_required"};
    if (!command.proofReferenceAvailable) return {false, false, "trusted_proof_reference_not_available"};
    return {true, true, "confirm_exact_target_action_duration_and_evidence"};
}
