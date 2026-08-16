// Template suggestions remain disabled until Clang AST validation is connected.

#include "bha/suggestions/template_suggester.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
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
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "template.evidence.insufficient");
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
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "template.evidence.insufficient");
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
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "template.semantic.unvalidated");
    }

    TEST_F(TemplateSuggesterTest, CorrelatesValidatedTraceWithExactAstRecord) {
        const auto root = std::filesystem::temp_directory_path() / "bha-template-suggester-test";
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "src", ec);

        const auto source = root / "src" / "main.cpp";
        std::ofstream(source)
            << "template <typename T> struct Box { T value{}; };\n"
            << "Box<int> make_box();\n"
            << "Box<int>* box_pointer = nullptr;\n"
            << "template struct Box<int>;\n";
        const auto database = root / "compile_commands.json";
        std::ofstream(database)
            << "[{\"directory\":\"" << root.string() << "\","
            << "\"file\":\"src/main.cpp\","
            << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/main.cpp\"]}]";

        BuildTrace trace;
        trace.template_evidence = TemplateEvidence::PerSpecializationTimingWithLocations;
        trace.template_semantic_validated = true;
        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats candidate;
        candidate.full_signature = "Box<int>";
        candidate.total_time = std::chrono::milliseconds(500);
        analysis.templates.templates.push_back(std::move(candidate));

        SuggesterOptions options;
        options.compile_commands_path = database;
        const SuggestionContext context{trace, analysis, options, root};
        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_analyzed, 1u);
        EXPECT_EQ(result.value().items_skipped, 0u);

        std::filesystem::remove_all(root, ec);
    }

}  // namespace bha::suggestions
