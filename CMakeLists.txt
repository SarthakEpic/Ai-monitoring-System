cmake_minimum_required(VERSION 3.20)
project(PredictiveAutoHeal)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🔥 FORCE sqlite3.c to compile as C
set_source_files_properties(sqlite3.c PROPERTIES LANGUAGE C COMPILE_OPTIONS "/W0")

add_executable(PredictiveAutoHeal
    ModernDashboardUI.cpp
    RuntimeFoundation.cpp
    RuntimeOrchestrator.cpp
    RuntimeStateStore.cpp
    main.cpp
    ActionVerification.cpp
    AdaptiveBaseline.cpp
    AppConfig.cpp
    AutoHealPlanner.cpp
    BackgroundAgent.cpp
    BenchmarkProof.cpp
    BrowserIntegrationBridge.cpp
    CooperativeIntegrations.cpp
    DecisionEngine.cpp
    HealingVerifier.cpp
    ImpactLearning.cpp
    LowEndAutopilot.cpp
    MetricsPipeline.cpp
    MetricsStorage.cpp
    PerformanceIntelligence.cpp
    RuntimeHealth.cpp
    SafeOnlinePolicy.cpp
    SafetyPolicy.cpp
    ProcessClassifier.cpp
    ProcessGenome.cpp
    ProcessTelemetry.cpp
    ResourceOrchestrator.cpp
    SystemMetrics.cpp
    UserIntent.cpp
    sqlite3.c
)

add_executable(PredictiveAutoHealBrowserHost BrowserNativeHost.cpp)
target_compile_definitions(PredictiveAutoHealBrowserHost PRIVATE UNICODE _UNICODE NOMINMAX)
add_dependencies(PredictiveAutoHeal PredictiveAutoHealBrowserHost)

target_compile_definitions(PredictiveAutoHeal PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(PredictiveAutoHeal PRIVATE Advapi32 User32 Gdi32 Iphlpapi Psapi Pdh Dwmapi Bcrypt Bits Ole32 Wintrust Crypt32 Shell32)

add_custom_command(TARGET PredictiveAutoHeal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.txt"
        "${CMAKE_SOURCE_DIR}/predict_model.py"
        "${CMAKE_SOURCE_DIR}/inference_service.py"
        "${CMAKE_SOURCE_DIR}/model_features.py"
        "${CMAKE_SOURCE_DIR}/labeling.py"
        "${CMAKE_SOURCE_DIR}/set_training_label.py"
        "${CMAKE_SOURCE_DIR}/training_data_summary.py"
        "${CMAKE_SOURCE_DIR}/process_genome_summary.py"
        "${CMAKE_SOURCE_DIR}/user_intent_summary.py"
        "${CMAKE_SOURCE_DIR}/decision_audit_summary.py"
        "${CMAKE_SOURCE_DIR}/heal_plan_summary.py"
        "${CMAKE_SOURCE_DIR}/heal_verification_summary.py"
        "${CMAKE_SOURCE_DIR}/safety_policy_summary.py"
        "${CMAKE_SOURCE_DIR}/runtime_health_summary.py"
        "${CMAKE_SOURCE_DIR}/adaptive_baseline_summary.py"
        "${CMAKE_SOURCE_DIR}/autopilot_summary.py"
        "${CMAKE_SOURCE_DIR}/install_startup.ps1"
        "${CMAKE_SOURCE_DIR}/install_browser_integration.ps1"
        "${CMAKE_SOURCE_DIR}/training_label.txt"
        "${CMAKE_SOURCE_DIR}/PROCESS_GENOME.md"
        "${CMAKE_SOURCE_DIR}/USER_INTENT.md"
        "${CMAKE_SOURCE_DIR}/DECISION_ENGINE.md"
        "${CMAKE_SOURCE_DIR}/AUTO_HEAL_PLANNER.md"
        "${CMAKE_SOURCE_DIR}/HEALING_VERIFIER.md"
        "${CMAKE_SOURCE_DIR}/SAFETY_POLICY.md"
        "${CMAKE_SOURCE_DIR}/RUNTIME_OBSERVABILITY.md"
        "${CMAKE_SOURCE_DIR}/ADAPTIVE_BASELINE.md"
        "${CMAKE_SOURCE_DIR}/LOW_END_AUTOPILOT.md"
        "${CMAKE_SOURCE_DIR}/BACKGROUND_AGENT.md"
        "${CMAKE_SOURCE_DIR}/BENCHMARK_PROOF.md"
        "${CMAKE_SOURCE_DIR}/DATA_COLLECTION.md"
        "${CMAKE_SOURCE_DIR}/requirements.txt"
        "$<TARGET_FILE_DIR:PredictiveAutoHeal>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/integrations"
        "$<TARGET_FILE_DIR:PredictiveAutoHeal>/integrations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:PredictiveAutoHealBrowserHost>"
        "$<TARGET_FILE_DIR:PredictiveAutoHeal>"
)

set(OPTIONAL_MODEL_FILES
    "${CMAKE_BINARY_DIR}/ai_model.joblib"
    "${CMAKE_BINARY_DIR}/ai_model_meta.json"
)

foreach(OPTIONAL_MODEL_FILE IN LISTS OPTIONAL_MODEL_FILES)
    if(EXISTS "${OPTIONAL_MODEL_FILE}")
        add_custom_command(TARGET PredictiveAutoHeal POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPTIONAL_MODEL_FILE}"
                "$<TARGET_FILE_DIR:PredictiveAutoHeal>"
        )
    endif()
