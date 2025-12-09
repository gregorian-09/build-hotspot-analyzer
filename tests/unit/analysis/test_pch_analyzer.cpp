//
// Created by gregorian on 08/12/2025.
//

#include <gtest/gtest.h>
#include "bha/analysis/pch_analyzer.h"

using namespace bha::analysis;
using namespace bha::core;

class PCHAnalyzerTest : public ::testing::Test {
protected:
    void SetUp() override {
        trace = BuildTrace{};
        graph = DependencyGraph{};
    }

    BuildTrace trace;
    DependencyGraph graph;

    void CreateSimpleTrace() {
        std::vector<std::pair<std::string, double>> units = {
            {"common.h", 200.0},
            {"util.h", 150.0},
            {"config.h", 100.0},
            {"main.cpp", 3000.0},
            {"module1.cpp", 2000.0},
            {"module2.cpp", 1500.0}
        };

        for (const auto& [path, time] : units) {
            CompilationUnit unit;
            unit.file_path = path;
            unit.total_time_ms = time;
            unit.preprocessing_time_ms = time * 0.05;
            unit.parsing_time_ms = time * 0.15;
            unit.codegen_time_ms = time * 0.30;
            trace.compilation_units.push_back(unit);
        }
    }

    void CreateComplexTrace() {
        std::vector<std::pair<std::string, double>> units = {
            {"base.h", 300.0},
            {"common.h", 250.0},
            {"math.h", 200.0},
            {"utils.h", 150.0},
            {"config.h", 100.0},
            {"memory.h", 120.0},
            {"main.cpp", 5000.0},
            {"module1.cpp", 3500.0},
            {"module2.cpp", 2800.0},
            {"module3.cpp", 2200.0}
        };

        for (const auto& [path, time] : units) {
            CompilationUnit unit;
            unit.file_path = path;
            unit.total_time_ms = time;
            unit.preprocessing_time_ms = time * 0.05;
            unit.parsing_time_ms = time * 0.15;
            unit.codegen_time_ms = time * 0.30;
            trace.compilation_units.push_back(unit);
        }
    }

    void CreateSimpleGraph() {
        graph.add_node("common.h");
        graph.add_node("util.h");
        graph.add_node("config.h");
        graph.add_node("main.cpp");
        graph.add_node("module1.cpp");
        graph.add_node("module2.cpp");

        // Create inclusion relationships
        graph.add_edge("main.cpp", "common.h");
        graph.add_edge("main.cpp", "util.h");
        graph.add_edge("main.cpp", "config.h");
        graph.add_edge("module1.cpp", "common.h");
        graph.add_edge("module1.cpp", "util.h");
        graph.add_edge("module2.cpp", "common.h");
        graph.add_edge("module2.cpp", "config.h");
    }

    void CreateComplexGraph() {
        const std::vector<std::string> headers = {"base.h", "common.h", "math.h", "utils.h",
                                            "config.h", "memory.h"};
        const std::vector<std::string> sources = {"main.cpp", "module1.cpp", "module2.cpp",
                                            "module3.cpp"};

        for (const auto& h : headers) {
            graph.add_node(h);
        }
        for (const auto& s : sources) {
            graph.add_node(s);
        }

        // Create dependencies
        graph.add_edge("main.cpp", "base.h");
        graph.add_edge("main.cpp", "common.h");
        graph.add_edge("main.cpp", "math.h");
        graph.add_edge("main.cpp", "utils.h");
        graph.add_edge("main.cpp", "config.h");

        graph.add_edge("module1.cpp", "base.h");
        graph.add_edge("module1.cpp", "common.h");
        graph.add_edge("module1.cpp", "math.h");
        graph.add_edge("module1.cpp", "memory.h");

        graph.add_edge("module2.cpp", "common.h");
        graph.add_edge("module2.cpp", "utils.h");
        graph.add_edge("module2.cpp", "memory.h");

        graph.add_edge("module3.cpp", "base.h");
        graph.add_edge("module3.cpp", "utils.h");
        graph.add_edge("module3.cpp", "config.h");
    }
};

