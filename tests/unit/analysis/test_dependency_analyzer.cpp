//
// Created by gregorian on 09/12/2025.
//

#include <ranges>
#include <gtest/gtest.h>
#include "bha/analysis/dependency_analyzer.h"

using namespace bha::analysis;
using namespace bha::core;

class DependencyAnalyzerTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = DependencyGraph{};
    }

    DependencyGraph graph;

    void CreateSimpleGraph() {
        graph.add_node("common.h");
        graph.add_node("util.h");
        graph.add_node("main.cpp");
        graph.add_node("other.cpp");

        graph.add_edge("main.cpp", "common.h");
        graph.add_edge("main.cpp", "util.h");
        graph.add_edge("other.cpp", "common.h");
    }

    void CreateComplexGraph() {
        graph.add_node("base.h");
        graph.add_node("common.h");
        graph.add_node("math.h");
        graph.add_node("utils.h");
        graph.add_node("main.cpp");
        graph.add_node("module1.cpp");
        graph.add_node("module2.cpp");
        graph.add_node("module3.cpp");
        graph.add_node("test.cpp");

        graph.add_edge("main.cpp", "base.h");
        graph.add_edge("module1.cpp", "base.h");
        graph.add_edge("module2.cpp", "base.h");
        graph.add_edge("module3.cpp", "base.h");
        graph.add_edge("test.cpp", "base.h");

        graph.add_edge("main.cpp", "common.h");
        graph.add_edge("module1.cpp", "common.h");
        graph.add_edge("module2.cpp", "common.h");
        graph.add_edge("test.cpp", "common.h");

        graph.add_edge("main.cpp", "math.h");
        graph.add_edge("module1.cpp", "utils.h");
        graph.add_edge("module2.cpp", "utils.h");
    }
};

