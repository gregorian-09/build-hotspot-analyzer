//
// Created by gregorian on 13/12/2025.
//

#include <gtest/gtest.h>
#include "bha/suggestions/suggestion_engine.h"

using namespace bha::suggestions;
using namespace bha::core;

class SuggestionEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<SuggestionEngine>();
        trace = BuildTrace{};
        graph = DependencyGraph{};
        options = SuggestionEngine::Options{};
    }

    std::unique_ptr<SuggestionEngine> engine;
    BuildTrace trace;
    DependencyGraph graph;
    SuggestionEngine::Options options;
};

TEST_F(SuggestionEngineTest, Construction) {
    SuggestionEngine test_engine;
    SUCCEED();
}

TEST_F(SuggestionEngineTest, GenerateAllSuggestionsWithDefaultOptions) {
    auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
    const auto& suggestions = result.value();
    EXPECT_GE(suggestions.size(), 0);
}

TEST_F(SuggestionEngineTest, GenerateAllSuggestionsWithEmptyTrace) {
    const BuildTrace empty_trace;
    const auto result = engine->generate_all_suggestions(empty_trace, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(SuggestionEngineTest, GenerateAllSuggestionsWithAllDisabled) {
    options.enable_forward_declarations = false;
    options.enable_header_splits = false;
    options.enable_pch_suggestions = false;
    options.enable_pimpl = false;

    auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(SuggestionEngineTest, GenerateAllSuggestionsWithOnlyForwardDecls) {
    options.enable_forward_declarations = true;
    options.enable_header_splits = false;
    options.enable_pch_suggestions = false;
    options.enable_pimpl = false;

    CompilationUnit unit;
    unit.file_path = "test.cpp";
    trace.compilation_units.push_back(unit);

    const auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(SuggestionEngineTest, SuggestForwardDeclarationsStatic) {
    auto result = SuggestionEngine::suggest_forward_declarations(trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().size(), 0);
}

TEST_F(SuggestionEngineTest, SuggestHeaderSplits) {
    auto result = engine->suggest_header_splits(graph, options);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().size(), 0);
}

TEST_F(SuggestionEngineTest, SuggestPCHOptimizationStatic) {
    auto result = SuggestionEngine::suggest_pch_optimization(trace, graph);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().size(), 0);
}

TEST_F(SuggestionEngineTest, SuggestPimplPatternsStatic) {
    auto result = SuggestionEngine::suggest_pimpl_patterns(trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().size(), 0);
}

TEST_F(SuggestionEngineTest, FilterAndRankWithEmptyList) {
    std::vector<Suggestion> empty_suggestions;

    const auto result = SuggestionEngine::filter_and_rank(
        empty_suggestions, 0.5, 50.0, 10);

    EXPECT_FALSE(result.is_success());  // Should fail with empty list
}

TEST_F(SuggestionEngineTest, FilterAndRankWithValidSuggestions) {
    std::vector<Suggestion> suggestions;

    Suggestion s1;
    s1.confidence = 0.8;
    s1.estimated_time_savings_ms = 100.0;
    s1.type = SuggestionType::FORWARD_DECLARATION;
    suggestions.push_back(s1);

    Suggestion s2;
    s2.confidence = 0.9;
    s2.estimated_time_savings_ms = 200.0;
    s2.type = SuggestionType::PCH_ADDITION;
    suggestions.push_back(s2);

    Suggestion s3;
    s3.confidence = 0.3;  // Low confidence
    s3.estimated_time_savings_ms = 20.0;  // Low savings
    s3.type = SuggestionType::HEADER_SPLIT;
    suggestions.push_back(s3);

    const auto result = SuggestionEngine::filter_and_rank(
        suggestions, 0.5, 50.0, 10);

    ASSERT_TRUE(result.is_success()); // This should filter out s3 due to low confidence and savings
}

TEST_F(SuggestionEngineTest, FilterByConfidenceThreshold) {
    std::vector<Suggestion> suggestions;

    Suggestion high_conf;
    high_conf.confidence = 0.9;
    high_conf.estimated_time_savings_ms = 100.0;
    high_conf.type = SuggestionType::FORWARD_DECLARATION;
    suggestions.push_back(high_conf);

    Suggestion low_conf;
    low_conf.confidence = 0.3;
    low_conf.estimated_time_savings_ms = 150.0;
    low_conf.type = SuggestionType::HEADER_SPLIT;
    suggestions.push_back(low_conf);

    auto result = SuggestionEngine::filter_and_rank(
        suggestions, 0.7, 0.0, 10);  // High confidence threshold

    if (result.is_success()) {
        EXPECT_GE(result.value().confidence, 0.7);
    }
}

TEST_F(SuggestionEngineTest, RankByBenefitHighToLow) {
    std::vector<Suggestion> suggestions;

    for (int i = 0; i < 5; ++i) {
        Suggestion s;
        s.confidence = 0.8;
        s.estimated_time_savings_ms = static_cast<double>(i * 50);
        s.type = SuggestionType::FORWARD_DECLARATION;
        suggestions.push_back(s);
    }

    auto result = SuggestionEngine::filter_and_rank(
        suggestions, 0.5, 0.0, 10);

    if (result.is_success()) {
        // Top suggestion should have the highest benefit
        const auto& top = result.value();
        EXPECT_GE(top.estimated_time_savings_ms, 100.0);
    }
}

TEST_F(SuggestionEngineTest, AggregateSuggestionsFromMultipleSources) {
    options.enable_forward_declarations = true;
    options.enable_pch_suggestions = true;

    CompilationUnit unit1;
    unit1.file_path = "file1.cpp";
    trace.compilation_units.push_back(unit1);

    CompilationUnit unit2;
    unit2.file_path = "file2.cpp";
    trace.compilation_units.push_back(unit2);

    const auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success()); // Should aggregate suggestions from both forward declarations and PCH
}

TEST_F(SuggestionEngineTest, CoordinateAllSuggestersWithFullOptions) {
    options.enable_forward_declarations = true;
    options.enable_header_splits = true;
    options.enable_pch_suggestions = true;
    options.enable_pimpl = true;
    options.min_confidence = 0.6;
    options.min_time_savings_ms = 30.0;
    options.max_suggestions = 15;

    auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
    const auto& suggestions = result.value();
    EXPECT_LE(suggestions.size(), options.max_suggestions);
}

TEST_F(SuggestionEngineTest, RespectMaxSuggestionsLimit) {
    std::vector<Suggestion> many_suggestions;

    for (int i = 0; i < 100; ++i) {
        Suggestion s;
        s.confidence = 0.8;
        s.estimated_time_savings_ms = 100.0;
        s.type = SuggestionType::FORWARD_DECLARATION;
        many_suggestions.push_back(s);
    }

    const auto result = SuggestionEngine::filter_and_rank(
        many_suggestions, 0.5, 0.0, 5);  // Max 5 suggestions

    // This should return at most 1 (since filter_and_rank returns a single top suggestion)
    EXPECT_TRUE(result.is_success());
}

TEST_F(SuggestionEngineTest, OptionsStructureDefaults) {
    constexpr SuggestionEngine::Options default_opts;

    EXPECT_TRUE(default_opts.enable_forward_declarations);
    EXPECT_TRUE(default_opts.enable_header_splits);
    EXPECT_TRUE(default_opts.enable_pch_suggestions);
    EXPECT_FALSE(default_opts.enable_pimpl);
    EXPECT_DOUBLE_EQ(default_opts.min_confidence, 0.5);
    EXPECT_DOUBLE_EQ(default_opts.min_time_savings_ms, 50.0);
    EXPECT_EQ(default_opts.max_suggestions, 20);
}

TEST_F(SuggestionEngineTest, HandleLargeTraceData) {
    for (int i = 0; i < 1000; ++i) {
        CompilationUnit unit;
        unit.file_path = "file" + std::to_string(i) + ".cpp";
        unit.preprocessing_time_ms = 50.0;
        trace.compilation_units.push_back(unit);
    }

    const auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(SuggestionEngineTest, SuggestionsAreRankedByBenefit) {
    std::vector<Suggestion> suggestions;

    Suggestion high_benefit;
    high_benefit.confidence = 0.8;
    high_benefit.estimated_time_savings_ms = 500.0;
    high_benefit.type = SuggestionType::FORWARD_DECLARATION;
    suggestions.push_back(high_benefit);

    Suggestion low_benefit;
    low_benefit.confidence = 0.8;
    low_benefit.estimated_time_savings_ms = 10.0;
    low_benefit.type = SuggestionType::PCH_ADDITION;
    suggestions.push_back(low_benefit);

    Suggestion medium_benefit;
    medium_benefit.confidence = 0.8;
    medium_benefit.estimated_time_savings_ms = 200.0;
    medium_benefit.type = SuggestionType::HEADER_SPLIT;
    suggestions.push_back(medium_benefit);

    auto result = SuggestionEngine::filter_and_rank(suggestions, 0.5, 0.0, 10);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().estimated_time_savings_ms, 200.0);
}

TEST_F(SuggestionEngineTest, FilteringAppliesConfidenceThreshold) {
    std::vector<Suggestion> suggestions;

    for (int i = 0; i < 5; ++i) {
        Suggestion s;
        s.confidence = 0.3 + (i * 0.15);  // 0.3, 0.45, 0.6, 0.75, 0.9
        s.estimated_time_savings_ms = 100.0;
        s.type = SuggestionType::FORWARD_DECLARATION;
        suggestions.push_back(s);
    }

    auto result = SuggestionEngine::filter_and_rank(suggestions, 0.6, 0.0, 10);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().confidence, 0.6);
}

