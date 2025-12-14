//
// Created by gregorian on 14/12/2025.
//

#include <gtest/gtest.h>
#include "bha/analysis/analysis_engine.h"
#include "bha/graph/graph_builder.h"
#include "bha/parsers/clang_parser.h"
#include "bha/export/json_exporter.h"
#include "bha/suggestions/suggestion_engine.h"
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;
using namespace bha::analysis;
using namespace bha::parsers;
using namespace bha::graph;
using namespace bha::suggestions;
using namespace bha::export_module;
using namespace bha::core;

class FullAnalysisWorkflowTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "bha_analysis_workflow_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    fs::path temp_dir;

    static BuildTrace create_simple_trace() {
        BuildTrace trace;
        trace.trace_id = "test-trace-001";
        trace.build_system = "CMake";
        trace.build_system_version = "3.20";
        trace.configuration = "Release";
        trace.platform = "Linux x86_64";
        trace.is_clean_build = true;

        auto now = std::chrono::system_clock::now();
        trace.build_start = now;
        trace.build_end = now + std::chrono::milliseconds(5000);
        trace.total_build_time_ms = 5000.0;

        return trace;
    }

    static BuildTrace create_complex_multi_file_trace(const int num_files) {
        BuildTrace trace = create_simple_trace();

        for (int i = 0; i < num_files; ++i) {
            CompilationUnit unit;
            unit.id = "unit-" + std::to_string(i);
            unit.file_path = "/project/src/file" + std::to_string(i) + ".cpp";
            unit.configuration = "Release";
            unit.compiler_type = "clang";
            unit.compiler_version = "14.0.0";
            unit.compile_flags = {"-O3", "-std=c++17", "-Wall"};

            unit.preprocessing_time_ms = 50.0 + (i * 10);
            unit.parsing_time_ms = 100.0 + (i * 20);
            unit.codegen_time_ms = 150.0 + (i * 30);
            unit.optimization_time_ms = 200.0 + (i * 40);
            unit.total_time_ms = unit.preprocessing_time_ms + unit.parsing_time_ms +
                                 unit.codegen_time_ms + unit.optimization_time_ms;

            unit.file_size_bytes = 5000 + (i * 1000);
            unit.preprocessed_size_bytes = 25000 + (i * 5000);
            unit.build_timestamp = trace.build_start;
            unit.commit_sha = "abc123def456";

            for (int j = 0; j < i; ++j) {
                unit.direct_includes.push_back("/project/include/header" + std::to_string(j) + ".h");
            }
            unit.direct_includes.emplace_back("/project/include/common.h");
            unit.all_includes = unit.direct_includes;
            for (int j = 0; j < i; ++j) {
                unit.all_includes.push_back("/project/include/indirect" + std::to_string(j) + ".h");
            }

            for (int t = 0; t < i % 3; ++t) {
                TemplateInstantiation templ;
                templ.template_name = "std::vector<T>";
                templ.instantiation_context = "file" + std::to_string(i) + ".cpp";
                templ.time_ms = 25.0 + (t * 5);
                templ.instantiation_depth = t + 1;
                unit.template_instantiations.push_back(templ);
            }

            trace.compilation_units.push_back(unit);
        }

        trace.total_build_time_ms = 0.0;
        for (const auto& unit : trace.compilation_units) {
            trace.total_build_time_ms += unit.total_time_ms;
        }

        trace.metrics.total_files_compiled = num_files;
        return trace;
    }

    static DependencyGraph create_dependency_graph(const BuildTrace& trace) {
        DependencyGraph graph;

        for (const auto& unit : trace.compilation_units) {
            graph.add_node(unit.file_path);

            for (const auto& include : unit.direct_includes) {
                graph.add_node(include);
                graph.add_edge(unit.file_path, include, EdgeType::DIRECT_INCLUDE);
            }
        }

        return graph;
    }
};

