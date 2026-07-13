#include "RuntimeFoundation.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace {

std::string Normalize(std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return normalized;
}

}  // namespace

RuntimeMode ParseRuntimeMode(std::string_view value, RuntimeMode fallback) {
    const std::string normalized = Normalize(value);
    if (normalized == "MONITOR_ONLY") return RuntimeMode::MonitorOnly;
    if (normalized == "RECOMMENDATION_ONLY") return RuntimeMode::RecommendationOnly;
    if (normalized == "MANUAL_CANARY") return RuntimeMode::ManualCanary;
    if (normalized == "RESTRICTED_AUTOMATIC") return RuntimeMode::RestrictedAutomatic;
    if (normalized == "CERTIFIED_AUTOMATIC") return RuntimeMode::CertifiedAutomatic;
    return fallback;
}

std::string_view ToString(RuntimeMode mode) {
    switch (mode) {
    case RuntimeMode::MonitorOnly: return "MONITOR_ONLY";
    case RuntimeMode::RecommendationOnly: return "RECOMMENDATION_ONLY";
    case RuntimeMode::ManualCanary: return "MANUAL_CANARY";
    case RuntimeMode::RestrictedAutomatic: return "RESTRICTED_AUTOMATIC";
    case RuntimeMode::CertifiedAutomatic: return "CERTIFIED_AUTOMATIC";
    }
    return "MONITOR_ONLY";
}

std::string_view ToString(RuntimeStatusCode code) {
    switch (code) {
    case RuntimeStatusCode::Ok: return "OK";
    case RuntimeStatusCode::AlreadyRunning: return "ALREADY_RUNNING";
    case RuntimeStatusCode::InvalidConfiguration: return "INVALID_CONFIGURATION";
    case RuntimeStatusCode::StartupFailed: return "STARTUP_FAILED";
    case RuntimeStatusCode::CollectorUnavailable: return "COLLECTOR_UNAVAILABLE";
    case RuntimeStatusCode::InferenceUnavailable: return "INFERENCE_UNAVAILABLE";
    case RuntimeStatusCode::StorageUnavailable: return "STORAGE_UNAVAILABLE";
    case RuntimeStatusCode::PolicyUnavailable: return "POLICY_UNAVAILABLE";
    case RuntimeStatusCode::UiUnavailable: return "UI_UNAVAILABLE";
    case RuntimeStatusCode::ShutdownFailed: return "SHUTDOWN_FAILED";
    }
    return "STARTUP_FAILED";
}

std::string_view ToString(RuntimeComponent component) {
    switch (component) {
    case RuntimeComponent::Collectors: return "COLLECTORS";
    case RuntimeComponent::Inference: return "INFERENCE";
    case RuntimeComponent::Storage: return "STORAGE";
    case RuntimeComponent::Policy: return "POLICY";
    case RuntimeComponent::Service: return "SERVICE";
    case RuntimeComponent::Certificate: return "CERTIFICATE";
    case RuntimeComponent::Ui: return "UI";
    }
    return "COLLECTORS";
}

std::string_view ToString(ComponentHealthState state) {
    switch (state) {
    case ComponentHealthState::Unknown: return "UNKNOWN";
    case ComponentHealthState::Starting: return "STARTING";
    case ComponentHealthState::Healthy: return "HEALTHY";
    case ComponentHealthState::Degraded: return "DEGRADED";
    case ComponentHealthState::Unavailable: return "UNAVAILABLE";
    case ComponentHealthState::Stopped: return "STOPPED";
    }
    return "UNKNOWN";
}

ModelStateLabel ParseModelStateLabel(std::string_view value) {
    const std::string normalized = Normalize(value);
    if (normalized == "NORMAL") return ModelStateLabel::Normal;
    if (normalized == "WARNING") return ModelStateLabel::Warning;
    if (normalized == "CRITICAL") return ModelStateLabel::Critical;
    if (normalized == "RECOVERY") return ModelStateLabel::Recovery;
    return ModelStateLabel::Unknown;
}

SeverityRank SeverityForLabel(ModelStateLabel label) {
    switch (label) {
    case ModelStateLabel::Critical: return SeverityRank::Critical;
    case ModelStateLabel::Warning: return SeverityRank::Warning;
    case ModelStateLabel::Normal:
    case ModelStateLabel::Recovery:
    case ModelStateLabel::Unknown:
        return SeverityRank::Normal;
    }
    return SeverityRank::Normal;
}

std::string_view ToString(SeverityRank severity) {
    switch (severity) {
    case SeverityRank::Normal: return "NORMAL";
    case SeverityRank::Warning: return "WARNING";
    case SeverityRank::Critical: return "CRITICAL";
    }
    return "NORMAL";
}

bool RuntimeModePermitsAutomaticActions(RuntimeMode mode) {
    return mode == RuntimeMode::CertifiedAutomatic;
}
