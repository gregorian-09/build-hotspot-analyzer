// Template suggestions are emitted only from validated Clang AST records.

#include "bha/suggestions/template_suggester.hpp"
#include "bha/suggestions/suggester.hpp"
#include "bha/suggestions/template_semantic_index.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ranges>

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
            << "Box<int>* make_box();\n"
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
        EXPECT_EQ(result.value().items_analyzed, 0u);
        EXPECT_EQ(result.value().items_skipped, 1u);

        std::filesystem::remove_all(root, ec);
    }

    TEST_F(TemplateSuggesterTest, RejectsMalformedTraceSignatureWithoutApproximateMatching) {
        const auto root = std::filesystem::temp_directory_path() / "bha-template-malformed-signature-test";
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "src", ec);

        const auto source = root / "src" / "main.cpp";
        std::ofstream(source)
            << "template <typename T> struct Box { T value{}; };\n"
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
        candidate.full_signature = "Box< int";
        candidate.total_time = std::chrono::milliseconds(500);
        analysis.templates.templates.push_back(std::move(candidate));

        SuggesterOptions options;
        options.compile_commands_path = database;
        const SuggestionContext context{trace, analysis, options, root};
        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_analyzed, 0u);
        EXPECT_EQ(result.value().items_skipped, 1u);

        std::filesystem::remove_all(root, ec);
    }

    TEST_F(TemplateSuggesterTest, EmitsCanonicalExternEditForHeaderAndUniqueOwner) {
        const auto root = std::filesystem::temp_directory_path() /
            ("bha-template-planner-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "include", ec);
        std::filesystem::create_directories(root / "src", ec);

        const auto header = root / "include" / "box.hpp";
        std::ofstream(header)
            << "#pragma once\n"
            << "template <typename T> struct Box { T value{}; };\n";
        const auto owner = root / "src" / "box.cpp";
        std::ofstream(owner)
            << "#include \"box.hpp\"\n"
            << "template struct Box<int>;\n";
        const auto use = root / "src" / "use.cpp";
        std::ofstream(use)
            << "#include \"box.hpp\"\n"
            << "Box<int>* make_box();\n"
            << "Box<int>* box_pointer = nullptr;\n";

        const auto database = root / "compile_commands.json";
        std::ofstream(database)
            << "[{\"directory\":\"" << root.string() << "\","
            << "\"file\":\"src/box.cpp\","
            << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/box.cpp\"]},"
            << "{\"directory\":\"" << root.string() << "\","
            << "\"file\":\"src/use.cpp\","
            << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/use.cpp\"]}]";

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
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_EQ(suggestion.edits.front().file, header);
        EXPECT_EQ(suggestion.edits.front().new_text, "extern template class Box<int>;\n");
        EXPECT_EQ(suggestion.estimated_savings, Duration::zero());
        EXPECT_EQ(result.value().items_analyzed, 1u);
        EXPECT_EQ(result.value().items_skipped, 0u);

        std::filesystem::remove_all(root, ec);
    }

    TEST_F(TemplateSuggesterTest, RejectsCompleteTypeUseEvenWithUniqueOwner) {
        const auto root = std::filesystem::temp_directory_path() /
            ("bha-template-complete-use-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        std::error_code ec;
        std::filesystem::create_directories(root / "include", ec);
        std::filesystem::create_directories(root / "src", ec);

        std::ofstream(root / "include" / "box.hpp")
            << "#pragma once\n"
            << "template <typename T> struct Box { T value{}; };\n";
        std::ofstream(root / "src" / "box.cpp")
            << "#include \"box.hpp\"\n"
            << "template struct Box<int>;\n";
        std::ofstream(root / "src" / "use.cpp")
            << "#include \"box.hpp\"\n"
            << "constexpr auto box_size = sizeof(Box<int>);\n";
        std::ofstream(root / "compile_commands.json")
            << "[{\"directory\":\"" << root.string() << "\","
            << "\"file\":\"src/box.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/box.cpp\"]},"
            << "{\"directory\":\"" << root.string() << "\","
            << "\"file\":\"src/use.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/use.cpp\"]}]";

        BuildTrace trace;
        trace.template_evidence = TemplateEvidence::PerSpecializationTimingWithLocations;
        trace.template_semantic_validated = true;
        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats candidate;
        candidate.full_signature = "Box<int>";
        analysis.templates.templates.push_back(std::move(candidate));

        SuggesterOptions options;
        options.compile_commands_path = root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, root};
        TemplateSemanticIndex index(*context.project_index);
        index.build();
        ASSERT_EQ(index.status(), TemplateSemanticStatus::Parsed) << index.diagnostic();
        const auto* record = index.find_exact("Box<int>");
        ASSERT_NE(record, nullptr);
        EXPECT_TRUE(std::ranges::any_of(record->uses, [&](const auto& use) {
            return use.source_file == root / "src" / "use.cpp" && use.requires_complete_type;
        }));
        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_skipped, 1u);
        std::filesystem::remove_all(root, ec);
    }

}  // namespace bha::suggestions