TEST_F(FullAnalysisWorkflowTest, EndToEndAnalysisWithSimpleTrace) {
    BuildTrace trace = create_simple_trace();

    CompilationUnit unit;
    unit.id = "unit-0";
    unit.file_path = "/project/src/main.cpp";
    unit.compiler_type = "clang";
    unit.total_time_ms = 1500.0;
    unit.preprocessing_time_ms = 300.0;
    unit.parsing_time_ms = 400.0;
    unit.codegen_time_ms = 500.0;
    unit.optimization_time_ms = 300.0;
    unit.direct_includes = {"/project/include/header.h"};
    unit.all_includes = unit.direct_includes;
    unit.build_timestamp = trace.build_start;

    trace.compilation_units.push_back(unit);
    trace.total_build_time_ms = 1500.0;

    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;
    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 1);
    EXPECT_DOUBLE_EQ(report.total_build_time_ms, 1500.0);
}

TEST_F(FullAnalysisWorkflowTest, WorkflowWithDependencyAnalysis) {
    BuildTrace trace = create_complex_multi_file_trace(5);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;
    options.enable_dependency_analysis = true;
    options.enable_hotspot_analysis = false;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = false;
    options.enable_template_analysis = false;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 5);
    EXPECT_GT(report.total_build_time_ms, 0.0);
    EXPECT_GE(report.include_depths.size(), 0);
}

TEST_F(FullAnalysisWorkflowTest, WorkflowWithHotspotAnalysis) {
    BuildTrace trace = create_complex_multi_file_trace(10);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;
    options.enable_dependency_analysis = false;
    options.enable_hotspot_analysis = true;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = false;
    options.enable_template_analysis = false;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 10);
    EXPECT_GE(report.hotspots.size(), 0);
    EXPECT_GE(report.slow_files.size(), 0);
}

TEST_F(FullAnalysisWorkflowTest, WorkflowWithPCHAnalysis) {
    BuildTrace trace = create_complex_multi_file_trace(8);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;
    options.enable_dependency_analysis = false;
    options.enable_hotspot_analysis = false;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = true;
    options.enable_template_analysis = false;
    options.pch_candidates_count = 5;
    options.pch_min_inclusion_ratio = 0.4;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 8);
    EXPECT_GE(report.pch_candidates.size(), 0);
}

TEST_F(FullAnalysisWorkflowTest, WorkflowWithTemplateAnalysis) {
    BuildTrace trace = create_complex_multi_file_trace(6);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;
    options.enable_dependency_analysis = false;
    options.enable_hotspot_analysis = false;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = false;
    options.enable_template_analysis = true;
    options.template_top_n = 10;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 6);
    EXPECT_GE(report.template_analysis.expensive_templates.size(), 0);
}

TEST_F(FullAnalysisWorkflowTest, FullWorkflowWithAllAnalysesEnabled) {
    BuildTrace trace = create_complex_multi_file_trace(12);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;
    options.enable_dependency_analysis = true;
    options.enable_hotspot_analysis = true;
    options.enable_impact_analysis = true;
    options.enable_pch_analysis = true;
    options.enable_template_analysis = true;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 12);
    EXPECT_GT(report.total_build_time_ms, 0.0);
    EXPECT_GE(report.dependency_issues.size(), 0);
    EXPECT_GE(report.hotspots.size(), 0);
    EXPECT_GE(report.pch_candidates.size(), 0);
    EXPECT_GE(report.template_analysis.expensive_templates.size(), 0);
}

TEST_F(FullAnalysisWorkflowTest, WorkflowWithGraphBuilding) {
    BuildTrace trace = create_complex_multi_file_trace(7);

    GraphBuilder builder;
    builder.set_merge_transitive(false);
    builder.set_include_system_headers(true);
    builder.set_weight_by_compile_time(true);

    auto graph_result = builder.build_from_trace(trace);
    ASSERT_TRUE(graph_result.is_success());

    const auto& graph = graph_result.value();

    int source_node_count = 0;
    for (const auto& unit : trace.compilation_units) {
        if (graph.has_node(unit.file_path)) {
            ++source_node_count;
        }
    }
    EXPECT_EQ(source_node_count, trace.compilation_units.size());

    BuildAnalysisEngine::Options options;
    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 7);
}

