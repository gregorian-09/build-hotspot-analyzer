find_package(GTest CONFIG REQUIRED)

add_library(bha_core STATIC
        ${PROJECT_SOURCE_DIR}/sources/bha/utils/string_utils.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/utils/path_utils.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/utils/file_utils.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/utils/hash_utils.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/utils/json_utils.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/core/error.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/core/types.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/core/config.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/parsers/parser.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/parsers/clang_parser.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/parsers/gcc_parser.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/parsers/msvc_parser.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/parsers/unified_format.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/graph/graph_algorithms.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/graph/graph_builder.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/graph/compressed_graph.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/analysis/hotspot_analyzer.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/analysis/dependency_analyzer.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/analysis/impact_analyzer.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/analysis/template_analyzer.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/analysis/pch_analyzer.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/suggestions/forward_decl_suggester.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/suggestions/pch_optimizer.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/suggestions/confidence_calculator.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/suggestions/header_splitter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/suggestions/suggestion_engine.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/suggestions/pimpl_suggester.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/build_systems/build_adapter_factory.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/build_systems/cmake_adapter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/build_systems/ninja_adapter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/build_systems/make_adapter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/build_systems/msbuild_adapter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/export/exporter_factory.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/export/json_exporter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/export/html_exporter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/export/csv_exporter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/export/text_exporter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/export/markdown_exporter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/export/report_generator.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/analysis/analysis_engine.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/storage/database.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/storage/sqlite_backend.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/security/input_validator.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/security/resource_limiter.cpp
        ${PROJECT_SOURCE_DIR}/sources/bha/security/anonymizer.cpp
)

target_include_directories(bha_core
        PUBLIC
        ${PROJECT_SOURCE_DIR}/headers
)

target_link_libraries(bha_core
        PUBLIC OpenSSL::SSL
        PUBLIC simdjson::simdjson
        PUBLIC tomlplusplus::tomlplusplus
        PUBLIC nlohmann_json::nlohmann_json
        PUBLIC SQLite::SQLite3
)

file(GLOB_RECURSE UNIT_TEST_SOURCES "unit/*.cpp")
if(UNIT_TEST_SOURCES)
    add_executable(unit_tests ${UNIT_TEST_SOURCES})
    target_link_libraries(unit_tests
            PRIVATE bha_core
            PRIVATE GTest::gtest
            PRIVATE GTest::gtest_main
    )
    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()


file(GLOB_RECURSE INTEGRATION_TEST_SOURCES "integration/*.cpp")
if(INTEGRATION_TEST_SOURCES)
    add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
    target_link_libraries(integration_tests
            PRIVATE bha_core
            PRIVATE GTest::gtest
            PRIVATE GTest::gtest_main
    )
    include(GoogleTest)
    gtest_discover_tests(integration_tests)
endif()
