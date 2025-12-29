//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/forward_decl_suggester.hpp"

#include <gtest/gtest.h>

namespace bha::suggestions
{
    class ForwardDeclSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<ForwardDeclSuggester>();
        }

        std::unique_ptr<ForwardDeclSuggester> suggester_;
    };

    TEST_F(ForwardDeclSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "ForwardDeclSuggester");
    }

    TEST_F(ForwardDeclSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(ForwardDeclSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::ForwardDeclaration);
    }

    TEST_F(ForwardDeclSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, SuggestsForHeaderInHeader) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "widget.h";
        header.total_parse_time = std::chrono::milliseconds(100);
        header.inclusion_count = 5;
        header.including_files = 3;
        header.included_by = {"base.h", "factory.h"};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().suggestions.size(), 1u);

        if (!result.value().suggestions.empty()) {
            const auto& suggestion = result.value().suggestions[0];
            EXPECT_EQ(suggestion.type, SuggestionType::ForwardDeclaration);
            EXPECT_FALSE(suggestion.is_safe);
        }
    }

    TEST_F(ForwardDeclSuggesterTest, SkipsNonHeaders) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "source.cpp";
        header.total_parse_time = std::chrono::milliseconds(100);
        header.inclusion_count = 5;
        analysis.dependencies.headers.push_back(header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}