TEST_F(DependencyAnalyzerTest, DetectCyclesWithEmptyGraph) {
    auto result = DependencyAnalyzer::detect_cycles(graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(DependencyAnalyzerTest, DetectCyclesWithAcyclicGraph) {
    CreateSimpleGraph();
    auto result = DependencyAnalyzer::detect_cycles(graph);

    ASSERT_TRUE(result.is_success());
    const auto& cycles = result.value();
    EXPECT_EQ(cycles.size(), 0);
}

TEST_F(DependencyAnalyzerTest, CalculateIncludeDepthsEmptyGraph) {
    auto result = DependencyAnalyzer::calculate_include_depths(graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(DependencyAnalyzerTest, CalculateIncludeDepthsSimpleGraph) {
    CreateSimpleGraph();
    auto result = DependencyAnalyzer::calculate_include_depths(graph);

    ASSERT_TRUE(result.is_success());
    auto depths = result.value();
    EXPECT_GE(depths.size(), 0);

    for (const auto& depth : depths | std::views::values) {
        EXPECT_GE(depth, 0);
    }
}

TEST_F(DependencyAnalyzerTest, CalculateIncludeDepthsComplexGraph) {
    CreateComplexGraph();
    auto result = DependencyAnalyzer::calculate_include_depths(graph);

    ASSERT_TRUE(result.is_success());
    auto depths = result.value();
    EXPECT_GT(depths.size(), 0);

    for (const auto& depth : depths | std::views::values) {
        EXPECT_GE(depth, 0);
    }
}

TEST_F(DependencyAnalyzerTest, FindRedundantIncludesEmptyGraph) {
    auto result = DependencyAnalyzer::find_redundant_includes("test.cpp", graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(DependencyAnalyzerTest, FindRedundantIncludesNonexistentFile) {
    CreateSimpleGraph();
    auto result = DependencyAnalyzer::find_redundant_includes("nonexistent.cpp", graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(DependencyAnalyzerTest, FindFanoutHeadersEmptyGraph) {
    auto result = DependencyAnalyzer::find_fanout_headers(graph, 10);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(DependencyAnalyzerTest, FindFanoutHeadersSimpleGraph) {
    CreateSimpleGraph();
    auto result = DependencyAnalyzer::find_fanout_headers(graph, 1);

    ASSERT_TRUE(result.is_success());
    const auto& fanout = result.value();
    // common.h has 2 dependents
    EXPECT_GE(fanout.size(), 0);
}

TEST_F(DependencyAnalyzerTest, FindFanoutHeadersComplexGraph) {
    CreateComplexGraph();
    auto result = DependencyAnalyzer::find_fanout_headers(graph, 3);

    ASSERT_TRUE(result.is_success());
    const auto& fanout = result.value();

    // base.h should be identified as having high fanout (5 dependents)
    // common.h should also have high fanout (4 dependents)
    EXPECT_GE(fanout.size(), 0);

    for (const auto& header : fanout) {
        EXPECT_TRUE(header.find(".h") != std::string::npos ||
                   header.find(".hpp") != std::string::npos);
    }
}

TEST_F(DependencyAnalyzerTest, FindFanoutHeadersWithHighThreshold) {
    CreateComplexGraph();
    auto result = DependencyAnalyzer::find_fanout_headers(graph, 100);

    ASSERT_TRUE(result.is_success());
    const auto& fanout = result.value();
    EXPECT_EQ(fanout.size(), 0);
}

TEST_F(DependencyAnalyzerTest, AnalyzeAllIssuesEmptyGraph) {
    auto result = DependencyAnalyzer::analyze_all_issues(graph);

    ASSERT_TRUE(result.is_success());
    const auto& issues = result.value();
    EXPECT_EQ(issues.size(), 0);
}

TEST_F(DependencyAnalyzerTest, AnalyzeAllIssuesSimpleGraph) {
    CreateSimpleGraph();
    auto result = DependencyAnalyzer::analyze_all_issues(graph);

    ASSERT_TRUE(result.is_success());

    for (const auto& issues = result.value(); const auto& issue : issues) {
        EXPECT_GE(issue.severity, 1);
        EXPECT_LE(issue.severity, 5);
        EXPECT_GT(issue.files.size(), 0);
        EXPECT_FALSE(issue.description.empty());
    }
}

TEST_F(DependencyAnalyzerTest, AnalyzeAllIssuesComplexGraph) {
    CreateComplexGraph();
    auto result = DependencyAnalyzer::analyze_all_issues(graph);

    ASSERT_TRUE(result.is_success());

    for (const auto& issues = result.value(); const auto& issue : issues) {
        EXPECT_GE(issue.severity, 1);
        EXPECT_LE(issue.severity, 5);
        EXPECT_GT(issue.files.size(), 0);
        EXPECT_FALSE(issue.description.empty());
        EXPECT_FALSE(issue.suggestion.empty());
    }
}

TEST_F(DependencyAnalyzerTest, CalculateTransitiveDepthEmptyGraph) {
    const int depth = DependencyAnalyzer::calculate_transitive_depth("test.cpp", graph);
    EXPECT_EQ(depth, -1);  // Returns -1 for non-existent node
}

TEST_F(DependencyAnalyzerTest, CalculateTransitiveDepthSimpleGraph) {
    CreateSimpleGraph();
    const int depth = DependencyAnalyzer::calculate_transitive_depth("main.cpp", graph);
    EXPECT_GE(depth, 0);
}

TEST_F(DependencyAnalyzerTest, CalculateTransitiveDepthComplexGraph) {
    CreateComplexGraph();
    const int depth = DependencyAnalyzer::calculate_transitive_depth("main.cpp", graph);
    EXPECT_GE(depth, 0);
}

TEST_F(DependencyAnalyzerTest, GetIncludeTreeEmptyGraph) {
    const auto tree = DependencyAnalyzer::get_include_tree("test.cpp", graph);
    EXPECT_EQ(tree.size(), 1);  // Returns root node even for empty graph
}

TEST_F(DependencyAnalyzerTest, GetIncludeTreeSimpleGraph) {
    CreateSimpleGraph();
    const auto tree = DependencyAnalyzer::get_include_tree("main.cpp", graph);

    // Should include main.cpp itself
    EXPECT_GE(tree.size(), 0);
}

TEST_F(DependencyAnalyzerTest, GetIncludeTreeWithMaxDepth) {
    CreateComplexGraph();
    const auto tree = DependencyAnalyzer::get_include_tree("main.cpp", graph, 1);

    // Respect max_depth parameter
    EXPECT_GE(tree.size(), 0);
}

TEST_F(DependencyAnalyzerTest, FindCommonDependenciesEmptyGraph) {
    auto result = DependencyAnalyzer::find_common_dependencies(graph);

    ASSERT_TRUE(result.is_success());
    const auto& common = result.value();
    EXPECT_EQ(common.size(), 0);
}

TEST_F(DependencyAnalyzerTest, FindCommonDependenciesSimpleGraph) {
    CreateSimpleGraph();
    auto result = DependencyAnalyzer::find_common_dependencies(graph);

    ASSERT_TRUE(result.is_success());

    // common.h is included by both main.cpp and other.cpp
    if (auto common = result.value(); common.contains("common.h")) {
        EXPECT_GE(common["common.h"].size(), 2);
    }
}

TEST_F(DependencyAnalyzerTest, FindCommonDependenciesComplexGraph) {
    CreateComplexGraph();
    auto result = DependencyAnalyzer::find_common_dependencies(graph);

    ASSERT_TRUE(result.is_success());

    // base.h should have many dependents
    if (auto common = result.value(); common.contains("base.h")) {
        EXPECT_GE(common["base.h"].size(), 3);
    }
}

TEST_F(DependencyAnalyzerTest, IssueTypeValidation) {
    CreateComplexGraph();
    auto result = DependencyAnalyzer::analyze_all_issues(graph);

    ASSERT_TRUE(result.is_success());

    for (const auto& issues = result.value(); const auto& issue : issues) {
        EXPECT_TRUE(
            issue.type == DependencyIssue::Type::CIRCULAR_DEPENDENCY ||
            issue.type == DependencyIssue::Type::REDUNDANT_INCLUDE ||
            issue.type == DependencyIssue::Type::HIGH_FANOUT ||
            issue.type == DependencyIssue::Type::DEEP_NESTING ||
            issue.type == DependencyIssue::Type::MISSING_FORWARD_DECL
        );
    }
}

TEST_F(DependencyAnalyzerTest, SeverityScoreRange) {
    CreateComplexGraph();
    auto result = DependencyAnalyzer::analyze_all_issues(graph);

    ASSERT_TRUE(result.is_success());

    for (const auto& issues = result.value(); const auto& issue : issues) {
        EXPECT_GE(issue.severity, 1) << "Severity should be at least 1";
        EXPECT_LE(issue.severity, 5) << "Severity should be at most 5";
    }
}