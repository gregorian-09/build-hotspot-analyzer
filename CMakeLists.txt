cmake_minimum_required(VERSION 3.31)
project(BuildtimeHotspotAnalyzer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_SYSTEM_OPENSSL "Use the system-installed OpenSSL" ON)

include(FetchContent)

FetchContent_Declare(
        simdjson
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v4.0.7
)

FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
)

FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
)

FetchContent_MakeAvailable(simdjson)
FetchContent_MakeAvailable(tomlplusplus)
FetchContent_MakeAvailable(json)

if(USE_SYSTEM_OPENSSL)
    if(DEFINED ENV{OPENSSL_ROOT_DIR})
        set(OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT_DIR})
    endif()
    find_package(OpenSSL REQUIRED)
else()
    find_package(OpenSSL REQUIRED)
endif()

if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not found. Please install or specify OPENSSL_ROOT_DIR.")
endif()

add_executable(BuildtimeHotspotAnalyzer main.cpp
        headers/bha/utils/string_utils.h
        sources/bha/utils/string_utils.cpp
        headers/bha/utils/path_utils.h
        sources/bha/utils/path_utils.cpp
        headers/bha/utils/file_utils.h
        sources/bha/utils/file_utils.cpp
        headers/bha/utils/hash_utils.h
        sources/bha/utils/hash_utils.cpp
        headers/bha/utils/json_utils.h
        sources/bha/utils/json_utils.cpp
        headers/bha/core/error.h
        sources/bha/core/error.cpp
        headers/bha/core/result.h
        headers/bha/core/types.h
        sources/bha/core/types.cpp
        headers/bha/core/config.h
        sources/bha/core/config.cpp
        headers/bha/parsers/parser.h
        sources/bha/parsers/parser.cpp
        headers/bha/parsers/clang_parser.h
        sources/bha/parsers/clang_parser.cpp
        headers/bha/parsers/gcc_parser.h
        sources/bha/parsers/gcc_parser.cpp
        headers/bha/parsers/msvc_parser.h
        sources/bha/parsers/msvc_parser.cpp
        headers/bha/graph/graph_algorithms.h
        sources/bha/graph/graph_algorithms.cpp
        headers/bha/graph/graph_builder.h
        sources/bha/graph/graph_builder.cpp
        headers/bha/parsers/unified_format.h
        sources/bha/parsers/unified_format.cpp
        headers/bha/graph/compressed_graph.h
        sources/bha/graph/compressed_graph.cpp
        headers/bha/analysis/hotspot_analyzer.h
        sources/bha/analysis/hotspot_analyzer.cpp
        headers/bha/analysis/dependency_analyzer.h
        sources/bha/analysis/dependency_analyzer.cpp
        headers/bha/analysis/impact_analyzer.h
        sources/bha/analysis/impact_analyzer.cpp
        headers/bha/analysis/template_analyzer.h
        sources/bha/analysis/template_analyzer.cpp
        headers/bha/analysis/pch_analyzer.h
        sources/bha/analysis/pch_analyzer.cpp
        sources/bha/suggestions/forward_decl_suggester.cpp
        headers/bha/suggestions/forward_decl_suggester.h
        headers/bha/suggestions/pch_optimizer.h
        sources/bha/suggestions/pch_optimizer.cpp
        headers/bha/suggestions/confidence_calculator.h
        sources/bha/suggestions/confidence_calculator.cpp
        headers/bha/suggestions/header_splitter.h
        sources/bha/suggestions/header_splitter.cpp
        headers/bha/suggestions/suggestion_engine.h
        headers/bha/suggestions/pimpl_suggester.h
        sources/bha/suggestions/pimpl_suggester.cpp
        sources/bha/suggestions/suggestion_engine.cpp
        headers/bha/build_systems/build_adapter.h
        sources/bha/build_systems/build_adapter_factory.cpp
        headers/bha/build_systems/cmake_adapter.h
        headers/bha/build_systems/ninja_adapter.h
        headers/bha/build_systems/make_adapter.h
        headers/bha/build_systems/msbuild_adapter.h
        sources/bha/build_systems/cmake_adapter.cpp
        sources/bha/build_systems/ninja_adapter.cpp
        sources/bha/build_systems/make_adapter.cpp
        sources/bha/build_systems/msbuild_adapter.cpp
        headers/bha/export/exporter.h
        headers/bha/export/json_exporter.h
        headers/bha/export/html_exporter.h
        headers/bha/export/csv_exporter.h
        headers/bha/export/markdown_exporter.h
        headers/bha/export/text_exporter.h
        sources/bha/export/exporter_factory.cpp
        sources/bha/export/json_exporter.cpp
        sources/bha/export/html_exporter.cpp
        sources/bha/export/csv_exporter.cpp
        sources/bha/export/text_exporter.cpp
        sources/bha/export/markdown_exporter.cpp
        headers/bha/export/report_generator.h
        sources/bha/export/report_generator.cpp
)

include_directories(${PROJECT_SOURCE_DIR}/headers)

target_link_libraries(BuildtimeHotspotAnalyzer
        PRIVATE OpenSSL::SSL
        PRIVATE simdjson::simdjson
        PRIVATE tomlplusplus::tomlplusplus
        PRIVATE nlohmann_json::nlohmann_json
)