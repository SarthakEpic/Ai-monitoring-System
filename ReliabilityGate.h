#pragma once

#include <string>
#include <vector>

#include "RuntimeFoundation.h"

enum class DriftLevel {
    Normal = 0,
    CollectMoreEvidence = 1,
    RecommendationOnly = 2,
    DeterministicMonitoringOnly = 3,
    CertificateInvalid = 4,
};

struct DataQualityGateInput {
    bool telemetryFresh = false;
    bool collectorsHealthy = false;
    bool missingnessAcceptable = false;
};

struct SupportedEnvelope {
    std::vector<std::string> windowsBuildFamilies;
    std::vector<std::string> cpuCoreTiers;
    std::vector<std::string> ramTiers;
    std::vector<std::string> storageTiers;
    std::vector<std::string> workloadClasses;
};

struct ReliabilityCertificate {
    std::string certificateId;
    std::string modelManifestHash;
    std::string featureSchemaHash;
    SupportedEnvelope envelope;
    long long issuedAtMs = 0;
    long long expiresAtMs = 0;
    double maximumAcceptedErrorUpperBound = 0.0;
    double minimumCoverageLowerBound = 0.0;
    bool issuedForResearch = true;
};

struct ReliabilityGateInput {
    long long nowMs = 0;
    std::string modelManifestHash;
    std::string featureSchemaHash;
    std::string windowsBuildFamily;
    std::string cpuCoreTier;
    std::string ramTier;
    std::string storageTier;
    std::string workloadClass;
    DataQualityGateInput dataQuality;
    double oodScore = 1.0;
    double estimatedErrorUpperBound = 1.0;
    double coverageLowerBound = 0.0;
    DriftLevel drift = DriftLevel::CertificateInvalid;
};

struct ReliabilityGateDecision {
    bool accepted = false;
    RuntimeMode requiredMode = RuntimeMode::MonitorOnly;
    std::string reason = "certificate_missing";
};

// Phase 2's fail-closed reliability boundary. A valid research certificate can
// permit a recommendation result only; automatic actions remain a Phase 4 gate.
class ReliabilityGate {
public:
    ReliabilityGateDecision Evaluate(
        const ReliabilityCertificate* certificate,
        const ReliabilityGateInput& input
    ) const;

    static const char* ToString(DriftLevel level);

private:
    static bool Contains(const std::vector<std::string>& values, const std::string& value);
};