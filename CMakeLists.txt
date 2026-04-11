cmake_minimum_required(VERSION 3.20)
project(PredictiveAutoHeal)

set(CMAKE_CXX_STANDARD 17)

# 🔥 FORCE sqlite3.c to compile as C
set_source_files_properties(sqlite3.c PROPERTIES LANGUAGE C)

add_executable(PredictiveAutoHeal
    main.cpp
    sqlite3.c
)

target_link_libraries(PredictiveAutoHeal PRIVATE Advapi32 User32 Gdi32)