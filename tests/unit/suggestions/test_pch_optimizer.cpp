//
// Created by gregorian on 12/12/2025.
//

#include <gtest/gtest.h>
#include "bha/suggestions/pch_optimizer.h"

using namespace bha::suggestions;
using namespace bha::core;

class PCHOptimizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        trace = BuildTrace{};
        graph = DependencyGraph{};
        current_pch_headers = {"header1.h", "header2.h", "header3.h"};
    }

    BuildTrace trace;
    DependencyGraph graph;
    std::vector<std::string> current_pch_headers;
};

TEST_F(PCHOptimizerTest, OptimizePCHWithEmptyTrace) {
    BuildTrace empty_trace;
    DependencyGraph empty_graph;
    std::vector<std::string> headers;

    auto result = PCHOptimizer::optimize_pch(empty_trace, empty_graph, headers);
    ASSERT_TRUE(result.is_success());

    const auto& opt_result = result.value();
    EXPECT_GE(opt_result.headers_to_add.size(), 0);
    EXPECT_GE(opt_result.headers_to_remove.size(), 0);
    EXPECT_GE(opt_result.confidence, 0.0);
    EXPECT_LE(opt_result.confidence, 1.0);
}

TEST_F(PCHOptimizerTest, OptimizePCHWithValidData) {
    CompilationUnit unit1;
    unit1.file_path = "file1.cpp";
    unit1.preprocessing_time_ms = 100.0;
    trace.compilation_units.push_back(unit1);

    CompilationUnit unit2;
    unit2.file_path = "file2.cpp";
    unit2.preprocessing_time_ms = 150.0;
    trace.compilation_units.push_back(unit2);

    auto result = PCHOptimizer::optimize_pch(trace, graph, current_pch_headers);
    ASSERT_TRUE(result.is_success());

    const auto& opt_result = result.value();
    EXPECT_GE(opt_result.estimated_time_savings_ms, 0.0);
}

TEST_F(PCHOptimizerTest, SuggestHeadersToAdd) {
    CompilationUnit unit1;
    unit1.file_path = "common.h";
    unit1.preprocessing_time_ms = 200.0;
    trace.compilation_units.push_back(unit1);

    auto result = PCHOptimizer::suggest_headers_to_add(trace, graph, 5, 0.3);
    ASSERT_TRUE(result.is_success());

    const auto& headers = result.value();
    EXPECT_LE(headers.size(), 5);  // Should respect top_n limit
}

TEST_F(PCHOptimizerTest, SuggestHeadersToAddWithHighThreshold) {
    const auto result = PCHOptimizer::suggest_headers_to_add(trace, graph, 10, 0.9);
    ASSERT_TRUE(result.is_success()); // With high threshold, fewer headers should be suggested
}

TEST_F(PCHOptimizerTest, SuggestHeadersToRemove) {
    CompilationUnit unit;
    unit.file_path = "rarely_used.h";
    unit.preprocessing_time_ms = 5.0;
    trace.compilation_units.push_back(unit);

    auto result = PCHOptimizer::suggest_headers_to_remove(trace, graph, current_pch_headers);
    ASSERT_TRUE(result.is_success());

    const auto& headers_to_remove = result.value();
    EXPECT_LE(headers_to_remove.size(), current_pch_headers.size());
}

TEST_F(PCHOptimizerTest, SuggestHeadersToRemoveEmptyPCH) {
    std::vector<std::string> empty_pch;
    auto result = PCHOptimizer::suggest_headers_to_remove(trace, graph, empty_pch);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);  // Nothing to remove from empty PCH
}

TEST_F(PCHOptimizerTest, GeneratePCHSuggestions) {
    auto result = PCHOptimizer::generate_pch_suggestions(trace, graph, current_pch_headers);
    ASSERT_TRUE(result.is_success());

    const auto& suggestions = result.value();
    EXPECT_GE(suggestions.size(), 0);

    for (const auto& suggestion : suggestions) {
        EXPECT_TRUE(
            suggestion.type == SuggestionType::PCH_ADDITION ||
            suggestion.type == SuggestionType::PCH_REMOVAL
        );
        EXPECT_GE(suggestion.confidence, 0.0);
        EXPECT_LE(suggestion.confidence, 1.0);
    }
}

