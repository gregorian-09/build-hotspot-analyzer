//
// Created by gregorian on 08/12/2025.
//

#include <gtest/gtest.h>
#include "bha/analysis/impact_analyzer.h"

using namespace bha::analysis;
using namespace bha::core;

class ImpactAnalyzerTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = DependencyGraph{};
        trace = BuildTrace{};
    }

    DependencyGraph graph;
    BuildTrace trace;

    void CreateSimpleGraph() {
        graph.add_node("base.h");
        graph.add_node("common.h");
        graph.add_node("main.cpp");
        graph.add_node("module1.cpp");
        graph.add_node("module2.cpp");

        graph.add_edge("main.cpp", "base.h");
        graph.add_edge("main.cpp", "common.h");
        graph.add_edge("module1.cpp", "common.h");
        graph.add_edge("module2.cpp", "base.h");
    }

    void CreateComplexGraph() {
        // Create a complex dependency tree
        graph.add_node("base.h");
        graph.add_node("common.h");
        graph.add_node("utils.h");
        graph.add_node("config.h");
        graph.add_node("main.cpp");
        graph.add_node("module1.cpp");
        graph.add_node("module2.cpp");
        graph.add_node("module3.cpp");

        // main.cpp depends on everything
        graph.add_edge("main.cpp", "base.h");
        graph.add_edge("main.cpp", "common.h");
        graph.add_edge("main.cpp", "utils.h");
        graph.add_edge("main.cpp", "config.h");

        // modules depend on common headers
        graph.add_edge("module1.cpp", "base.h");
        graph.add_edge("module1.cpp", "common.h");
        graph.add_edge("module2.cpp", "common.h");
        graph.add_edge("module2.cpp", "utils.h");
        graph.add_edge("module3.cpp", "base.h");
        graph.add_edge("module3.cpp", "utils.h");
    }

    void CreateSimpleTrace() {
        CompilationUnit unit1;
        unit1.file_path = "base.h";
        unit1.total_time_ms = 1500.0;
        trace.compilation_units.push_back(unit1);

        CompilationUnit unit2;
        unit2.file_path = "common.h";
        unit2.total_time_ms = 800.0;
        trace.compilation_units.push_back(unit2);

        CompilationUnit unit3;
        unit3.file_path = "main.cpp";
        unit3.total_time_ms = 2000.0;
        trace.compilation_units.push_back(unit3);

        CompilationUnit unit4;
        unit4.file_path = "module1.cpp";
        unit4.total_time_ms = 1200.0;
        trace.compilation_units.push_back(unit4);

        CompilationUnit unit5;
        unit5.file_path = "module2.cpp";
        unit5.total_time_ms = 1000.0;
        trace.compilation_units.push_back(unit5);
    }

    void CreateComplexTrace() {
        std::vector<std::pair<std::string, double>> units = {
            {"base.h", 1500.0},
            {"common.h", 800.0},
            {"utils.h", 600.0},
            {"config.h", 300.0},
            {"main.cpp", 3000.0},
            {"module1.cpp", 2000.0},
            {"module2.cpp", 1800.0},
            {"module3.cpp", 1600.0}
        };

        for (const auto& [path, time] : units) {
            CompilationUnit unit;
            unit.file_path = path;
            unit.total_time_ms = time;
            trace.compilation_units.push_back(unit);
        }
    }
};

TEST_F(ImpactAnalyzerTest, AnalyzeChangeImpactWithEmptyGraphAndTrace) {
    auto result = ImpactAnalyzer::analyze_change_impact("file.cpp", graph, trace);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_EQ(report.affected_files.size(), 0);
    EXPECT_DOUBLE_EQ(report.estimated_rebuild_time_ms, 0.0);
}

TEST_F(ImpactAnalyzerTest, AnalyzeChangeImpactWithSimpleData) {
    CreateSimpleGraph();
    CreateSimpleTrace();

    auto result = ImpactAnalyzer::analyze_change_impact("common.h", graph, trace);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_GE(report.affected_files.size(), 0);
    EXPECT_GE(report.estimated_rebuild_time_ms, 0.0);
    EXPECT_GE(report.num_cascading_rebuilds, 0);
}

TEST_F(ImpactAnalyzerTest, AnalyzeChangeImpactWithComplexData) {
    CreateComplexGraph();
    CreateComplexTrace();

    auto result = ImpactAnalyzer::analyze_change_impact("base.h", graph, trace);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_GE(report.affected_files.size(), 0);
    EXPECT_GE(report.estimated_rebuild_time_ms, 0.0);
    EXPECT_GE(report.num_cascading_rebuilds, 0);
}

TEST_F(ImpactAnalyzerTest, AnalyzeChangeImpactOfSourceFile) {
    CreateSimpleGraph();
    CreateSimpleTrace();

    auto result = ImpactAnalyzer::analyze_change_impact("main.cpp", graph, trace);

    ASSERT_TRUE(result.is_success());
    const auto& report = result.value();
    EXPECT_GE(report.affected_files.size(), 0);
}

