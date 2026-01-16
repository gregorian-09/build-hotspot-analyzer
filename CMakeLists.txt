cmake_minimum_required(VERSION 3.28)
project(bha
        VERSION 1.0.0
        DESCRIPTION "Build Hotspot Analyzer - Identify and optimize C++ build bottlenecks"
        LANGUAGES CXX
)

# =============================================================================
# Options
# =============================================================================

option(BHA_BUILD_TESTS "Build unit and integration tests" ON)
option(BHA_BUILD_CLI "Build command-line interface" ON)
option(BHA_BUILD_PYTHON "Build Python bindings" ON)
option(BHA_BUILD_DOCS "Build documentation" OFF)
option(BHA_ENABLE_COVERAGE "Enable code coverage" OFF)
option(BHA_ENABLE_SANITIZERS "Enable sanitizers (address, undefined)" OFF)
option(BHA_BUILD_SHARED "Build shared library" OFF)

# =============================================================================
# C++ Standard
# =============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use modern FindPython (disables legacy PythonInterp behavior)
set(PYBIND11_FINDPYTHON ON)

# Generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# Compiler Warnings
# =============================================================================

add_library(bha_warnings INTERFACE)
add_library(bha::warnings ALIAS bha_warnings)

if(MSVC)
    target_compile_options(bha_warnings INTERFACE
            /W4
            /WX
            /permissive-
    )
