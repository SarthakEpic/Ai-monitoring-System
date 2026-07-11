#include "ActionVerification.h"

#include <iostream>
#include <memory>
#include <stdexcept>

using namespace std;

namespace {

void Require(bool condition, const string& message) {
    if (!condition) throw runtime_error(message);
}

ActionTransaction ExecutedTransaction() {
    ActionTransaction tx;
    tx.transactionId = "ACT-TEST";
    tx.status = ActionTransactionStatus::Executed;
    return tx;
}

ActionMetricSnapshot Snapshot(long long time, double cpu, double available, unsigned long faults) {
    ActionMetricSnapshot value;
    value.timestampMs = time;
    value.systemCpuPercent = cpu;
    value.availableMemoryMB = available;
    value.targetPageFaults = faults;
    value.targetWorkingSetMB = 500.0;
    value.targetCpuTime100ns = static_cast<unsigned long long>(time) * 10000;
    value.foregroundCpuTime100ns = static_cast<unsigned long long>(time) * 5000;
    value.foregroundAlive = true;
    value.targetAlive = true;
    value.measured = true;
    return value;
}

void VerifyHelpful() {
    ActionOutcomeVerifier verifier;
    ActionMetricSnapshot before = Snapshot(1000, 80.0, 300.0, 100);
    ActionMetricSnapshot after = Snapshot(11000, 60.0, 420.0, 120);
    after.targetWorkingSetMB = 400.0;
    const ActionImpactResult result = verifier.Evaluate(ExecutedTransaction(), before, after, OutcomePolicy{});
    Require(result.status == ImpactStatus::VerifiedHelpful, "helpful outcome was not classified helpful");
    Require(result.measured && !result.rollbackRequired, "helpful outcome contract is invalid");
}

void VerifyNeutral() {
    ActionOutcomeVerifier verifier;
    const ActionImpactResult result = verifier.Evaluate(
        ExecutedTransaction(), Snapshot(1000, 40.0, 500.0, 100), Snapshot(11000, 39.0, 505.0, 105), OutcomePolicy{});
    Require(result.status == ImpactStatus::VerifiedNeutral, "small effect was not classified neutral");
}

void VerifyHarmful() {
    ActionOutcomeVerifier verifier;
    ActionMetricSnapshot before = Snapshot(1000, 40.0, 500.0, 100);
    ActionMetricSnapshot after = Snapshot(11000, 70.0, 250.0, 2000);
    after.foregroundHung = true;
    const ActionImpactResult result = verifier.Evaluate(ExecutedTransaction(), before, after, OutcomePolicy{});
    Require(result.status == ImpactStatus::VerifiedHarmful, "regression was not classified harmful");
    Require(result.rollbackRequired, "harmful outcome did not require rollback");
}

void VerifyInvalidEvidence() {
    ActionOutcomeVerifier verifier;
    ActionMetricSnapshot before;
    ActionMetricSnapshot after;
    const ActionImpactResult result = verifier.Evaluate(ExecutedTransaction(), before, after, OutcomePolicy{});
    Require(!result.measured && result.status == ImpactStatus::Planned,
            "invalid evidence must never be presented as measured");
}

}  // namespace

int main() {
    try {
        VerifyHelpful();
        VerifyNeutral();
        VerifyHarmful();
        VerifyInvalidEvidence();
        cout << "ActionVerificationTests passed\n";
        return 0;
    } catch (const exception& ex) {
        cerr << "ActionVerificationTests failed: " << ex.what() << '\n';
        return 1;
    }
}
