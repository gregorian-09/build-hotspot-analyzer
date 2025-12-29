//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/include_suggester.hpp"

#include <gtest/gtest.h>

namespace bha::suggestions
{
    class IncludeSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<IncludeSuggester>();
        }

        std::unique_ptr<IncludeSuggester> suggester_;
    };

    TEST_F(IncludeSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "IncludeSuggester");
    }

    TEST_F(IncludeSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(IncludeSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::IncludeRemoval);
    }

    TEST_F(IncludeSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(IncludeSuggesterTest, SuggestsForPotentiallyUnused) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "bloated_header.h";
        header.total_parse_time = std::chrono::milliseconds(200);
        header.inclusion_count = 30;
        header.including_files = 10;
        analysis.dependencies.headers.push_back(header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().suggestions.size(), 1u);
    }

    TEST_F(IncludeSuggesterTest, SkipsCheapHeaders) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "cheap_header.h";
        header.total_parse_time = std::chrono::milliseconds(10);
        header.inclusion_count = 100;
        analysis.dependencies.headers.push_back(header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}