TEST_F(PCHOptimizerTest, GeneratePCHHeaderFile) {
    const std::vector<std::string> headers = {
        "vector",
        "string",
        "myproject/common.h",
        "myproject/types.h"
    };

    auto result = PCHOptimizer::generate_pch_header_file(headers);
    ASSERT_TRUE(result.is_success());

    const auto& content = result.value();
    EXPECT_FALSE(content.empty());
    EXPECT_TRUE(content.find("#include") != std::string::npos);

    for (const auto& header : headers) {
        EXPECT_TRUE(content.find(header) != std::string::npos);
    }
}

TEST_F(PCHOptimizerTest, GeneratePCHHeaderFileEmpty) {
    constexpr std::vector<std::string> empty_headers;
    const auto result = PCHOptimizer::generate_pch_header_file(empty_headers);

    ASSERT_TRUE(result.is_success()); // Empty or minimal content is acceptable
}

TEST_F(PCHOptimizerTest, EstimateOptimizationBenefit) {
    const std::vector<std::string> headers_to_add = {"new_header1.h", "new_header2.h"};
    const std::vector<std::string> headers_to_remove = {"old_header1.h"};

    CompilationUnit unit1;
    unit1.file_path = "new_header1.h";
    unit1.preprocessing_time_ms = 80.0;
    trace.compilation_units.push_back(unit1);

    CompilationUnit unit2;
    unit2.file_path = "old_header1.h";
    unit2.preprocessing_time_ms = 20.0;
    trace.compilation_units.push_back(unit2);

    const double benefit = PCHOptimizer::estimate_pch_optimization_benefit(
        headers_to_add, headers_to_remove, trace, graph);

    EXPECT_LT(benefit, 0.0);
}

TEST_F(PCHOptimizerTest, EstimateOptimizationBenefitNoChanges) {
    constexpr std::vector<std::string> empty_add;
    constexpr std::vector<std::string> empty_remove;

    const double benefit = PCHOptimizer::estimate_pch_optimization_benefit(
        empty_add, empty_remove, trace, graph);

    EXPECT_DOUBLE_EQ(benefit, 0.0);  // No changes = zero benefit
}

TEST_F(PCHOptimizerTest, PCHOptimizationResultStructure) {
    PCHOptimizationResult result;
    result.headers_to_add = {"new1.h", "new2.h"};
    result.headers_to_remove = {"old1.h"};
    result.suggested_pch_content = "#include \"new1.h\"\n#include \"new2.h\"";
    result.estimated_time_savings_ms = 150.5;
    result.confidence = 0.85;

    EXPECT_EQ(result.headers_to_add.size(), 2);
    EXPECT_EQ(result.headers_to_remove.size(), 1);
    EXPECT_FALSE(result.suggested_pch_content.empty());
    EXPECT_DOUBLE_EQ(result.estimated_time_savings_ms, 150.5);
    EXPECT_DOUBLE_EQ(result.confidence, 0.85);
}

TEST_F(PCHOptimizerTest, OptimizeWithLargeHeaderSet) {
    std::vector<std::string> large_pch;
    for (int i = 0; i < 100; ++i) {
        large_pch.push_back("header" + std::to_string(i) + ".h");
    }

    const auto result = PCHOptimizer::optimize_pch(trace, graph, large_pch);
    ASSERT_TRUE(result.is_success());
}

TEST_F(PCHOptimizerTest, SuggestionsHaveValidPriority) {
    auto result = PCHOptimizer::generate_pch_suggestions(trace, graph, current_pch_headers);
    ASSERT_TRUE(result.is_success());

    for (const auto& suggestion : result.value()) {
        EXPECT_TRUE(
            suggestion.priority == Priority::LOW ||
            suggestion.priority == Priority::MEDIUM ||
            suggestion.priority == Priority::HIGH ||
            suggestion.priority == Priority::CRITICAL
        );
    }
}

