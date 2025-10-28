cmake_minimum_required(VERSION 3.31)
project(BuildtimeHotspotAnalyzer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_SYSTEM_OPENSSL "Use the system-installed OpenSSL" ON)
option(USE_SYSTEM_SQLITE3 "Use the system-installed SQLite3" ON)

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

add_executable(BuildtimeHotspotAnalyzer
        main.cpp
)

target_sources(BuildtimeHotspotAnalyzer
        PRIVATE
        sources/bha/utils/string_utils.cpp
        sources/bha/utils/path_utils.cpp
        sources/bha/utils/file_utils.cpp
        sources/bha/utils/hash_utils.cpp
        sources/bha/utils/json_utils.cpp
        sources/bha/core/error.cpp
        sources/bha/core/types.cpp
        sources/bha/core/config.cpp
        sources/bha/parsers/parser.cpp
        sources/bha/parsers/clang_parser.cpp
        sources/bha/parsers/gcc_parser.cpp
        sources/bha/parsers/msvc_parser.cpp
        sources/bha/parsers/unified_format.cpp
        sources/bha/graph/graph_algorithms.cpp
        sources/bha/graph/graph_builder.cpp
        sources/bha/graph/compressed_graph.cpp
        sources/bha/analysis/hotspot_analyzer.cpp
        sources/bha/analysis/dependency_analyzer.cpp
        sources/bha/analysis/impact_analyzer.cpp
        sources/bha/analysis/template_analyzer.cpp
        sources/bha/analysis/pch_analyzer.cpp
        sources/bha/suggestions/forward_decl_suggester.cpp
        sources/bha/suggestions/pch_optimizer.cpp
        sources/bha/suggestions/confidence_calculator.cpp
        sources/bha/suggestions/header_splitter.cpp
        sources/bha/suggestions/suggestion_engine.cpp
        sources/bha/suggestions/pimpl_suggester.cpp
        sources/bha/build_systems/build_adapter_factory.cpp
        sources/bha/build_systems/cmake_adapter.cpp
        sources/bha/build_systems/ninja_adapter.cpp
        sources/bha/build_systems/make_adapter.cpp
        sources/bha/build_systems/msbuild_adapter.cpp
        sources/bha/export/exporter_factory.cpp
        sources/bha/export/json_exporter.cpp
        sources/bha/export/html_exporter.cpp
        sources/bha/export/csv_exporter.cpp
        sources/bha/export/text_exporter.cpp
        sources/bha/export/markdown_exporter.cpp
        sources/bha/export/report_generator.cpp
        sources/bha/analysis/analysis_engine.cpp
        sources/bha/storage/database.cpp
        sources/bha/storage/sqlite_backend.cpp
)

target_include_directories(BuildtimeHotspotAnalyzer
        PRIVATE
        ${PROJECT_SOURCE_DIR}/headers
)

target_link_libraries(BuildtimeHotspotAnalyzer
        PRIVATE OpenSSL::SSL
        PRIVATE simdjson::simdjson
        PRIVATE tomlplusplus::tomlplusplus
        PRIVATE nlohmann_json::nlohmann_json
        PRIVATE SQLite::SQLite3
)