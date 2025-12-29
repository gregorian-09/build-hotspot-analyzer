//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/header_split_suggester.hpp"

#include <gtest/gtest.h>

namespace bha::suggestions
{
    class HeaderSplitSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<HeaderSplitSuggester>();
        }

        std::unique_ptr<HeaderSplitSuggester> suggester_;
    };

    TEST_F(HeaderSplitSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "HeaderSplitSuggester");
    }

    TEST_F(HeaderSplitSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(HeaderSplitSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::HeaderSplit);
    }

    TEST_F(HeaderSplitSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(HeaderSplitSuggesterTest, SuggestsForLargeHeader) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "big_header.h";
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 30;
        header.including_files = 15;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().suggestions.size(), 1u);

        if (!result.value().suggestions.empty()) {
            const auto& suggestion = result.value().suggestions[0];
            EXPECT_EQ(suggestion.type, SuggestionType::HeaderSplit);
            EXPECT_FALSE(suggestion.is_safe);
            EXPECT_GT(suggestion.estimated_savings.count(), 0);
            EXPECT_FALSE(suggestion.implementation_steps.empty());
        }
    }

    TEST_F(HeaderSplitSuggesterTest, SkipsSmallHeaders) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "small_header.h";
        header.total_parse_time = std::chrono::milliseconds(50);
        header.inclusion_count = 10;
        header.including_files = 5;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(HeaderSplitSuggesterTest, SkipsRarelyIncludedHeaders) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "rare_header.h";
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 2;
        header.including_files = 2;
        analysis.dependencies.headers.push_back(header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(HeaderSplitSuggesterTest, SkipsAlreadySplitHeaders) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;

        analyzers::DependencyAnalysisResult::HeaderInfo fwd_header;
        fwd_header.path = "types_fwd.h";
        fwd_header.total_parse_time = std::chrono::milliseconds(500);
        fwd_header.including_files = 20;
        analysis.dependencies.headers.push_back(fwd_header);

        analyzers::DependencyAnalysisResult::HeaderInfo types_header;
        types_header.path = "widget_types.h";
        types_header.total_parse_time = std::chrono::milliseconds(500);
        types_header.including_files = 20;
        analysis.dependencies.headers.push_back(types_header);

        analyzers::DependencyAnalysisResult::HeaderInfo decl_header;
        decl_header.path = "api_decl.h";
        decl_header.total_parse_time = std::chrono::milliseconds(500);
        decl_header.including_files = 20;
        analysis.dependencies.headers.push_back(decl_header);

        analyzers::DependencyAnalysisResult::HeaderInfo impl_header;
        impl_header.path = "core_impl.h";
        impl_header.total_parse_time = std::chrono::milliseconds(500);
        impl_header.including_files = 20;
        analysis.dependencies.headers.push_back(impl_header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_skipped, 4u);
    }

    TEST_F(HeaderSplitSuggesterTest, SkipsNonHeaders) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo source;
        source.path = "source.cpp";
        source.total_parse_time = std::chrono::milliseconds(500);
        source.including_files = 20;
        analysis.dependencies.headers.push_back(source);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(HeaderSplitSuggesterTest, PrioritizesByEstimatedSavings) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);

        analyzers::AnalysisResult analysis;

        analyzers::DependencyAnalysisResult::HeaderInfo small_header;
        small_header.path = "small.h";
        small_header.total_parse_time = std::chrono::milliseconds(300);
        small_header.including_files = 10;
        analysis.dependencies.headers.push_back(small_header);

        analyzers::DependencyAnalysisResult::HeaderInfo big_header;
        big_header.path = "big.h";
        big_header.total_parse_time = std::chrono::milliseconds(600);
        big_header.including_files = 30;
        analysis.dependencies.headers.push_back(big_header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_GE(result.value().suggestions.size(), 2u);

        EXPECT_TRUE(result.value().suggestions[0].estimated_savings >=
                  result.value().suggestions[1].estimated_savings);
    }

    TEST_F(HeaderSplitSuggesterTest, CalculatesCorrectPriority) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(120);

        analyzers::AnalysisResult analysis;

        // Critical: > 1000ms, >= 50 includers
        analyzers::DependencyAnalysisResult::HeaderInfo critical_header;
        critical_header.path = "critical.h";
        critical_header.total_parse_time = std::chrono::milliseconds(1500);
        critical_header.including_files = 60;
        analysis.dependencies.headers.push_back(critical_header);

        // High: > 500ms, >= 20 includers
        analyzers::DependencyAnalysisResult::HeaderInfo high_header;
        high_header.path = "high.h";
        high_header.total_parse_time = std::chrono::milliseconds(600);
        high_header.including_files = 25;
        analysis.dependencies.headers.push_back(high_header);

        // Medium: > 200ms, >= 10 includers
        analyzers::DependencyAnalysisResult::HeaderInfo medium_header;
        medium_header.path = "medium.h";
        medium_header.total_parse_time = std::chrono::milliseconds(300);
        medium_header.including_files = 12;
        analysis.dependencies.headers.push_back(medium_header);

        // Low: default
        analyzers::DependencyAnalysisResult::HeaderInfo low_header;
        low_header.path = "low.h";
        low_header.total_parse_time = std::chrono::milliseconds(250);
        low_header.including_files = 6;
        analysis.dependencies.headers.push_back(low_header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_GE(result.value().suggestions.size(), 4u);

        // Find suggestions by id
        std::unordered_map<std::string, Priority> priorities;
        for (const auto& s : result.value().suggestions) {
            if (s.id.find("critical.h") != std::string::npos) {
                priorities["critical"] = s.priority;
            } else if (s.id.find("high.h") != std::string::npos) {
                priorities["high"] = s.priority;
            } else if (s.id.find("medium.h") != std::string::npos) {
                priorities["medium"] = s.priority;
            } else if (s.id.find("low.h") != std::string::npos) {
                priorities["low"] = s.priority;
            }
        }

        EXPECT_EQ(priorities["critical"], Priority::Critical);
        EXPECT_EQ(priorities["high"], Priority::High);
        EXPECT_EQ(priorities["medium"], Priority::Medium);
        EXPECT_EQ(priorities["low"], Priority::Low);
    }
}