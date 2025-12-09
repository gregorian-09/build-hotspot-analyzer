//
// Created by gregorian on 09/12/2025.
//

#include <gtest/gtest.h>
#include "bha/analysis/hotspot_analyzer.h"

using namespace bha::analysis;
using namespace bha::core;

class HotspotAnalyzerTest : public ::testing::Test {
protected:
    void SetUp() override {
        trace = BuildTrace{};
        graph = DependencyGraph{};
        options = HotspotAnalyzer::Options{};
    }

    BuildTrace trace;
    DependencyGraph graph;
    HotspotAnalyzer::Options options;

    void CreateSimpleTrace() {
        for (int i = 0; i < 5; ++i) {
            CompilationUnit unit;
            unit.file_path = "file" + std::to_string(i) + ".cpp";
            unit.total_time_ms = static_cast<double>(i * 1000);
            unit.preprocessing_time_ms = i * 50;
            unit.parsing_time_ms = i * 100;
            unit.codegen_time_ms = i * 200;
            trace.compilation_units.push_back(unit);
        }
        trace.total_build_time_ms = 10000.0;
    }

    void CreateComplexTrace() {
        std::vector<std::pair<std::string, double>> files = {
            {"main.cpp", 5000.0},
            {"module1.cpp", 3500.0},
            {"module2.cpp", 2800.0},
            {"module3.cpp", 2200.0},
            {"module4.cpp", 1800.0},
            {"utils.cpp", 1200.0},
            {"base.cpp", 900.0},
            {"helper.cpp", 500.0},
            {"common.h", 150.0},
            {"config.h", 80.0}
        };

        for (const auto& [path, time] : files) {
            CompilationUnit unit;
            unit.file_path = path;
            unit.total_time_ms = time;
            unit.preprocessing_time_ms = time * 0.1;
            unit.parsing_time_ms = time * 0.2;
            unit.codegen_time_ms = time * 0.4;
            trace.compilation_units.push_back(unit);
        }

        trace.total_build_time_ms = 18230.0;
    }

    void CreateComplexGraph() {
        graph.add_node("common.h");
        graph.add_node("config.h");
        graph.add_node("main.cpp");
        graph.add_node("module1.cpp");
        graph.add_node("module2.cpp");

        graph.add_edge("main.cpp", "common.h");
        graph.add_edge("main.cpp", "config.h");
        graph.add_edge("module1.cpp", "common.h");
        graph.add_edge("module2.cpp", "common.h");
    }
};

