#include <cassert>

#include "ReliabilityGate.h"

namespace {
ReliabilityCertificate ValidCertificate() {
    ReliabilityCertificate certificate;
    certificate.certificateId = "research-cert-1";
    certificate.modelManifestHash = "model-hash";
    certificate.featureSchemaHash = "feature-hash";
    certificate.envelope.windowsBuildFamilies = {"WINDOWS_11_26200"};
    certificate.envelope.cpuCoreTiers = {"4_LOGICAL"};
    certificate.envelope.ramTiers = {"4GB"};
    certificate.envelope.storageTiers = {"SSD"};
    certificate.envelope.workloadClasses = {"COMPILATION"};
    certificate.issuedAtMs = 100;
    certificate.expiresAtMs = 10000;
    certificate.maximumAcceptedErrorUpperBound = 0.01;
    certificate.minimumCoverageLowerBound = 0.70;
    return certificate;
}

ReliabilityGateInput ValidInput() {
    ReliabilityGateInput input;
    input.nowMs = 200;
    input.modelManifestHash = "model-hash";
    input.featureSchemaHash = "feature-hash";
    input.windowsBuildFamily = "WINDOWS_11_26200";
    input.cpuCoreTier = "4_LOGICAL";
    input.ramTier = "4GB";
    input.storageTier = "SSD";
    input.workloadClass = "COMPILATION";
    input.dataQuality = {true, true, true};
    input.oodScore = 0.05;
    input.estimatedErrorUpperBound = 0.005;
    input.coverageLowerBound = 0.75;
    input.drift = DriftLevel::Normal;
    return input;
}

void TestValidResearchCertificateIsRecommendationOnly() {
    ReliabilityGate gate;
    ReliabilityCertificate certificate = ValidCertificate();
    ReliabilityGateInput input = ValidInput();
    const ReliabilityGateDecision decision = gate.Evaluate(&certificate, input);
    assert(decision.accepted);
    assert(decision.requiredMode == RuntimeMode::RecommendationOnly);
}

void TestInvalidInputsFailClosed() {
    ReliabilityGate gate;
    ReliabilityCertificate certificate = ValidCertificate();
    ReliabilityGateInput input = ValidInput();
    assert(!gate.Evaluate(nullptr, input).accepted);

    certificate.expiresAtMs = 200;
    assert(gate.Evaluate(&certificate, input).reason == "certificate_expired");
    certificate = ValidCertificate();

    input.oodScore = 0.5;
    assert(gate.Evaluate(&certificate, input).reason == "ood_rejected");
    input = ValidInput();
    input.drift = DriftLevel::CertificateInvalid;
    assert(gate.Evaluate(&certificate, input).reason == "certificate_invalidated_by_drift");
    input = ValidInput();
    input.dataQuality.collectorsHealthy = false;
    assert(gate.Evaluate(&certificate, input).reason == "data_quality_rejected");
}
}  // namespace

int main() {
    TestValidResearchCertificateIsRecommendationOnly();
    TestInvalidInputsFailClosed();
    return 0;
}