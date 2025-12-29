//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/pch_suggester.hpp"

#include <gtest/gtest.h>

namespace bha::suggestions
{
    class PCHSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<PCHSuggester>();
        }

        std::unique_ptr<PCHSuggester> suggester_;
    };

    TEST_F(PCHSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "PCHSuggester");
    }

    TEST_F(PCHSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(PCHSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::PCHOptimization);
    }

    TEST_F(PCHSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(PCHSuggesterTest, SuggestsForExpensiveHeader) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(10);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "expensive_header.h";
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 20;
        header.including_files = 15;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().suggestions.size(), 1u);

        if (!result.value().suggestions.empty()) {
            const auto& suggestion = result.value().suggestions[0];
            EXPECT_EQ(suggestion.type, SuggestionType::PCHOptimization);
            EXPECT_TRUE(suggestion.is_safe);
            EXPECT_GT(suggestion.estimated_savings.count(), 0);
        }
    }

    TEST_F(PCHSuggesterTest, SkipsLowInclusionCount) {
        BuildTrace trace;
        analyzers::AnalysisResult analysis;

        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "rarely_included.h";
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 2;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }
}