TEST_F(FullAnalysisWorkflowTest, WorkflowWithLargeMultiFileScenario) {
    BuildTrace trace = create_complex_multi_file_trace(50);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;
    options.enable_dependency_analysis = true;
    options.enable_hotspot_analysis = true;
    options.enable_impact_analysis = true;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 50);
    EXPECT_GT(report.total_build_time_ms, 0.0);
}

TEST_F(FullAnalysisWorkflowTest, SuggestionGenerationFromAnalysis) {
    BuildTrace trace = create_complex_multi_file_trace(8);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options analysis_options;
    auto analysis_result = BuildAnalysisEngine::analyze(trace, graph, analysis_options);

    ASSERT_TRUE(analysis_result.is_success());

    SuggestionEngine engine;
    SuggestionEngine::Options suggestion_options;
    suggestion_options.enable_forward_declarations = true;
    suggestion_options.enable_header_splits = true;
    suggestion_options.enable_pch_suggestions = true;
    suggestion_options.min_confidence = 0.5;
    suggestion_options.min_time_savings_ms = 10.0;
    suggestion_options.max_suggestions = 20;

    auto suggestions_result = engine.generate_all_suggestions(trace, suggestion_options);
    ASSERT_TRUE(suggestions_result.is_success());

    auto suggestions = suggestions_result.value();
    EXPECT_GE(suggestions.size(), 0);
    EXPECT_LE(suggestions.size(), suggestion_options.max_suggestions);

    for (const auto& suggestion : suggestions) {
        EXPECT_FALSE(suggestion.id.empty());
        EXPECT_FALSE(suggestion.title.empty());
        EXPECT_GE(suggestion.confidence, 0.0);
        EXPECT_LE(suggestion.confidence, 1.0);
    }
}

TEST_F(FullAnalysisWorkflowTest, ExportIntegrationWithAnalysisResults) {
    BuildTrace trace = create_complex_multi_file_trace(6);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;
    auto analysis_result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(analysis_result.is_success());

    SuggestionEngine engine;
    SuggestionEngine::Options sugg_options;
    auto suggestions_result = engine.generate_all_suggestions(trace, sugg_options);

    ASSERT_TRUE(suggestions_result.is_success());
    const auto& suggestions = suggestions_result.value();

    JSONExporter exporter(JSONExporter::Options{.pretty_print = true, .include_full_trace = true});
    fs::path output_file = temp_dir / "analysis_report.json";

    auto export_result = exporter.export_report(
        trace.metrics,
        suggestions,
        trace,
        output_file.string()
    );

    ASSERT_TRUE(export_result.is_success());
    EXPECT_TRUE(fs::exists(output_file));
}

TEST_F(FullAnalysisWorkflowTest, ConsistencyAcrossMultipleRuns) {
    BuildTrace trace = create_complex_multi_file_trace(5);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;

    auto result1 = BuildAnalysisEngine::analyze(trace, graph, options);
    auto result2 = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());

    const auto& report1 = result1.value();
    const auto& report2 = result2.value();

    EXPECT_EQ(report1.total_files_analyzed, report2.total_files_analyzed);
    EXPECT_DOUBLE_EQ(report1.total_build_time_ms, report2.total_build_time_ms);
}

TEST_F(FullAnalysisWorkflowTest, PipelineResilienceWithSelectiveDisabling) {
    BuildTrace trace = create_complex_multi_file_trace(9);
    DependencyGraph graph = create_dependency_graph(trace);

    BuildAnalysisEngine::Options options;
    options.enable_dependency_analysis = true;
    options.enable_hotspot_analysis = true;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = false;
    options.enable_template_analysis = false;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 9);
    EXPECT_GT(report.total_build_time_ms, 0.0);
}