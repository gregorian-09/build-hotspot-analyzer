// Template suggestions remain disabled until Clang AST validation is connected.

#include "bha/suggestions/template_suggester.hpp"

#include <chrono>
#include <gtest/gtest.h>

namespace bha::suggestions {

    class TemplateSuggesterTest : public testing::Test {
    protected:
        TemplateSuggester suggester_;
    };

    TEST_F(TemplateSuggesterTest, ReportsIdentity) {
        EXPECT_EQ(suggester_.name(), "TemplateSuggester");
        EXPECT_FALSE(suggester_.description().empty());
        EXPECT_EQ(suggester_.suggestion_type(), SuggestionType::ExplicitTemplate);
    }

    TEST_F(TemplateSuggesterTest, RejectsTemplateTimingWithoutSemanticEvidence) {
        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats template_stats;
        template_stats.full_signature = "Candidate<int>";
        template_stats.total_time = std::chrono::milliseconds(500);
        template_stats.instantiation_count = 20;
        analysis.templates.templates.push_back(std::move(template_stats));

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};
        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_skipped, 1u);
    }

    TEST_F(TemplateSuggesterTest, RejectsAggregateTiming) {
        BuildTrace trace;
        trace.template_evidence = TemplateEvidence::AggregateTiming;
        analyzers::AnalysisResult analysis;
        analysis.templates.templates.emplace_back();

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};
        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_skipped, 1u);
    }

    TEST_F(TemplateSuggesterTest, RejectsUnvalidatedSpecializationTiming) {
        BuildTrace trace;
        trace.template_evidence = TemplateEvidence::PerSpecializationTimingWithLocations;
        trace.template_semantic_validated = false;
        analyzers::AnalysisResult analysis;
        analysis.templates.templates.emplace_back();

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};
        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_skipped, 1u);
    }

}  // namespace bha::suggestions