TEST_F(HotspotAnalyzerTest, IdentifyHotspotsWithEmptyTrace) {
    auto result = HotspotAnalyzer::identify_hotspots(trace, options);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(HotspotAnalyzerTest, IdentifyHotspotsWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = HotspotAnalyzer::identify_hotspots(trace, options);

    ASSERT_TRUE(result.is_success());
    const auto& hotspots = result.value();
    EXPECT_GE(hotspots.size(), 0);
    EXPECT_LE(hotspots.size(), static_cast<size_t>(options.top_n));

    for (const auto& hotspot : hotspots) {
        EXPECT_FALSE(hotspot.file_path.empty());
        EXPECT_GE(hotspot.time_ms, 0.0);
        EXPECT_GE(hotspot.impact_score, 0.0);
    }
}

TEST_F(HotspotAnalyzerTest, IdentifyHotspotsWithComplexTrace) {
    CreateComplexTrace();
    options.top_n = 10;
    auto result = HotspotAnalyzer::identify_hotspots(trace, options);

    ASSERT_TRUE(result.is_success());
    const auto& hotspots = result.value();
    EXPECT_GT(hotspots.size(), 0);
    EXPECT_LE(hotspots.size(), static_cast<size_t>(options.top_n));

    // Hotspots should be sorted by impact
    for (size_t i = 0; i < hotspots.size() - 1; ++i) {
        EXPECT_GE(hotspots[i].time_ms, hotspots[i + 1].time_ms);
    }
}

TEST_F(HotspotAnalyzerTest, FindSlowFilesWithEmptyTrace) {
    auto result = HotspotAnalyzer::find_slow_files(trace, 5, 1000.0);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(HotspotAnalyzerTest, FindSlowFilesWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = HotspotAnalyzer::find_slow_files(trace, 5, 1000.0);

    ASSERT_TRUE(result.is_success());
    const auto& slow_files = result.value();
    EXPECT_LE(slow_files.size(), 5);

    for (const auto& hotspot : slow_files) {
        EXPECT_GE(hotspot.time_ms, 1000.0);
        EXPECT_FALSE(hotspot.file_path.empty());
    }
}

TEST_F(HotspotAnalyzerTest, FindSlowFilesWithComplexTrace) {
    CreateComplexTrace();
    auto result = HotspotAnalyzer::find_slow_files(trace, 5, 2000.0);

    ASSERT_TRUE(result.is_success());
    const auto& slow_files = result.value();
    EXPECT_LE(slow_files.size(), 5);

    // All returned files should exceed threshold
    for (const auto& hotspot : slow_files) {
        EXPECT_GE(hotspot.time_ms, 2000.0);
    }
}

TEST_F(HotspotAnalyzerTest, FindSlowFilesWithZeroThreshold) {
    CreateComplexTrace();
    auto result = HotspotAnalyzer::find_slow_files(trace, 100, 0.0);

    ASSERT_TRUE(result.is_success());
    const auto& slow_files = result.value();
    EXPECT_GT(slow_files.size(), 0);
}

TEST_F(HotspotAnalyzerTest, FindHotHeadersWithEmptyTrace) {
    auto result = HotspotAnalyzer::find_hot_headers(trace, graph, 10);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(HotspotAnalyzerTest, FindHotHeadersWithComplexData) {
    CreateComplexTrace();
    CreateComplexGraph();

    auto result = HotspotAnalyzer::find_hot_headers(trace, graph, 10);

    ASSERT_TRUE(result.is_success());
    const auto& hot_headers = result.value();
    EXPECT_LE(hot_headers.size(), 10);

    for (const auto& hotspot : hot_headers) {
        EXPECT_GE(hotspot.time_ms, 0.0);
        EXPECT_FALSE(hotspot.file_path.empty());
    }
}

TEST_F(HotspotAnalyzerTest, CalculateImpactScoresWithEmptyTrace) {
    const auto scores = HotspotAnalyzer::calculate_all_impact_scores(trace, graph);

    EXPECT_EQ(scores.size(), 0);
}

TEST_F(HotspotAnalyzerTest, CalculateImpactScoresWithSimpleTrace) {
    CreateSimpleTrace();
    auto scores = HotspotAnalyzer::calculate_all_impact_scores(trace, graph);

    EXPECT_EQ(scores.size(), 5);
    for (const auto& [file, score] : scores) {
        EXPECT_GE(score, 0.0);
        EXPECT_FALSE(file.empty());
    }
}

TEST_F(HotspotAnalyzerTest, CalculateImpactScoresWithComplexData) {
    CreateComplexTrace();
    CreateComplexGraph();
    auto scores = HotspotAnalyzer::calculate_all_impact_scores(trace, graph);

    EXPECT_GT(scores.size(), 0);
    for (const auto& [file, score] : scores) {
        EXPECT_GE(score, 0.0);
        EXPECT_FALSE(file.empty());
    }
}

TEST_F(HotspotAnalyzerTest, FindCriticalPathWithEmptyData) {
    auto result = HotspotAnalyzer::find_critical_path(trace, graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(HotspotAnalyzerTest, FindCriticalPathWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = HotspotAnalyzer::find_critical_path(trace, graph);

    ASSERT_TRUE(result.is_success());
    const auto& critical = result.value();
    EXPECT_GE(critical.size(), 0);

    for (const auto& hotspot : critical) {
        EXPECT_GE(hotspot.time_ms, 0.0);
    }
}

TEST_F(HotspotAnalyzerTest, FindCriticalPathWithComplexData) {
    CreateComplexTrace();
    CreateComplexGraph();
    auto result = HotspotAnalyzer::find_critical_path(trace, graph);

    ASSERT_TRUE(result.is_success());
    const auto& critical = result.value();
    EXPECT_GE(critical.size(), 0);

    // Critical path should be ordered by time
    for (size_t i = 0; i < critical.size() - 1; ++i) {
        EXPECT_GE(critical[i].time_ms, critical[i + 1].time_ms);
    }
}

TEST_F(HotspotAnalyzerTest, RankByMetricWithEmptyList) {
    constexpr std::vector<Hotspot> hotspots;
    auto result = HotspotAnalyzer::rank_by_metric(hotspots, "absolute_time");

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(HotspotAnalyzerTest, RankByMetricAbsoluteTime) {
    CreateSimpleTrace();
    auto result = HotspotAnalyzer::identify_hotspots(trace, options);
    ASSERT_TRUE(result.is_success());

    auto ranked = HotspotAnalyzer::rank_by_metric(result.value(), "absolute_time");
    ASSERT_TRUE(ranked.is_success());

    const auto& hotspots = ranked.value();
    for (size_t i = 0; i < hotspots.size() - 1; ++i) {
        EXPECT_GE(hotspots[i].time_ms, hotspots[i + 1].time_ms);
    }
}

TEST_F(HotspotAnalyzerTest, RankByMetricImpactScore) {
    CreateSimpleTrace();
    auto result = HotspotAnalyzer::identify_hotspots(trace, options);
    ASSERT_TRUE(result.is_success());

    auto ranked = HotspotAnalyzer::rank_by_metric(result.value(), "impact_score");
    ASSERT_TRUE(ranked.is_success());

    const auto& hotspots = ranked.value();
    // Should be sorted by impact score
    for (size_t i = 0; i < hotspots.size() - 1; ++i) {
        EXPECT_GE(hotspots[i].impact_score, hotspots[i + 1].impact_score);
    }
}

TEST_F(HotspotAnalyzerTest, CalculateImpactScoreForSingleFile) {
    CreateComplexTrace();
    CreateComplexGraph();

    const double score = HotspotAnalyzer::calculate_impact_score("main.cpp", graph, trace);
    EXPECT_GE(score, 0.0);
}

TEST_F(HotspotAnalyzerTest, CalculateImpactScoreForHeaderFile) {
    CreateComplexTrace();
    CreateComplexGraph();

    const double score = HotspotAnalyzer::calculate_impact_score("common.h", graph, trace);
    EXPECT_GE(score, 0.0);
}

TEST_F(HotspotAnalyzerTest, CalculateImpactScoreForNonexistentFile) {
    CreateComplexTrace();
    CreateComplexGraph();

    const double score = HotspotAnalyzer::calculate_impact_score("nonexistent.cpp", graph, trace);
    EXPECT_GE(score, 0.0);
}

TEST_F(HotspotAnalyzerTest, HotspotStructureValidation) {
    CreateComplexTrace();
    auto result = HotspotAnalyzer::identify_hotspots(trace, options);

    ASSERT_TRUE(result.is_success());

    for (auto hotspots = result.value(); const auto& [file_path, time_ms, impact_score, num_dependent_files, category] : hotspots) {
        EXPECT_FALSE(file_path.empty());
        EXPECT_GE(time_ms, 0.0);
        EXPECT_GE(impact_score, 0.0);
        EXPECT_GE(num_dependent_files, 0);
        EXPECT_FALSE(category.empty());
    }
}

TEST_F(HotspotAnalyzerTest, OptionsStructureValidation) {
    HotspotAnalyzer::Options opts;
    opts.top_n = 15;
    opts.threshold_ms = 500.0;
    opts.include_headers = false;

    EXPECT_EQ(opts.top_n, 15);
    EXPECT_DOUBLE_EQ(opts.threshold_ms, 500.0);
    EXPECT_FALSE(opts.include_headers);
}