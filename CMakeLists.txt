cmake_minimum_required(VERSION 3.16)
project(PokerEngine VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
# -march=native ties binaries to the build machine; keep it opt-in so the
# project builds portable Release artifacts by default.
option(POKER_NATIVE_TUNE "Tune -march=native for the build host (non-portable)" OFF)
# Optional examples (e.g. the real LLM agent) pull in an HTTPS/runtime dependency,
# so keep them opt-in to preserve a lean, dependency-free core build.
option(BUILD_EXAMPLES "Build optional examples in examples/ (LLM agent, etc.)" OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(POKER_NATIVE_TUNE)
        add_compile_options(-O3 -march=native -DNDEBUG)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()
else()
    add_compile_options(-O0 -g)
endif()

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

# nlohmann_json — use vendored single header
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    add_library(nlohmann_json INTERFACE)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    target_include_directories(nlohmann_json SYSTEM INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/include
    )
endif()

# spdlog — use vendored headers
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    add_library(spdlog INTERFACE)
    add_library(spdlog::spdlog ALIAS spdlog)
    target_include_directories(spdlog SYSTEM INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include
    )
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
endif()

# fmt — use vendored headers (header-only mode)
find_package(fmt QUIET)
if(NOT fmt_FOUND)
    add_library(fmt INTERFACE)
    add_library(fmt::fmt ALIAS fmt)
    target_include_directories(fmt SYSTEM INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt/include
    )
    target_compile_definitions(fmt INTERFACE FMT_HEADER_ONLY=1)
endif()

 # Eigen3 — use vendored headers
 find_package(Eigen3 QUIET)
 if(NOT EIGEN3_FOUND)
     add_library(Eigen3 INTERFACE)
     add_library(Eigen3::Eigen ALIAS Eigen3)
     target_include_directories(Eigen3 SYSTEM INTERFACE
         ${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen3
     )
     set(EIGEN3_FOUND TRUE)
     set(Eigen3_FOUND TRUE)
 endif()

add_subdirectory(third_party)
add_subdirectory(core)
add_library(poker_engine_core ALIAS core)
add_subdirectory(equity)
add_subdirectory(phase1)
add_subdirectory(phase2)
add_subdirectory(phase3)
add_subdirectory(phase4)
add_subdirectory(phase5)
add_subdirectory(phase6)
add_subdirectory(phase7)
add_subdirectory(phase8)
add_subdirectory(phase9)
add_subdirectory(phase10)
add_subdirectory(phase11)
add_subdirectory(phase12)
add_subdirectory(phase13)
add_subdirectory(phase14)
add_subdirectory(phase15)
add_subdirectory(phase16)
add_subdirectory(phase17)
add_subdirectory(phase18)
add_subdirectory(phase19)
add_subdirectory(phase20)
add_subdirectory(phase21)
add_subdirectory(arena)
add_subdirectory(infra/net)
add_subdirectory(cli)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
add_subdirectory(tests)
