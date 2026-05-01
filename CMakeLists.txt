cmake_minimum_required(VERSION 3.20)
project(PredictiveAutoHeal)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🔥 FORCE sqlite3.c to compile as C
set_source_files_properties(sqlite3.c PROPERTIES LANGUAGE C)

add_executable(PredictiveAutoHeal
    main.cpp
    AppConfig.cpp
    DecisionEngine.cpp
    MetricsPipeline.cpp
    MetricsStorage.cpp
    SystemMetrics.cpp
    sqlite3.c
)

target_compile_definitions(PredictiveAutoHeal PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(PredictiveAutoHeal PRIVATE Advapi32 User32 Gdi32 Iphlpapi Psapi)

add_custom_command(TARGET PredictiveAutoHeal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.txt"
        "${CMAKE_SOURCE_DIR}/predict_model.py"
        "${CMAKE_SOURCE_DIR}/model_features.py"
        "${CMAKE_SOURCE_DIR}/labeling.py"
        "${CMAKE_SOURCE_DIR}/requirements.txt"
        "$<TARGET_FILE_DIR:PredictiveAutoHeal>"
)

add_custom_command(TARGET PredictiveAutoHeal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/ai_model.joblib"
        "${CMAKE_BINARY_DIR}/ai_model_meta.json"
        "$<TARGET_FILE_DIR:PredictiveAutoHeal>"
)
