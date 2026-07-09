cmake_minimum_required(VERSION 3.20)
project(PredictiveAutoHeal)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🔥 FORCE sqlite3.c to compile as C
set_source_files_properties(sqlite3.c PROPERTIES LANGUAGE C)

add_executable(PredictiveAutoHeal
    main.cpp
    AppConfig.cpp
    AutoHealPlanner.cpp
    DecisionEngine.cpp
    MetricsPipeline.cpp
    MetricsStorage.cpp
    ProcessClassifier.cpp
    ProcessGenome.cpp
    ProcessTelemetry.cpp
    SystemMetrics.cpp
    UserIntent.cpp
    sqlite3.c
)

target_compile_definitions(PredictiveAutoHeal PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(PredictiveAutoHeal PRIVATE Advapi32 User32 Gdi32 Iphlpapi Psapi Wintrust Crypt32)

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
        "${CMAKE_SOURCE_DIR}/training_label.txt"
        "${CMAKE_SOURCE_DIR}/PROCESS_GENOME.md"
        "${CMAKE_SOURCE_DIR}/USER_INTENT.md"
        "${CMAKE_SOURCE_DIR}/DECISION_ENGINE.md"
        "${CMAKE_SOURCE_DIR}/AUTO_HEAL_PLANNER.md"
        "${CMAKE_SOURCE_DIR}/DATA_COLLECTION.md"
        "${CMAKE_SOURCE_DIR}/requirements.txt"
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
