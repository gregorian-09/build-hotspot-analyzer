//
// Created by gregorian on 09/12/2025.
//

#include <gtest/gtest.h>
#include "bha/analysis/analysis_engine.h"

using namespace bha::analysis;
using namespace bha::core;

class AnalysisEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        trace = BuildTrace{};
        graph = DependencyGraph{};
        options = BuildAnalysisEngine::Options{};
    }

    BuildTrace trace;
    DependencyGraph graph;
    BuildAnalysisEngine::Options options;

    void CreateSimpleTrace() {
        for (int i = 0; i < 5; ++i) {
            CompilationUnit unit;
            unit.file_path = "file" + std::to_string(i) + ".cpp";
            unit.total_time_ms = static_cast<double>(i * 500 + 1000);
            unit.preprocessing_time_ms = unit.total_time_ms * 0.1;
            unit.parsing_time_ms = unit.total_time_ms * 0.2;
            unit.codegen_time_ms = unit.total_time_ms * 0.4;

            TemplateInstantiation temp;
            temp.template_name = "std::vector<int>";
            temp.time_ms = unit.total_time_ms * 0.15;
            unit.template_instantiations.push_back(temp);

            trace.compilation_units.push_back(unit);
        }
        trace.total_build_time_ms = 15000.0;
    }

    void CreateComplexTrace() {
        std::vector<std::pair<std::string, double>> files = {
            {"main.cpp", 5000.0},
            {"module1.cpp", 3500.0},
            {"module2.cpp", 2800.0},
            {"module3.cpp", 2200.0},
            {"utils.cpp", 1500.0},
            {"common.h", 800.0}
        };

        for (const auto& [path, time] : files) {
            CompilationUnit unit;
            unit.file_path = path;
            unit.total_time_ms = time;
            unit.preprocessing_time_ms = time * 0.1;
            unit.parsing_time_ms = time * 0.2;
            unit.codegen_time_ms = time * 0.4;

            TemplateInstantiation temp;
            temp.template_name = "std::map<std::string, std::vector<int>>";
            temp.time_ms = time * 0.2;
            unit.template_instantiations.push_back(temp);

            trace.compilation_units.push_back(unit);
            trace.total_build_time_ms += unit.total_time_ms;
        }
    }

    void CreateSimpleGraph() {
        graph.add_node("common.h");
        graph.add_node("util.h");
        graph.add_node("main.cpp");
        graph.add_node("file0.cpp");
        graph.add_node("file1.cpp");

        graph.add_edge("main.cpp", "common.h");
        graph.add_edge("main.cpp", "util.h");
        graph.add_edge("file0.cpp", "common.h");
        graph.add_edge("file1.cpp", "util.h");
    }

    void CreateComplexGraph() {
        graph.add_node("base.h");
        graph.add_node("common.h");
        graph.add_node("utils.h");
        graph.add_node("main.cpp");
        graph.add_node("module1.cpp");
        graph.add_node("module2.cpp");
        graph.add_node("module3.cpp");
        graph.add_node("utils.cpp");
        graph.add_node("common.h");

        graph.add_edge("main.cpp", "base.h");
        graph.add_edge("main.cpp", "common.h");
        graph.add_edge("main.cpp", "utils.h");
        graph.add_edge("module1.cpp", "base.h");
        graph.add_edge("module1.cpp", "common.h");
        graph.add_edge("module2.cpp", "common.h");
        graph.add_edge("module2.cpp", "utils.h");
        graph.add_edge("module3.cpp", "base.h");
        graph.add_edge("module3.cpp", "utils.h");
        graph.add_edge("utils.cpp", "utils.h");
        graph.add_edge("common.h", "base.h");
    }
};

TEST_F(AnalysisEngineTest, AnalyzeWithEmptyData) {
    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.total_files_analyzed, 0);
    EXPECT_DOUBLE_EQ(report.total_build_time_ms, 0.0);
}

TEST_F(AnalysisEngineTest, AnalyzeWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_GT(report.total_files_analyzed, 0);
    EXPECT_GT(report.total_build_time_ms, 0.0);
}

TEST_F(AnalysisEngineTest, AnalyzeWithComplexData) {
    CreateComplexTrace();
    CreateComplexGraph();
    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_GT(report.total_files_analyzed, 0);
    EXPECT_GT(report.total_build_time_ms, 0.0);
}

TEST_F(AnalysisEngineTest, AllAnalyzersEnabled) {
    options.enable_dependency_analysis = true;
    options.enable_hotspot_analysis = true;
    options.enable_impact_analysis = true;
    options.enable_pch_analysis = true;
    options.enable_template_analysis = true;

    CreateComplexTrace();
    CreateComplexGraph();
    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();

    EXPECT_GE(report.hotspots.size(), 0);
    EXPECT_GE(report.slow_files.size(), 0);
    EXPECT_GE(report.hot_headers.size(), 0);
    EXPECT_GE(report.critical_path.size(), 0);
    EXPECT_GE(report.dependency_issues.size(), 0);
    EXPECT_GE(report.dependency_cycles.size(), 0);
    EXPECT_GE(report.pch_candidates.size(), 0);
}