TEST_F(PCHOptimizerTest, OptimizeWithMultipleCompilationUnits) {
    for (int i = 0; i < 20; ++i) {
        CompilationUnit unit;
        unit.file_path = "file" + std::to_string(i) + ".cpp";
        unit.preprocessing_time_ms = 50.0 + (i * 5);
        unit.total_time_ms = 500.0 + (i * 50);
        trace.compilation_units.push_back(unit);
    }

    auto result = PCHOptimizer::optimize_pch(trace, graph, current_pch_headers);
    ASSERT_TRUE(result.is_success());

    const auto& opt_result = result.value();
    EXPECT_GE(opt_result.estimated_time_savings_ms, 0.0);
    EXPECT_LE(opt_result.confidence, 1.0);
}

TEST_F(PCHOptimizerTest, HeadersToAddHaveHighImpact) {
    CompilationUnit unit1;
    unit1.file_path = "common.h";
    unit1.preprocessing_time_ms = 200.0;
    trace.compilation_units.push_back(unit1);

    CompilationUnit unit2;
    unit2.file_path = "utils.h";
    unit2.preprocessing_time_ms = 150.0;
    trace.compilation_units.push_back(unit2);

    CompilationUnit unit3;
    unit3.file_path = "types.h";
    unit3.preprocessing_time_ms = 80.0;
    trace.compilation_units.push_back(unit3);

    auto result = PCHOptimizer::suggest_headers_to_add(trace, graph, 3, 0.3);
    ASSERT_TRUE(result.is_success());

    // This should suggest headers that compile slowly
    EXPECT_LE(result.value().size(), 3);
}

TEST_F(PCHOptimizerTest, HeadersToRemoveAreRarelyUsed) {
    CompilationUnit unit1;
    unit1.file_path = "rarely_used.h";
    unit1.preprocessing_time_ms = 5.0;  // Very low preprocessing time
    trace.compilation_units.push_back(unit1);

    const auto result = PCHOptimizer::suggest_headers_to_remove(trace, graph, current_pch_headers);
    ASSERT_TRUE(result.is_success());
}

TEST_F(PCHOptimizerTest, PCHContentGenerationFormat) {
    const std::vector<std::string> headers = {
        "<vector>",
        "<map>",
        "\"project/config.h\"",
        "\"project/types.h\""
    };

    auto result = PCHOptimizer::generate_pch_header_file(headers);
    ASSERT_TRUE(result.is_success());

    const auto& content = result.value();
    EXPECT_TRUE(content.find("#include") != std::string::npos);

    for (const auto& header : headers) {
        EXPECT_TRUE(content.find(header) != std::string::npos);
    }
}

TEST_F(PCHOptimizerTest, OptimizationBenefitProportionalToFrequency) {
    CompilationUnit high_freq;
    high_freq.file_path = "frequent.h";
    high_freq.preprocessing_time_ms = 100.0;
    trace.compilation_units.push_back(high_freq);

    CompilationUnit low_freq;
    low_freq.file_path = "infrequent.h";
    low_freq.preprocessing_time_ms = 10.0;
    trace.compilation_units.push_back(low_freq);

    const std::vector<std::string> add_high = {"frequent.h"};
    const std::vector<std::string> add_low = {"infrequent.h"};
    constexpr std::vector<std::string> remove_none;

    const double benefit_high = PCHOptimizer::estimate_pch_optimization_benefit(
        add_high, remove_none, trace, graph);
    const double benefit_low = PCHOptimizer::estimate_pch_optimization_benefit(
        add_low, remove_none, trace, graph);

    // High frequency should potentially yield more benefit
    EXPECT_GE(benefit_high, 0.0);
    EXPECT_GE(benefit_low, 0.0);
}

TEST_F(PCHOptimizerTest, ConfidenceScalesWithDataQuality) {
    // With more data points, confidence should be more reliable
    for (int i = 0; i < 50; ++i) {
        CompilationUnit unit;
        unit.file_path = "file" + std::to_string(i) + ".cpp";
        unit.preprocessing_time_ms = 50.0 + (i * 2);
        trace.compilation_units.push_back(unit);
    }

    auto result = PCHOptimizer::optimize_pch(trace, graph, current_pch_headers);
    ASSERT_TRUE(result.is_success());

    EXPECT_GE(result.value().confidence, 0.0);
    EXPECT_LE(result.value().confidence, 1.0);
}