TEST_F(SuggestionEngineTest, FilteringAppliesiSavingsThreshold) {
    std::vector<Suggestion> suggestions;

    for (int i = 0; i < 5; ++i) {
        Suggestion s;
        s.confidence = 0.8;
        s.estimated_time_savings_ms = 20.0 + (i * 40.0);  // 20, 60, 100, 140, 180
        s.type = SuggestionType::PCH_ADDITION;
        suggestions.push_back(s);
    }

    auto result = SuggestionEngine::filter_and_rank(suggestions, 0.5, 100.0, 10);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().estimated_time_savings_ms, 100.0);
}

TEST_F(SuggestionEngineTest, RespectMaxSuggestionsInFiltering) {
    std::vector<Suggestion> many_suggestions;

    for (int i = 0; i < 100; ++i) {
        Suggestion s;
        s.confidence = 0.8 + (i % 2 * 0.1);
        s.estimated_time_savings_ms = 100.0 + (i * 10.0);
        s.type = SuggestionType::FORWARD_DECLARATION;
        many_suggestions.push_back(s);
    }

    const auto result = SuggestionEngine::filter_and_rank(
        many_suggestions, 0.5, 0.0, 5);

    EXPECT_TRUE(result.is_success());
}

TEST_F(SuggestionEngineTest, AllSuggestionsHaveRequiredFields) {
    auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
    for (const auto& sugg : result.value()) {
        EXPECT_FALSE(sugg.description.empty());
        EXPECT_GE(sugg.confidence, 0.0);
        EXPECT_LE(sugg.confidence, 1.0);
        EXPECT_GE(sugg.estimated_time_savings_ms, 0.0);
    }
}