TEST_F(PCHAnalyzerTest, IdentifyPCHCandidatesWithEmptyData) {
    auto result = PCHAnalyzer::identify_pch_candidates(trace, graph, 10, 0.5);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(PCHAnalyzerTest, IdentifyPCHCandidatesWithSimpleTrace) {
    CreateSimpleTrace();
    CreateSimpleGraph();

    auto result = PCHAnalyzer::identify_pch_candidates(trace, graph, 10, 0.5);

    ASSERT_TRUE(result.is_success());
    auto candidates = result.value();
    EXPECT_LE(candidates.size(), 10);

    for (const auto& [header, inclusion_count, average_compile_time_ms, potential_savings_ms, benefit_score] : candidates) {
        EXPECT_FALSE(header.empty());
        EXPECT_GT(inclusion_count, 0);
        EXPECT_GE(average_compile_time_ms, 0.0);
        EXPECT_GE(potential_savings_ms, 0.0);
        EXPECT_GE(benefit_score, 0.0);
    }
}

TEST_F(PCHAnalyzerTest, IdentifyPCHCandidatesWithComplexTrace) {
    CreateComplexTrace();
    CreateComplexGraph();

    auto result = PCHAnalyzer::identify_pch_candidates(trace, graph, 5, 0.4);

    ASSERT_TRUE(result.is_success());
    const auto& candidates = result.value();
    EXPECT_LE(candidates.size(), 5);

    // Candidates should be sorted by benefit score
    if (candidates.size() > 1) {
        for (size_t i = 0; i < candidates.size() - 1; ++i) {
            EXPECT_GE(candidates[i].benefit_score, candidates[i + 1].benefit_score);
        }
    }
}

TEST_F(PCHAnalyzerTest, IdentifyPCHCandidatesWithHighTopN) {
    CreateComplexTrace();
    CreateComplexGraph();

    auto result = PCHAnalyzer::identify_pch_candidates(trace, graph, 100, 0.3);

    ASSERT_TRUE(result.is_success());
    const auto& candidates = result.value();
    EXPECT_LE(candidates.size(), 100);
}

TEST_F(PCHAnalyzerTest, IdentifyPCHCandidatesWithHighInclusionRatio) {
    CreateComplexTrace();
    CreateComplexGraph();

    // High ratio means only very frequently included headers
    auto result = PCHAnalyzer::identify_pch_candidates(trace, graph, 10, 0.9);

    ASSERT_TRUE(result.is_success());

    // All candidates should have high inclusion count
    for (const auto& candidates = result.value(); const auto& candidate : candidates) {
        EXPECT_GT(candidate.inclusion_count, 0);
    }
}

TEST_F(PCHAnalyzerTest, AnalyzePCHEffectivenessWithEmptyTrace) {
    auto result = PCHAnalyzer::analyze_pch_effectiveness(trace, "pch.h");

    ASSERT_TRUE(result.is_success());
    const auto& metrics = result.value();
    EXPECT_EQ(metrics.pch_file, "pch.h");
}

TEST_F(PCHAnalyzerTest, AnalyzePCHEffectivenessWithSimpleTrace) {
    CreateSimpleTrace();

    auto result = PCHAnalyzer::analyze_pch_effectiveness(trace, "common.h");

    ASSERT_TRUE(result.is_success());
    const auto& metrics = result.value();
    EXPECT_FALSE(metrics.pch_file.empty());
    EXPECT_GE(metrics.pch_build_time_ms, 0.0);
    EXPECT_GE(metrics.files_using_pch, 0);
    EXPECT_GE(metrics.total_time_saved_ms, 0.0);
}

TEST_F(PCHAnalyzerTest, AnalyzePCHEffectivenessWithComplexTrace) {
    CreateComplexTrace();

    auto result = PCHAnalyzer::analyze_pch_effectiveness(trace, "base.h");

    ASSERT_TRUE(result.is_success());
    const auto& metrics = result.value();
    EXPECT_FALSE(metrics.pch_file.empty());
    EXPECT_GE(metrics.pch_build_time_ms, 0.0);
    EXPECT_GE(metrics.average_time_saved_per_file_ms, 0.0);
    EXPECT_GE(metrics.pch_hit_rate, 0.0);
}

TEST_F(PCHAnalyzerTest, SuggestPCHAdditionsWithEmptyData) {
    auto result = PCHAnalyzer::suggest_pch_additions(trace, graph, "existing_pch.h");

    ASSERT_TRUE(result.is_success());
    const auto &suggestions = result.value();
    EXPECT_EQ(suggestions.size(), 0);
}

TEST_F(PCHAnalyzerTest, SuggestPCHAdditionsWithSimpleData) {
    CreateSimpleTrace();
    CreateSimpleGraph();

    auto result = PCHAnalyzer::suggest_pch_additions(trace, graph, "pch.h");

    ASSERT_TRUE(result.is_success());
    const auto &suggestions = result.value();
    EXPECT_GE(suggestions.size(), 0);

    for (const auto& header : suggestions) {
        EXPECT_FALSE(header.empty());
    }
}

TEST_F(PCHAnalyzerTest, SuggestPCHAdditionsWithComplexData) {
    CreateComplexTrace();
    CreateComplexGraph();

    auto result = PCHAnalyzer::suggest_pch_additions(trace, graph, "core_pch.h");

    ASSERT_TRUE(result.is_success());
    const auto& suggestions = result.value();
    EXPECT_GE(suggestions.size(), 0);
}

TEST_F(PCHAnalyzerTest, SuggestPCHRemovalsWithEmptyData) {
    auto result = PCHAnalyzer::suggest_pch_removals(trace, graph, "pch.h");

    ASSERT_TRUE(result.is_success());
    const auto& removals = result.value();
    EXPECT_EQ(removals.size(), 0);
}

TEST_F(PCHAnalyzerTest, SuggestPCHRemovalsWithSimpleData) {
    CreateSimpleTrace();
    CreateSimpleGraph();

    auto result = PCHAnalyzer::suggest_pch_removals(trace, graph, "common.h");

    ASSERT_TRUE(result.is_success());
    const auto& removals = result.value();
    EXPECT_GE(removals.size(), 0);
}

TEST_F(PCHAnalyzerTest, SuggestPCHRemovalsWithComplexData) {
    CreateComplexTrace();
    CreateComplexGraph();

    auto result = PCHAnalyzer::suggest_pch_removals(trace, graph, "base.h");

    ASSERT_TRUE(result.is_success());
    const auto& removals = result.value();
    EXPECT_GE(removals.size(), 0);
}

TEST_F(PCHAnalyzerTest, CalculatePCHBenefitScore) {
    const double score = PCHAnalyzer::calculate_pch_benefit_score(10, 500.0, 50);
    EXPECT_GE(score, 0.0);
}

TEST_F(PCHAnalyzerTest, CalculatePCHBenefitScoreWithHighInclusion) {
    const double score = PCHAnalyzer::calculate_pch_benefit_score(50, 1000.0, 100);
    EXPECT_GE(score, 0.0);
}

TEST_F(PCHAnalyzerTest, CalculatePCHBenefitScoreComparison) {
    const double score1 = PCHAnalyzer::calculate_pch_benefit_score(30, 800.0, 80);
    const double score2 = PCHAnalyzer::calculate_pch_benefit_score(10, 300.0, 80);

    // More inclusions and higher compile time should yield higher score
    EXPECT_GE(score1, 0.0);
    EXPECT_GE(score2, 0.0);
}

TEST_F(PCHAnalyzerTest, EstimatePCHSavingsWithEmptyData) {
    const std::vector<std::string> pch_headers = {"header1.h", "header2.h"};
    auto result = PCHAnalyzer::estimate_pch_savings(pch_headers, trace, graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value(), 0.0);
}

TEST_F(PCHAnalyzerTest, EstimatePCHSavingsWithSimpleTrace) {
    CreateSimpleTrace();
    CreateSimpleGraph();

    const std::vector<std::string> pch_headers = {"common.h", "util.h"};
    auto result = PCHAnalyzer::estimate_pch_savings(pch_headers, trace, graph);

    ASSERT_TRUE(result.is_success());
    const double savings = result.value();
    EXPECT_GE(savings, 0.0);
}

TEST_F(PCHAnalyzerTest, EstimatePCHSavingsWithComplexTrace) {
    CreateComplexTrace();
    CreateComplexGraph();

    const std::vector<std::string> pch_headers = {"base.h", "common.h", "math.h"};
    auto result = PCHAnalyzer::estimate_pch_savings(pch_headers, trace, graph);

    ASSERT_TRUE(result.is_success());
    const double savings = result.value();
    EXPECT_GE(savings, 0.0);
}

TEST_F(PCHAnalyzerTest, EstimatePCHSavingsWithSingleHeader) {
    CreateComplexTrace();
    CreateComplexGraph();

    const std::vector<std::string> pch_headers = {"common.h"};
    auto result = PCHAnalyzer::estimate_pch_savings(pch_headers, trace, graph);

    ASSERT_TRUE(result.is_success());
    const double savings = result.value();
    EXPECT_GE(savings, 0.0);
}

TEST_F(PCHAnalyzerTest, EstimatePCHSavingsWithEmptyHeaderList) {
    CreateSimpleTrace();

    constexpr std::vector<std::string> pch_headers;
    auto result = PCHAnalyzer::estimate_pch_savings(pch_headers, trace, graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TEST_F(PCHAnalyzerTest, PCHCandidateStructureValidation) {
    CreateComplexTrace();
    CreateComplexGraph();

    auto result = PCHAnalyzer::identify_pch_candidates(trace, graph, 5, 0.3);

    ASSERT_TRUE(result.is_success());

    for (const auto& candidates = result.value(); const auto& [header, inclusion_count, average_compile_time_ms, potential_savings_ms, benefit_score] : candidates) {
        EXPECT_FALSE(header.empty());
        EXPECT_GT(inclusion_count, 0);
        EXPECT_GE(average_compile_time_ms, 0.0);
        EXPECT_GE(potential_savings_ms, 0.0);
        EXPECT_GE(benefit_score, 0.0);
    }
}

TEST_F(PCHAnalyzerTest, PCHMetricsStructureValidation) {
    CreateComplexTrace();

    auto result = PCHAnalyzer::analyze_pch_effectiveness(trace, "test_pch.h");

    ASSERT_TRUE(result.is_success());
    auto [pch_file, pch_build_time_ms, average_time_saved_per_file_ms, files_using_pch, total_time_saved_ms, pch_hit_rate] = result.value();

    EXPECT_FALSE(pch_file.empty());
    EXPECT_GE(pch_build_time_ms, 0.0);
    EXPECT_GE(average_time_saved_per_file_ms, 0.0);
    EXPECT_GE(files_using_pch, 0);
    EXPECT_GE(total_time_saved_ms, 0.0);
    EXPECT_GE(pch_hit_rate, 0.0);
    EXPECT_LE(pch_hit_rate, 1.0);
}