endforeach()

include(CTest)

if(BUILD_TESTING)
    add_executable(StageAutopilotTests
        StageAutopilotTests.cpp
        BackgroundAgent.cpp
        BenchmarkProof.cpp
        LowEndAutopilot.cpp
    )
    target_compile_definitions(StageAutopilotTests PRIVATE UNICODE _UNICODE NOMINMAX)
    add_test(NAME StageAutopilotTests COMMAND StageAutopilotTests)

    add_executable(RuntimeFoundationTests
        RuntimeFoundationTests.cpp
        RuntimeFoundation.cpp
        RuntimeOrchestrator.cpp
        RuntimeStateStore.cpp
    )
    target_compile_definitions(RuntimeFoundationTests PRIVATE UNICODE _UNICODE NOMINMAX)
    add_test(NAME RuntimeFoundationTests COMMAND RuntimeFoundationTests)

    add_executable(ResourceOrchestratorTests
        ResourceOrchestratorTests.cpp
        ActionVerification.cpp
        ResourceOrchestrator.cpp
        sqlite3.c
    )
    target_compile_definitions(ResourceOrchestratorTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(ResourceOrchestratorTests PRIVATE Psapi User32)
    add_test(NAME ResourceOrchestratorTests COMMAND ResourceOrchestratorTests)

    add_executable(ActionVerificationTests
        ActionVerificationTests.cpp
        ActionVerification.cpp
        ResourceOrchestrator.cpp
        sqlite3.c
    )
    target_compile_definitions(ActionVerificationTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(ActionVerificationTests PRIVATE Psapi User32)
    add_test(NAME ActionVerificationTests COMMAND ActionVerificationTests)
    add_executable(PerformanceIntelligenceTests
        PerformanceIntelligenceTests.cpp
        PerformanceIntelligence.cpp
        sqlite3.c
    )
    target_compile_definitions(PerformanceIntelligenceTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(PerformanceIntelligenceTests PRIVATE Psapi Pdh Dwmapi User32)
    add_test(NAME PerformanceIntelligenceTests COMMAND PerformanceIntelligenceTests)
    add_executable(ImpactLearningTests
        ImpactLearningTests.cpp
        ImpactLearning.cpp
        ActionVerification.cpp
        ResourceOrchestrator.cpp
        PerformanceIntelligence.cpp
        sqlite3.c
    )
    target_compile_definitions(ImpactLearningTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(ImpactLearningTests PRIVATE Bcrypt Psapi Pdh Dwmapi User32)
    add_test(NAME ImpactLearningTests COMMAND ImpactLearningTests)
    add_executable(SafeOnlinePolicyTests
        SafeOnlinePolicyTests.cpp
        SafeOnlinePolicy.cpp
        ImpactLearning.cpp
        ActionVerification.cpp
        ResourceOrchestrator.cpp
        PerformanceIntelligence.cpp
        SafetyPolicy.cpp
        AutoHealPlanner.cpp
        HealingVerifier.cpp
        sqlite3.c
    )
    target_compile_definitions(SafeOnlinePolicyTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(SafeOnlinePolicyTests PRIVATE Bcrypt Psapi Pdh Dwmapi User32)
    add_test(NAME SafeOnlinePolicyTests COMMAND SafeOnlinePolicyTests)
    add_executable(CooperativeIntegrationsTests
        CooperativeIntegrationsTests.cpp
        CooperativeIntegrations.cpp
        PerformanceIntelligence.cpp
        sqlite3.c
    )
    target_compile_definitions(CooperativeIntegrationsTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(CooperativeIntegrationsTests PRIVATE Bits Ole32 Psapi Pdh Dwmapi User32)
    add_test(NAME CooperativeIntegrationsTests COMMAND CooperativeIntegrationsTests)
    add_executable(BrowserIntegrationBridgeTests
        BrowserIntegrationBridgeTests.cpp
        BrowserIntegrationBridge.cpp
        CooperativeIntegrations.cpp
        PerformanceIntelligence.cpp
        sqlite3.c
    )
    target_compile_definitions(BrowserIntegrationBridgeTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(BrowserIntegrationBridgeTests PRIVATE Bits Ole32 Psapi Pdh Dwmapi User32)
    add_test(NAME BrowserIntegrationBridgeTests COMMAND BrowserIntegrationBridgeTests)
endif()

if(MSVC)
    set(PROJECT_WARNING_TARGETS
        PredictiveAutoHeal
        PredictiveAutoHealBrowserHost
        StageAutopilotTests
        RuntimeFoundationTests
        ResourceOrchestratorTests
        ActionVerificationTests
        PerformanceIntelligenceTests
        ImpactLearningTests
        SafeOnlinePolicyTests
        CooperativeIntegrationsTests
        BrowserIntegrationBridgeTests
    )
    foreach(WARNING_TARGET IN LISTS PROJECT_WARNING_TARGETS)
        if(TARGET ${WARNING_TARGET})
            target_compile_options(${WARNING_TARGET} PRIVATE /W4 /permissive- /Zc:__cplusplus)
        endif()
    endforeach()
endif()