#pragma once

#include <string>
#include <string_view>

enum class RuntimeMode {
    MonitorOnly,
    RecommendationOnly,
    ManualCanary,
    RestrictedAutomatic,
    CertifiedAutomatic,
};

enum class RuntimeStatusCode {
    Ok,
    AlreadyRunning,
    InvalidConfiguration,
    StartupFailed,
    CollectorUnavailable,
    InferenceUnavailable,
    StorageUnavailable,
    PolicyUnavailable,
    UiUnavailable,
    ShutdownFailed,
};

enum class RuntimeComponent {
    Collectors,
    Inference,
    Storage,
    Policy,
    Service,
    Certificate,
    Ui,
};

enum class ComponentHealthState {
    Unknown,
    Starting,
    Healthy,
    Degraded,
    Unavailable,
    Stopped,
};

enum class ModelStateLabel {
    Normal,
    Warning,
    Critical,
    Recovery,
    Unknown,
};

enum class SeverityRank {
    Normal,
    Warning,
    Critical,
};

struct RuntimeStatus {
    RuntimeStatusCode code = RuntimeStatusCode::Ok;
    std::string detail = "ok";

    [[nodiscard]] bool Succeeded() const { return code == RuntimeStatusCode::Ok; }
};

struct ComponentHealth {
    RuntimeComponent component = RuntimeComponent::Collectors;
    ComponentHealthState state = ComponentHealthState::Unknown;
    RuntimeStatusCode statusCode = RuntimeStatusCode::Ok;
    std::string detail = "not evaluated";
    long long updatedAtMs = 0;
};

struct AsymmetricErrorCostMatrix {
    double missedUserImpact = 10.0;
    double unsafeAction = 100.0;
    double falseAlert = 1.0;
    double unnecessaryAbstention = 0.5;
};

RuntimeMode ParseRuntimeMode(std::string_view value, RuntimeMode fallback = RuntimeMode::MonitorOnly);
std::string_view ToString(RuntimeMode mode);
std::string_view ToString(RuntimeStatusCode code);
std::string_view ToString(RuntimeComponent component);
std::string_view ToString(ComponentHealthState state);
ModelStateLabel ParseModelStateLabel(std::string_view value);
SeverityRank SeverityForLabel(ModelStateLabel label);
std::string_view ToString(SeverityRank severity);
bool RuntimeModePermitsAutomaticActions(RuntimeMode mode);