TEST_F(AnalysisEngineTest, DependencyAnalysisOnly) {
    options.enable_dependency_analysis = true;
    options.enable_hotspot_analysis = false;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = false;
    options.enable_template_analysis = false;

    CreateSimpleGraph();
    const auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(AnalysisEngineTest, HotspotAnalysisOnly) {
    options.enable_dependency_analysis = false;
    options.enable_hotspot_analysis = true;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = false;
    options.enable_template_analysis = false;

    CreateSimpleTrace();
    const auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(AnalysisEngineTest, ImpactAnalysisOnly) {
    options.enable_dependency_analysis = false;
    options.enable_hotspot_analysis = false;
    options.enable_impact_analysis = true;
    options.enable_pch_analysis = false;
    options.enable_template_analysis = false;

    CreateSimpleGraph();
    CreateSimpleTrace();
    const auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(AnalysisEngineTest, PCHAnalysisOnly) {
    options.enable_dependency_analysis = false;
    options.enable_hotspot_analysis = false;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = true;
    options.enable_template_analysis = false;

    CreateSimpleTrace();
    CreateSimpleGraph();
    const auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(AnalysisEngineTest, TemplateAnalysisOnly) {
    options.enable_dependency_analysis = false;
    options.enable_hotspot_analysis = false;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = false;
    options.enable_template_analysis = true;

    CreateSimpleTrace();
    const auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(AnalysisEngineTest, AllAnalyzersDisabled) {
    options.enable_dependency_analysis = false;
    options.enable_hotspot_analysis = false;
    options.enable_impact_analysis = false;
    options.enable_pch_analysis = false;
    options.enable_template_analysis = false;

    CreateComplexTrace();
    const auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(AnalysisEngineTest, ProduceComprehensiveReport) {
    CreateComplexTrace();
    CreateComplexGraph();
    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();

    EXPECT_GE(report.hotspots.size(), 0);
    EXPECT_GE(report.slow_files.size(), 0);
    EXPECT_GE(report.hot_headers.size(), 0);
    EXPECT_GE(report.critical_path.size(), 0);
    EXPECT_GE(report.dependency_issues.size(), 0);
    EXPECT_GE(report.dependency_cycles.size(), 0);
    EXPECT_GE(report.include_depths.size(), 0);
    EXPECT_GE(report.impact_by_file.size(), 0);
    EXPECT_GE(report.fragile_headers.size(), 0);
    EXPECT_GE(report.pch_candidates.size(), 0);
}

TEST_F(AnalysisEngineTest, AnalysisReportStructureDefaults) {
    AnalysisReport report;

    EXPECT_EQ(report.total_build_time_ms, 0.0);
    EXPECT_EQ(report.total_files_analyzed, 0);
    EXPECT_EQ(report.dependency_issues.size(), 0);
    EXPECT_EQ(report.dependency_cycles.size(), 0);
    EXPECT_EQ(report.hotspots.size(), 0);
    EXPECT_EQ(report.slow_files.size(), 0);
    EXPECT_EQ(report.hot_headers.size(), 0);
    EXPECT_EQ(report.critical_path.size(), 0);
    EXPECT_EQ(report.impact_by_file.size(), 0);
    EXPECT_EQ(report.fragile_headers.size(), 0);
    EXPECT_EQ(report.pch_candidates.size(), 0);
}

TEST_F(AnalysisEngineTest, OptionsStructureDefaults) {
    BuildAnalysisEngine::Options opts;

    EXPECT_TRUE(opts.enable_dependency_analysis);
    EXPECT_TRUE(opts.enable_hotspot_analysis);
    EXPECT_TRUE(opts.enable_impact_analysis);
    EXPECT_TRUE(opts.enable_pch_analysis);
    EXPECT_TRUE(opts.enable_template_analysis);
    EXPECT_EQ(opts.pch_candidates_count, 10);
    EXPECT_DOUBLE_EQ(opts.pch_min_inclusion_ratio, 0.5);
    EXPECT_EQ(opts.template_top_n, 20);
    EXPECT_EQ(opts.fragile_header_threshold, 10);
}

TEST_F(AnalysisEngineTest, OptionsCustomConfiguration) {
    BuildAnalysisEngine::Options opts;
    opts.pch_candidates_count = 20;
    opts.pch_min_inclusion_ratio = 0.7;
    opts.template_top_n = 50;
    opts.fragile_header_threshold = 15;

    EXPECT_EQ(opts.pch_candidates_count, 20);
    EXPECT_DOUBLE_EQ(opts.pch_min_inclusion_ratio, 0.7);
    EXPECT_EQ(opts.template_top_n, 50);
    EXPECT_EQ(opts.fragile_header_threshold, 15);
}

TEST_F(AnalysisEngineTest, ReportWithMultipleHotspots) {
    CreateComplexTrace();
    CreateComplexGraph();
    options.hotspot_options.top_n = 10;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();

    EXPECT_LE(report.slow_files.size(), 10);
    for (const auto& hotspot : report.slow_files) {
        EXPECT_FALSE(hotspot.file_path.empty());
        EXPECT_GE(hotspot.time_ms, 0.0);
    }
}

TEST_F(AnalysisEngineTest, ReportWithDependencyData) {
    CreateComplexGraph();
    options.enable_dependency_analysis = true;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();

    EXPECT_GE(report.include_depths.size(), 0);
    EXPECT_GE(report.dependency_cycles.size(), 0);
}

TEST_F(AnalysisEngineTest, ReportWithPCHData) {
    CreateComplexTrace();
    CreateComplexGraph();
    options.enable_pch_analysis = true;
    options.pch_candidates_count = 5;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();

    EXPECT_LE(report.pch_candidates.size(), 5);
    for (const auto& candidate : report.pch_candidates) {
        EXPECT_FALSE(candidate.header.empty());
        EXPECT_GE(candidate.benefit_score, 0.0);
    }
}

TEST_F(AnalysisEngineTest, ReportWithTemplateData) {
    CreateComplexTrace();
    options.enable_template_analysis = true;
    options.template_top_n = 10;

    auto result = BuildAnalysisEngine::analyze(trace, graph, options);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();

    EXPECT_GE(report.template_analysis.expensive_templates.size(), 0);
    EXPECT_GE(report.template_analysis.total_template_time_ms, 0.0);
}
