add_executable(bha_tests
        unit/utils/test_path_utils.cpp
        unit/utils/test_string_utils.cpp
        unit/utils/test_numeric_utils.cpp
        unit/utils/test_parallel.cpp
        unit/utils/test_cmake_parse_utils.cpp
        unit/utils/test_include_parse_utils.cpp
        unit/utils/test_file_utils.cpp
        unit/parsers/test_clang_parser.cpp
        unit/parsers/test_gcc_parser.cpp
        unit/parsers/test_msvc_parser.cpp
        unit/parsers/test_intel_parser.cpp
        unit/parsers/test_lld_trace_parser.cpp
        unit/parsers/test_sccache_stats_parser.cpp
        unit/parsers/test_p1689_module_parser.cpp
        unit/parsers/test_memory_parser.cpp
        unit/parsers/test_process_resource_parser.cpp
        unit/build_systems/test_adapter.cpp
        unit/build_sessions/test_cmake_instrumentation.cpp
        unit/build_sessions/test_cmake_file_api.cpp
        unit/build_sessions/test_session_file.cpp
        unit/core/test_error.cpp
        unit/core/test_result.cpp
        unit/core/test_types.cpp
        unit/core/test_project_index.cpp
        unit/suggestions/test_forward_decl_suggester.cpp
        unit/suggestions/test_explainability.cpp
        unit/suggestions/test_header_split_suggester.cpp
        unit/suggestions/test_include_suggester.cpp
        unit/suggestions/test_pch_suggester.cpp
        unit/suggestions/test_pimpl_eligibility.cpp
        unit/suggestions/test_pimpl_suggester.cpp
        unit/suggestions/test_consolidator.cpp
        unit/suggestions/test_template_suggester.cpp
        unit/suggestions/test_template_semantic_index.cpp
        unit/suggestions/test_unity_build_suggester.cpp
        unit/suggestions/test_project_root.cpp
        unit/suggestions/test_suggester_catalog.cpp
        unit/analyzers/test_dependency_analyzer.cpp
        unit/analyzers/test_file_analyzer.cpp
        unit/analyzers/test_build_session_analyzer.cpp
        unit/analyzers/test_build_target_analyzer.cpp
        unit/analyzers/test_cache_analyzer.cpp
        unit/analyzers/test_linker_analyzer.cpp
        unit/analyzers/test_module_analyzer.cpp
        unit/analyzers/test_process_resource_analyzer.cpp
        unit/analyzers/test_performance_analyzer.cpp
        unit/analyzers/test_analyzer_registry.cpp
        unit/analyzers/test_symbol_analyzer.cpp
        unit/analyzers/test_template_analyzer.cpp
        unit/exporters/test_exporter.cpp
        unit/storage/test_storage_compare.cpp
)

if(BHA_ENABLE_LSP)
    target_sources(bha_tests PRIVATE
            unit/lsp/test_suggestion_manager.cpp
            unit/lsp/test_uri.cpp
    )
endif()

if(BHA_BUILD_REFACTOR_TOOLS)
    target_sources(bha_tests PRIVATE
            ../tools/refactor/pimpl_tooling.cpp
            unit/refactor/test_pimpl_tooling.cpp
    )
    if(BHA_CLANG_TOOLING_INCLUDE_DIR AND BHA_CLANG_CPP_LIBRARY AND BHA_LLVM_LIBRARY)
        target_include_directories(bha_tests SYSTEM PRIVATE ${BHA_CLANG_TOOLING_INCLUDE_DIR})
        target_link_libraries(bha_tests PRIVATE ${BHA_CLANG_CPP_LIBRARY} ${BHA_LLVM_LIBRARY})
    endif()
endif()

target_link_libraries(bha_tests
        PRIVATE
        bha::bha
        bha::warnings
        GTest::gtest
        GTest::gtest_main
        $<$<BOOL:${BHA_ENABLE_SANITIZERS}>:bha::sanitizers>
        $<$<BOOL:${BHA_ENABLE_COVERAGE}>:bha::coverage>
)

target_compile_definitions(bha_tests PRIVATE BHA_HAVE_CLANG_TOOLING=${BHA_HAVE_CLANG_TOOLING})

if(BHA_ENABLE_LSP)
    target_link_libraries(bha_tests PRIVATE bha::lsp_core)
    target_include_directories(bha_tests PRIVATE ${PROJECT_SOURCE_DIR}/lsp/headers)
endif()

gtest_discover_tests(bha_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND BHA_BUILD_CLI)
    add_test(
            NAME ExportCrossFormatFixture
            COMMAND ${Python3_EXECUTABLE}
                    ${PROJECT_SOURCE_DIR}/tests/validate_export_fixture.py
                    --binary $<TARGET_FILE:bha>
                    --schema ${PROJECT_SOURCE_DIR}/docs/schemas/analysis-v0.1.0.schema.json
                    --trace ${PROJECT_SOURCE_DIR}/tests/fixtures/sample_clang_trace.json
    )
endif()