else()
    target_compile_options(bha_warnings INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wcast-qual
            -Wformat=2
            -Wundef
            -Wshadow
            -Wcast-align
            -Wunused
            -Wnull-dereference
            -Wdouble-promotion
            -Woverloaded-virtual
            -Wnon-virtual-dtor
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(bha_warnings INTERFACE
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
                -Wuseless-cast
                -Wno-null-dereference # Disable false positive from system headers (GCC bug with streambuf)
        )
    endif()
endif()

# =============================================================================
# Sanitizers (optional)
# =============================================================================

add_library(bha_sanitizers INTERFACE)
add_library(bha::sanitizers ALIAS bha_sanitizers)

if(BHA_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(bha_sanitizers INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
    )
    target_link_options(bha_sanitizers INTERFACE
            -fsanitize=address,undefined
    )
endif()

# =============================================================================
# Coverage (optional)
# =============================================================================

add_library(bha_coverage INTERFACE)
add_library(bha::coverage ALIAS bha_coverage)

if(BHA_ENABLE_COVERAGE AND NOT MSVC)
    target_compile_options(bha_coverage INTERFACE --coverage)
    target_link_options(bha_coverage INTERFACE --coverage)
endif()

# =============================================================================
# Dependencies
# =============================================================================

# nlohmann/json for JSON parsing
include(FetchContent)

find_package(nlohmann_json CONFIG QUIET)

if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found on system, using FetchContent")

    include(FetchContent)

    FetchContent_Declare(
            json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(json)
else()
    message(STATUS "Using system-installed nlohmann_json")
endif()


# =============================================================================
# Resources (HTML templates, CSS, JS, vendor libraries)
# =============================================================================

# Must be added before bha_lib to ensure headers are generated first
add_subdirectory(resources)

# =============================================================================
# Main Library
# =============================================================================

# Header-only core library (for now)
add_library(bha_core INTERFACE)
add_library(bha::core ALIAS bha_core)

target_include_directories(bha_core INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/headers>
        $<INSTALL_INTERFACE:headers>
)

# Later Notes: nlohmann_json will be linked to bha_lib when serialization is implemented
# Core types are self-contained and don't need external dependencies

# Full library (will contain implementations)
add_library(bha_lib STATIC)
add_library(bha::bha ALIAS bha_lib)

# Make this static library suitable for linking into shared modules
set_target_properties(bha_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(bha_lib PRIVATE
        sources/bha/parsers/gcc_parser.cpp
        sources/bha/parsers/clang_parser.cpp
        sources/bha/parsers/nvcc_parser.cpp
        sources/bha/parsers/parser_registry.cpp
        sources/bha/parsers/msvc_parser.cpp
        sources/bha/parsers/intel_parser.cpp
        sources/bha/parsers/memory_parser.cpp
        sources/bha/graph/graph.cpp
        sources/bha/build_systems/adapter.cpp
        sources/bha/suggestions/forward_decl_suggester.cpp
        sources/bha/suggestions/header_split_suggester.cpp
        sources/bha/suggestions/include_suggester.cpp
        sources/bha/suggestions/pch_suggester.cpp
        sources/bha/suggestions/pimpl_suggester.cpp
        sources/bha/suggestions/suggester_registry.cpp
        sources/bha/suggestions/template_suggester.cpp
        sources/bha/suggestions/unity_build_suggester.cpp
        sources/bha/analyzers/analyzer_registry.cpp
        sources/bha/analyzers/dependency_analyzer.cpp
        sources/bha/analyzers/file_analyzer.cpp
        sources/bha/analyzers/pch_analyzer.cpp
        sources/bha/analyzers/performance_analyzer.cpp
        sources/bha/analyzers/symbol_analyzer.cpp
        sources/bha/analyzers/template_analyzer.cpp
        sources/bha/exporters/exporter.cpp
        sources/bha/git/git_integration.cpp
        sources/bha/storage/storage.cpp
)

target_include_directories(bha_lib
        PRIVATE
            ${CMAKE_BINARY_DIR}/generated/resources
)

target_link_libraries(bha_lib
        PUBLIC
            bha::core
        PRIVATE
            bha::warnings
            nlohmann_json::nlohmann_json
            $<$<BOOL:${BHA_ENABLE_SANITIZERS}>:bha::sanitizers>
            $<$<BOOL:${BHA_ENABLE_COVERAGE}>:bha::coverage>
)

# Ensure resources are generated before compiling bha_lib
add_dependencies(bha_lib generate_resources)

if(BHA_BUILD_CLI)
    add_executable(bha
            cli/commands/analyze_cmd.cpp
            cli/commands/baseline_cmd.cpp
            cli/commands/build_cmd.cpp
            cli/commands/command.cpp
            cli/commands/compare_cmd.cpp
            cli/commands/export_cmd.cpp
            cli/commands/snapshot_cmd.cpp
            cli/commands/suggest_cmd.cpp
            cli/formatter.cpp
            cli/progress.cpp
            cli/main.cpp
            cli/commands/record_cmd.cpp
    )

    target_link_libraries(bha
            PRIVATE
            bha::bha
            bha::warnings
            $<$<BOOL:${BHA_ENABLE_SANITIZERS}>:bha::sanitizers>
            $<$<BOOL:${BHA_ENABLE_COVERAGE}>:bha::coverage>
    )
endif()

# =============================================================================
# Tests
# =============================================================================

if(BHA_BUILD_TESTS)
    enable_testing()

    find_package(GTest CONFIG QUIET)

    if(NOT GTest_FOUND)
        include(FetchContent)

        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG v1.14.0
        )
        FetchContent_MakeAvailable(googletest)
    endif()

    if(NOT TARGET GTest::gtest)
        message(FATAL_ERROR "GoogleTest not found")
    endif()

    include(GoogleTest)
    add_subdirectory(tests)
endif()

# =============================================================================
# Python Bindings
# =============================================================================

if(BHA_BUILD_PYTHON)
    find_package(Python COMPONENTS Interpreter Development REQUIRED)
    find_package(pybind11 CONFIG QUIET)

    if(NOT pybind11_FOUND)
        include(FetchContent)

        FetchContent_Declare(
                pybind11
                GIT_REPOSITORY https://github.com/pybind/pybind11.git
                GIT_TAG v2.11.1
        )
        FetchContent_MakeAvailable(pybind11)
    endif()

    add_subdirectory(python)
endif()


# =============================================================================
# Documentation
# =============================================================================

if(BHA_BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_subdirectory(docs)
    endif()
endif()

# =============================================================================
# Installation
# =============================================================================

include(GNUInstallDirs)

# Install headers
install(DIRECTORY headers/bha
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install CLI executable
if(BHA_BUILD_CLI)
    install(TARGETS bha
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()