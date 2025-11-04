cmake_minimum_required(VERSION 3.31)
project(BuildtimeHotspotAnalyzer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    include(CTest)
endif()

find_package(simdjson CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(bha_core STATIC
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
        sources/bha/security/input_validator.cpp
        sources/bha/security/resource_limiter.cpp
        sources/bha/security/anonymizer.cpp
)

target_include_directories(bha_core
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/headers>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(bha_core
        PUBLIC
        OpenSSL::SSL
        simdjson::simdjson
        tomlplusplus::tomlplusplus
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_executable(BuildtimeHotspotAnalyzer main.cpp)

target_link_libraries(BuildtimeHotspotAnalyzer
        PRIVATE bha_core
)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_subdirectory(tests)
endif()