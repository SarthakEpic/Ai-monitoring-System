#include "SecureActuation.h"
#include "ProofLedger.h"
#include <filesystem>
#include <iostream>
#include <climits>
#include <stdexcept>
namespace { void Require(bool v, const char* m) { if (!v) throw std::runtime_error(m); } long long Now() { return 200000; } }
int main() {
    try {
        SecureProtocolMessage message; std::string reason;
        Require(SecureMessageValidation::Parse("v=1|id=req1|session=user1|seq=1|issued=199000|expires=201000|cmd=BEGIN_CANARY|proof=p1", message, reason), "valid message rejected");
        ReplayProtection replay; Require(replay.Accept(message, Now(), reason), "fresh request rejected"); Require(!replay.Accept(message, Now(), reason), "replay accepted");
        Require(!SecureMessageValidation::Parse(std::string(3000, 'x'), message, reason), "oversized request accepted");
        ProofCarryingAction proof; proof.requestId="req1"; proof.modelId="native-v1"; proof.certificateId="research"; proof.need.accepted=true; proof.evidence.action=ResourceActionType::LowerPriority; proof.evidence.benefitLowerBound=.2; proof.evidence.harmUpperBound=.01; proof.evidence.mechanismConsistent=true; proof.evidence.guardianPassed=true; proof.context.targetPid=55; proof.context.foregroundPid=99; proof.context.userApproved=true; proof.target.pid=55; proof.deterministicSafetyPassed=true; proof.rollbackSnapshotComplete=true; proof.canaryDurationMs=1000; proof.maximumLeaseMs=2000;
        const auto ledgerPath = std::filesystem::temp_directory_path() / "aegis99-proof-ledger-test.db";
        std::filesystem::remove(ledgerPath);
        ProofLedger ledger; Require(ledger.Open(ledgerPath.string(), reason), "proof ledger open failed");
        Require(ledger.Save(proof, reason), "proof ledger save failed");
        Require(!ledger.Save(proof, reason), "duplicate proof ID accepted");
        ProofCarryingAction loadedProof; Require(ledger.Load(proof.requestId, loadedProof, reason), "proof ledger load failed");
        Require(loadedProof.requestId == proof.requestId && loadedProof.context.targetPid == proof.context.targetPid, "proof ledger changed target identity");
        Require(TrustedSafetyController().ValidateProof(loadedProof, reason), "loaded proof failed trusted revalidation");
        Require(ledger.ClaimForCanary(proof.requestId, reason), "pending proof claim failed");
        Require(!ledger.ClaimForCanary(proof.requestId, reason), "proof was claimed twice");
        Require(ledger.MarkLeaseState(proof.requestId, "ROLLED_BACK", reason), "proof ledger state update failed");
        ledger.Close(); std::filesystem::remove(ledgerPath);
        Require(TrustedSafetyController().ValidateProof(proof, reason), "valid proof rejected"); proof.context.foregroundPid=55; Require(!TrustedSafetyController().ValidateProof(proof, reason), "foreground proof accepted"); proof.context.foregroundPid=99;
        const auto neutral=CanaryController().Evaluate({true,true,false,true,.01,0.0}, .05, .02); Require(neutral.rollback && !neutral.commit, "neutral canary committed");
        const auto helpful=CanaryController().Evaluate({true,true,false,true,.2,0.0}, .05, .02); Require(helpful.commit, "helpful canary did not commit");
        bool restored=false; ActionLeaseManager leases; Require(leases.Grant("tx", LLONG_MAX - 1, [&]{restored=true;}, reason), "lease rejected"); Require(leases.RestoreExpired(LLONG_MAX)==1 && restored, "expired lease did not restore");
        std::cout << "SecureActuationTests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