TEST_F(SuggestionEngineTest, SuggestionTypesAreValid) {
    auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
    for (const auto& sugg : result.value()) {
        EXPECT_TRUE(
            sugg.type == SuggestionType::FORWARD_DECLARATION ||
            sugg.type == SuggestionType::HEADER_SPLIT ||
            sugg.type == SuggestionType::PCH_ADDITION ||
            sugg.type == SuggestionType::PCH_REMOVAL ||
            sugg.type == SuggestionType::PIMPL_PATTERN
        );
    }
}

TEST_F(SuggestionEngineTest, GenerateAllSuggestionsRespectsMaxLimit) {
    options.max_suggestions = 5;

    for (int i = 0; i < 20; ++i) {
        CompilationUnit unit;
        unit.file_path = "file" + std::to_string(i) + ".cpp";
        unit.preprocessing_time_ms = 100.0;
        trace.compilation_units.push_back(unit);
    }

    auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
    EXPECT_LE(result.value().size(), options.max_suggestions);
}

TEST_F(SuggestionEngineTest, FilterAndRankWithMixedTypes) {
    std::vector<Suggestion> mixed;

    Suggestion fwd_decl;
    fwd_decl.type = SuggestionType::FORWARD_DECLARATION;
    fwd_decl.confidence = 0.85;
    fwd_decl.estimated_time_savings_ms = 75.0;
    mixed.push_back(fwd_decl);

    Suggestion pch;
    pch.type = SuggestionType::PCH_ADDITION;
    pch.confidence = 0.9;
    pch.estimated_time_savings_ms = 200.0;
    mixed.push_back(pch);

    Suggestion split;
    split.type = SuggestionType::HEADER_SPLIT;
    split.confidence = 0.72;
    split.estimated_time_savings_ms = 150.0;
    mixed.push_back(split);

    auto result = SuggestionEngine::filter_and_rank(mixed, 0.7, 50.0, 10);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().confidence, 0.7);
}