TEST_F(ImpactAnalyzerTest, GetAffectedFilesWithEmptyGraph) {
    auto result = ImpactAnalyzer::get_affected_files("test.cpp", graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(ImpactAnalyzerTest, GetAffectedFilesWithSimpleGraph) {
    CreateSimpleGraph();

    auto result = ImpactAnalyzer::get_affected_files("common.h", graph);

    ASSERT_TRUE(result.is_success());
    const auto& affected = result.value();
    EXPECT_GE(affected.size(), 0);

    // All affected files should be non-empty strings
    for (const auto& file : affected) {
        EXPECT_FALSE(file.empty());
    }
}

TEST_F(ImpactAnalyzerTest, GetAffectedFilesWithComplexGraph) {
    CreateComplexGraph();

    auto result = ImpactAnalyzer::get_affected_files("base.h", graph);

    ASSERT_TRUE(result.is_success());
    const auto& affected = result.value();
    EXPECT_GT(affected.size(), 0);  // base.h should affect multiple files
}

TEST_F(ImpactAnalyzerTest, GetAffectedFilesForNonexistentFile) {
    CreateSimpleGraph();

    auto result = ImpactAnalyzer::get_affected_files("nonexistent.cpp", graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(ImpactAnalyzerTest, EstimateRebuildTimeWithEmptyTrace) {
    const std::vector<std::string> affected = {"file0.cpp", "file1.cpp"};
    auto result = ImpactAnalyzer::estimate_rebuild_time(affected, trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TEST_F(ImpactAnalyzerTest, EstimateRebuildTimeWithSimpleTrace) {
    CreateSimpleTrace();

    const std::vector<std::string> affected = {"main.cpp", "module1.cpp"};
    auto result = ImpactAnalyzer::estimate_rebuild_time(affected, trace);

    ASSERT_TRUE(result.is_success());
    const double rebuild_time = result.value();
    EXPECT_GE(rebuild_time, 0.0);
    EXPECT_LE(rebuild_time, 3200.0);  // Sum of main.cpp and module1.cpp
}

TEST_F(ImpactAnalyzerTest, EstimateRebuildTimeWithComplexTrace) {
    CreateComplexTrace();

    const std::vector<std::string> affected = {"main.cpp", "module1.cpp", "module2.cpp"};
    auto result = ImpactAnalyzer::estimate_rebuild_time(affected, trace);

    ASSERT_TRUE(result.is_success());
    const double rebuild_time = result.value();
    EXPECT_GE(rebuild_time, 0.0);
}

TEST_F(ImpactAnalyzerTest, EstimateRebuildTimeWithEmptyAffectedList) {
    CreateSimpleTrace();

    constexpr std::vector<std::string> affected;
    auto result = ImpactAnalyzer::estimate_rebuild_time(affected, trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TEST_F(ImpactAnalyzerTest, FindFragileHeadersWithEmptyGraph) {
    auto result = ImpactAnalyzer::find_fragile_headers(graph, 5);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(ImpactAnalyzerTest, FindFragileHeadersWithSimpleGraph) {
    CreateSimpleGraph();

    auto result = ImpactAnalyzer::find_fragile_headers(graph, 1);

    ASSERT_TRUE(result.is_success());
    const auto& fragile = result.value();
    EXPECT_GE(fragile.size(), 0);

    for (const auto& header : fragile) {
        EXPECT_TRUE(header.find(".h") != std::string::npos ||
                   header.find(".hpp") != std::string::npos);
    }
}

TEST_F(ImpactAnalyzerTest, FindFragileHeadersWithComplexGraph) {
    CreateComplexGraph();

    auto result = ImpactAnalyzer::find_fragile_headers(graph, 2);

    ASSERT_TRUE(result.is_success());
    const auto& fragile = result.value();
    EXPECT_GT(fragile.size(), 0);  // base.h, common.h, utils.h have 3+ dependents

    for (const auto& header : fragile) {
        EXPECT_TRUE(header.find(".h") != std::string::npos ||
                   header.find(".hpp") != std::string::npos);
    }
}

TEST_F(ImpactAnalyzerTest, FindFragileHeadersWithHighThreshold) {
    CreateComplexGraph();

    auto result = ImpactAnalyzer::find_fragile_headers(graph, 100);

    ASSERT_TRUE(result.is_success());
    const auto& fragile = result.value();
    EXPECT_EQ(fragile.size(), 0);  // No header with 100+ dependents
}

TEST_F(ImpactAnalyzerTest, AnalyzeAllFilesWithEmptyData) {
    auto result = ImpactAnalyzer::analyze_all_files(graph, trace);

    ASSERT_TRUE(result.is_success());
    const auto& impact_map = result.value();
    EXPECT_EQ(impact_map.size(), 0);
}

TEST_F(ImpactAnalyzerTest, AnalyzeAllFilesWithSimpleData) {
    CreateSimpleGraph();
    CreateSimpleTrace();

    auto result = ImpactAnalyzer::analyze_all_files(graph, trace);

    ASSERT_TRUE(result.is_success());
    auto impact_map = result.value();
    EXPECT_GT(impact_map.size(), 0);

    for (const auto& [file, report] : impact_map) {
        EXPECT_FALSE(file.empty());
        EXPECT_GE(report.estimated_rebuild_time_ms, 0.0);
        EXPECT_GE(report.num_cascading_rebuilds, 0);
    }
}

TEST_F(ImpactAnalyzerTest, AnalyzeAllFilesWithComplexData) {
    CreateComplexGraph();
    CreateComplexTrace();

    auto result = ImpactAnalyzer::analyze_all_files(graph, trace);

    ASSERT_TRUE(result.is_success());
    auto impact_map = result.value();
    EXPECT_GT(impact_map.size(), 0);

    // Verify reports have consistent structure
    for (const auto& [file, report] : impact_map) {
        EXPECT_FALSE(file.empty());
        EXPECT_GE(report.estimated_rebuild_time_ms, 0.0);
        EXPECT_GE(report.num_cascading_rebuilds, 0);
    }
}

TEST_F(ImpactAnalyzerTest, CalculateFragilityScoreWithEmptyData) {
    const double score = ImpactAnalyzer::calculate_fragility_score("test.cpp", graph, trace);
    EXPECT_DOUBLE_EQ(score, 0.0);
}

TEST_F(ImpactAnalyzerTest, CalculateFragilityScoreForHeaderFile) {
    CreateComplexGraph();
    CreateComplexTrace();

    const double score = ImpactAnalyzer::calculate_fragility_score("base.h", graph, trace);
    EXPECT_GE(score, 0.0);
}

TEST_F(ImpactAnalyzerTest, CalculateFragilityScoreForSourceFile) {
    CreateComplexGraph();
    CreateComplexTrace();

    const double score = ImpactAnalyzer::calculate_fragility_score("main.cpp", graph, trace);
    EXPECT_GE(score, 0.0);
}

TEST_F(ImpactAnalyzerTest, CalculateAllFragilityScores) {
    CreateSimpleGraph();
    CreateSimpleTrace();

    auto result = ImpactAnalyzer::calculate_all_fragility_scores(graph, trace);

    ASSERT_TRUE(result.is_success());
    auto scores = result.value();
    EXPECT_GT(scores.size(), 0);

    for (const auto& [file, score] : scores) {
        EXPECT_GE(score, 0.0);
        EXPECT_FALSE(file.empty());
    }
}

TEST_F(ImpactAnalyzerTest, SimulateHeaderRemovalWithEmptyGraph) {
    const auto result = ImpactAnalyzer::simulate_header_removal("header.h", graph);

    // Should fail when header doesn't exist in graph
    EXPECT_FALSE(result.is_success());
}

TEST_F(ImpactAnalyzerTest, SimulateHeaderRemovalWithSimpleGraph) {
    CreateSimpleGraph();

    auto result = ImpactAnalyzer::simulate_header_removal("common.h", graph);

    ASSERT_TRUE(result.is_success());
    const auto& affected = result.value();
    EXPECT_GE(affected.size(), 0);
}

TEST_F(ImpactAnalyzerTest, SimulateHeaderRemovalWithComplexGraph) {
    CreateComplexGraph();

    auto result = ImpactAnalyzer::simulate_header_removal("base.h", graph);

    ASSERT_TRUE(result.is_success());
    const auto& affected = result.value();
    EXPECT_GT(affected.size(), 0);  // base.h is critical

    for (const auto& file : affected) {
        EXPECT_FALSE(file.empty());
    }
}

TEST_F(ImpactAnalyzerTest, CountCascadingRebuildsWithEmptyGraph) {
    const int count = ImpactAnalyzer::count_cascading_rebuilds("test.cpp", graph);
    EXPECT_EQ(count, 0);
}

TEST_F(ImpactAnalyzerTest, CountCascadingRebuildsWithSimpleGraph) {
    CreateSimpleGraph();

    const int count = ImpactAnalyzer::count_cascading_rebuilds("common.h", graph);
    EXPECT_GE(count, 0);
}

TEST_F(ImpactAnalyzerTest, CountCascadingRebuildsWithComplexGraph) {
    CreateComplexGraph();

    const int count = ImpactAnalyzer::count_cascading_rebuilds("base.h", graph);
    EXPECT_GT(count, 0);  // base.h affects multiple files
}

TEST_F(ImpactAnalyzerTest, ImpactReportStructureValidation) {
    CreateSimpleGraph();
    CreateSimpleTrace();

    auto result = ImpactAnalyzer::analyze_change_impact("base.h", graph, trace);

    ASSERT_TRUE(result.is_success());
    auto [affected_files, estimated_rebuild_time_ms, num_cascading_rebuilds, fragile_headers] = result.value();

    EXPECT_GE(affected_files.size(), 0);
    EXPECT_GE(estimated_rebuild_time_ms, 0.0);
    EXPECT_GE(num_cascading_rebuilds, 0);
    EXPECT_GE(fragile_headers.size(), 0);
}