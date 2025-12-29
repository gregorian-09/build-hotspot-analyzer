//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/template_suggester.hpp"

#include <gtest/gtest.h>

namespace bha::suggestions
{
    class TemplateSuggesterTest : public testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<TemplateSuggester>();
        }

        std::unique_ptr<TemplateSuggester> suggester_;
    };

    TEST_F(TemplateSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "TemplateSuggester");
    }

    TEST_F(TemplateSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(TemplateSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::ExplicitTemplate);
    }

    TEST_F(TemplateSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(TemplateSuggesterTest, SuggestsForExpensiveTemplate) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name = "MyContainer<int>";
        tmpl.total_time = std::chrono::milliseconds(500);
        tmpl.instantiation_count = 20;
        tmpl.files_using = {"a.cpp", "b.cpp", "c.cpp"};
        analysis.templates.templates.push_back(tmpl);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().suggestions.size(), 1u);

        if (!result.value().suggestions.empty()) {
            const auto& suggestion = result.value().suggestions[0];
            EXPECT_EQ(suggestion.type, SuggestionType::ExplicitTemplate);
            EXPECT_TRUE(suggestion.is_safe);
            EXPECT_GT(suggestion.estimated_savings.count(), 0);
        }
    }

    TEST_F(TemplateSuggesterTest, SkipsStdTemplates) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name = "std::vector<int>";
        tmpl.total_time = std::chrono::milliseconds(500);
        tmpl.instantiation_count = 100;
        analysis.templates.templates.push_back(tmpl);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(TemplateSuggesterTest, SkipsLowCount) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name = "RareTemplate<double>";
        tmpl.total_time = std::chrono::milliseconds(500);
        tmpl.instantiation_count = 2;
        analysis.templates.templates.push_back(tmpl);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}