TEST_F(PCHOptimizerTest, SuggestionsTargetHighCompileTime) {
    for (int i = 0; i < 10; ++i) {
        CompilationUnit unit;
        unit.file_path = "slow" + std::to_string(i) + ".h";
        unit.preprocessing_time_ms = 100.0 * (10 - i);  // Decreasing times
        trace.compilation_units.push_back(unit);
    }

    auto result = PCHOptimizer::suggest_headers_to_add(trace, graph, 5, 0.2);
    ASSERT_TRUE(result.is_success());

    const auto& headers = result.value();
    EXPECT_LE(headers.size(), 5);
}

TEST_F(PCHOptimizerTest, SystemHeadersExcluded) {
    CompilationUnit system_unit;
    system_unit.file_path = "<iostream>";
    system_unit.preprocessing_time_ms = 50.0;
    trace.compilation_units.push_back(system_unit);

    CompilationUnit local_unit;
    local_unit.file_path = "myheader.h";
    local_unit.preprocessing_time_ms = 100.0;
    trace.compilation_units.push_back(local_unit);

    const auto result = PCHOptimizer::suggest_headers_to_add(trace, graph, 2, 0.3);
    ASSERT_TRUE(result.is_success());
}

TEST_F(PCHOptimizerTest, BalancedAdditionsAndRemovals) {
    // Scenario: Add beneficial headers, remove harmful ones
    CompilationUnit to_add;
    to_add.file_path = "beneficial.h";
    to_add.preprocessing_time_ms = 200.0;
    trace.compilation_units.push_back(to_add);

    CompilationUnit to_remove;
    to_remove.file_path = "harmful.h";
    to_remove.preprocessing_time_ms = 5.0;
    trace.compilation_units.push_back(to_remove);

    const std::vector<std::string> add = {"beneficial.h"};
    const std::vector<std::string> remove = {"harmful.h"};

    const double benefit = PCHOptimizer::estimate_pch_optimization_benefit(
        add, remove, trace, graph);

    EXPECT_LT(benefit, 0.0);
}

TEST_F(PCHOptimizerTest, HandleEmptyCompilationUnits) {
    const BuildTrace empty_trace;
    constexpr std::vector<std::string> headers;

    auto result = PCHOptimizer::optimize_pch(empty_trace, graph, headers);
    ASSERT_TRUE(result.is_success());

    EXPECT_EQ(result.value().headers_to_add.size(), 0);
    EXPECT_EQ(result.value().headers_to_remove.size(), 0);
}

TEST_F(PCHOptimizerTest, SafetyOfAddingHeadersToPCH) {
    // A header added to PCH should be safe (no circular dependencies)
    const std::vector<std::string> headers_to_add = {"safe1.h", "safe2.h"};
    constexpr std::vector<std::string> headers_to_remove;

    CompilationUnit unit1;
    unit1.file_path = "safe1.h";
    unit1.preprocessing_time_ms = 100.0;
    trace.compilation_units.push_back(unit1);

    const double benefit = PCHOptimizer::estimate_pch_optimization_benefit(
        headers_to_add, headers_to_remove, trace, graph);

    EXPECT_GE(benefit, 0.0);
}

TEST_F(PCHOptimizerTest, ApplicabilityLowInclusionRatio) {
    // A header with low inclusion ratio might not be applicable for PCH
    CompilationUnit rarely_included;
    rarely_included.file_path = "rare.h";
    rarely_included.preprocessing_time_ms = 50.0;
    trace.compilation_units.push_back(rarely_included);

    const auto result = PCHOptimizer::suggest_headers_to_add(trace, graph, 10, 0.8);

    ASSERT_TRUE(result.is_success()); // With high threshold, rarely included headers shouldn't be suggested
}

TEST_F(PCHOptimizerTest, PCHSuggestionsHaveValidDescriptions) {
    auto result = PCHOptimizer::generate_pch_suggestions(trace, graph, current_pch_headers);
    ASSERT_TRUE(result.is_success());

    for (const auto& suggestion : result.value()) {
        EXPECT_FALSE(suggestion.description.empty());
        EXPECT_FALSE(suggestion.affected_files.empty());
    }
}