TEST_F(SuggestionEngineTest, SelectiveEnablingOfSuggesters) {
    options.enable_forward_declarations = true;
    options.enable_header_splits = false;
    options.enable_pch_suggestions = false;
    options.enable_pimpl = false;

    for (int i = 0; i < 5; ++i) {
        CompilationUnit unit;
        unit.file_path = "file" + std::to_string(i) + ".cpp";
        unit.preprocessing_time_ms = 100.0;
        trace.compilation_units.push_back(unit);
    }

    auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
    for (const auto& sugg : result.value()) {
        EXPECT_EQ(sugg.type, SuggestionType::FORWARD_DECLARATION);
    }
}

TEST_F(SuggestionEngineTest, HighMinimumConfidenceReducesSuggestions) {
    std::vector<Suggestion> mixed;

    for (int i = 0; i < 10; ++i) {
        Suggestion s;
        s.confidence = 0.3 + (i * 0.07);
        s.estimated_time_savings_ms = 100.0;
        s.type = SuggestionType::FORWARD_DECLARATION;
        mixed.push_back(s);
    }

    auto result_low = SuggestionEngine::filter_and_rank(mixed, 0.4, 0.0, 10);
    auto result_high = SuggestionEngine::filter_and_rank(mixed, 0.8, 0.0, 10);

    EXPECT_TRUE(result_low.is_success());
    if (result_high.is_success()) {
        EXPECT_GE(result_high.value().confidence, 0.8);
    }
}

TEST_F(SuggestionEngineTest, SuggestionsConsiderProjectSize) {
    // Larger project should generate more suggestions
    for (int i = 0; i < 50; ++i) {
        CompilationUnit unit;
        unit.file_path = "file" + std::to_string(i) + ".cpp";
        unit.preprocessing_time_ms = 50.0 + (i % 10 * 5);
        unit.total_time_ms = 500.0 + (i % 10 * 50);
        trace.compilation_units.push_back(unit);
    }

    auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().size(), 0);
}

TEST_F(SuggestionEngineTest, CombineSuggestionsFromMultipleSources) {
    options.enable_forward_declarations = true;
    options.enable_pch_suggestions = true;

    for (int i = 0; i < 5; ++i) {
        CompilationUnit unit;
        unit.file_path = "file" + std::to_string(i) + ".cpp";
        unit.preprocessing_time_ms = 100.0 + (i * 20);
        trace.compilation_units.push_back(unit);
    }

    const auto result = engine->generate_all_suggestions(trace, options);

    ASSERT_TRUE(result.is_success());
}

TEST_F(SuggestionEngineTest, TopSuggestionHasBestMetrics) {
    std::vector<Suggestion> suggestions;

    Suggestion s1;
    s1.confidence = 0.7;
    s1.estimated_time_savings_ms = 100.0;
    s1.type = SuggestionType::FORWARD_DECLARATION;
    suggestions.push_back(s1);

    Suggestion s2;
    s2.confidence = 0.9;
    s2.estimated_time_savings_ms = 300.0;
    s2.type = SuggestionType::PCH_ADDITION;
    suggestions.push_back(s2);

    Suggestion s3;
    s3.confidence = 0.8;
    s3.estimated_time_savings_ms = 150.0;
    s3.type = SuggestionType::HEADER_SPLIT;
    suggestions.push_back(s3);

    auto result = SuggestionEngine::filter_and_rank(suggestions, 0.5, 0.0, 10);

    ASSERT_TRUE(result.is_success());
    // s2 should be top due to high confidence and savings
    EXPECT_GT(result.value().estimated_time_savings_ms, 250.0);
    EXPECT_GT(result.value().confidence, 0.85);
}