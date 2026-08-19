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
            << "#include <vector>\n"
            << "#include \"box.hpp\"\n"
            << "constexpr auto box_size = sizeof(Box<int>);\n"
            << "std::vector<Box<int>> boxes;\n"
            << "Box<int> boxed_array[2];\n";
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
        EXPECT_TRUE(std::ranges::any_of(record->uses, [&](const auto& use) {
            return use.source_file == root / "src" / "use.cpp" &&
                use.kind == "template-argument" && use.requires_complete_type;
        }));
        EXPECT_TRUE(std::ranges::any_of(record->uses, [&](const auto& use) {
            return use.source_file == root / "src" / "use.cpp" &&
                use.kind == "variable-declaration" && use.requires_complete_type;
        }));
        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_skipped, 1u);
        std::filesystem::remove_all(root, ec);
    }

    TEST_F(TemplateSuggesterTest, EmitsCanonicalExternEditForFunctionTemplate) {
        const auto root = std::filesystem::temp_directory_path() /
            ("bha-template-function-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        std::error_code ec;
        std::filesystem::create_directories(root / "include", ec);
        std::filesystem::create_directories(root / "src", ec);

        const auto header = root / "include" / "identity.hpp";
        std::ofstream(header)
            << "#pragma once\n"
            << "template <typename T> T identity(T value) { return value; }\n";
        const auto owner = root / "src" / "identity.cpp";
        std::ofstream(owner)
            << "#include \"identity.hpp\"\n"
            << "template int identity<int>(int);\n";
        const auto use = root / "src" / "use.cpp";
        std::ofstream(use)
            << "#include \"identity.hpp\"\n"
            << "int use_identity() { return identity(42); }\n";
        const auto database = root / "compile_commands.json";
        std::ofstream(database)
            << "[{\"directory\":\"" << root.string() << "\","
            << "\"file\":\"src/identity.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/identity.cpp\"]},"
            << "{\"directory\":\"" << root.string() << "\","
            << "\"file\":\"src/use.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/use.cpp\"]}]";

        BuildTrace trace;
        trace.template_evidence = TemplateEvidence::PerSpecializationTimingWithLocations;
        trace.template_semantic_validated = true;
        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats candidate;
        candidate.full_signature = "identity<int>";
        analysis.templates.templates.push_back(std::move(candidate));
        SuggesterOptions options;
        options.compile_commands_path = database;
        const SuggestionContext context{trace, analysis, options, root};

        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_EQ(suggestion.edits.front().file, header);
        EXPECT_EQ(
            suggestion.edits.front().new_text,
            "extern template int identity<int>(int);\n"
        );
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        std::filesystem::remove_all(root, ec);
    }

    TEST_F(TemplateSuggesterTest, RejectsInlineFunctionTemplate) {
        const auto root = std::filesystem::temp_directory_path() /
            ("bha-template-inline-function-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        std::error_code ec;
        std::filesystem::create_directories(root / "include", ec);
        std::filesystem::create_directories(root / "src", ec);
        std::ofstream(root / "include" / "identity.hpp")
            << "#pragma once\n"
            << "template <typename T> inline T identity(T value) { return value; }\n";
        std::ofstream(root / "src" / "identity.cpp")
            << "#include \"identity.hpp\"\n"
            << "template int identity<int>(int);\n";
        std::ofstream(root / "src" / "use.cpp")
            << "#include \"identity.hpp\"\n"
            << "int use_identity() { return identity(42); }\n";
        std::ofstream(root / "compile_commands.json")
            << "[{\"directory\":\"" << root.string() << "\",\"file\":\"src/identity.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/identity.cpp\"]},"
            << "{\"directory\":\"" << root.string() << "\",\"file\":\"src/use.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/use.cpp\"]}]";

        BuildTrace trace;
        trace.template_evidence = TemplateEvidence::PerSpecializationTimingWithLocations;
        trace.template_semantic_validated = true;
        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats candidate;
        candidate.full_signature = "identity<int>";
        analysis.templates.templates.push_back(std::move(candidate));
        SuggesterOptions options;
        options.compile_commands_path = root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, root};

        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        std::filesystem::remove_all(root, ec);
    }

    TEST_F(TemplateSuggesterTest, EmitsCanonicalExternEditForVariableTemplate) {
        const auto root = std::filesystem::temp_directory_path() /
            ("bha-template-variable-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        std::error_code ec;
        std::filesystem::create_directories(root / "include", ec);
        std::filesystem::create_directories(root / "src", ec);
        const auto header = root / "include" / "value.hpp";
        std::ofstream(header)
            << "#pragma once\n"
            << "template <typename T> T value = T{42};\n";
        const auto owner = root / "src" / "value.cpp";
        std::ofstream(owner)
            << "#include \"value.hpp\"\n"
            << "template int value<int>;\n";
        const auto use = root / "src" / "use.cpp";
        std::ofstream(use)
            << "#include \"value.hpp\"\n"
            << "int use_value() { return value<int>; }\n";
        const auto database = root / "compile_commands.json";
        std::ofstream(database)
            << "[{\"directory\":\"" << root.string() << "\",\"file\":\"src/value.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/value.cpp\"]},"
            << "{\"directory\":\"" << root.string() << "\",\"file\":\"src/use.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/use.cpp\"]}]";

        BuildTrace trace;
        trace.template_evidence = TemplateEvidence::PerSpecializationTimingWithLocations;
        trace.template_semantic_validated = true;
        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats candidate;
        candidate.full_signature = "value<int>";
        analysis.templates.templates.push_back(std::move(candidate));
        SuggesterOptions options;
        options.compile_commands_path = database;
        const SuggestionContext context{trace, analysis, options, root};

        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        ASSERT_EQ(result.value().suggestions.front().edits.size(), 1u);
        EXPECT_EQ(
            result.value().suggestions.front().edits.front().new_text,
            "extern template int value<int>;\n"
        );
        std::filesystem::remove_all(root, ec);
    }

    TEST_F(TemplateSuggesterTest, EmitsCanonicalExternEditForStaticMemberTemplate) {
        const auto root = std::filesystem::temp_directory_path() /
            ("bha-template-member-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        std::error_code ec;
        std::filesystem::create_directories(root / "include", ec);
        std::filesystem::create_directories(root / "src", ec);
        const auto header = root / "include" / "utility.hpp";
        std::ofstream(header)
            << "#pragma once\n"
            << "struct Utility {\n"
            << "  template <typename T> static T identity(T value) { return value; }\n"
            << "};\n";
        const auto owner = root / "src" / "utility.cpp";
        std::ofstream(owner)
            << "#include \"utility.hpp\"\n"
            << "template int Utility::identity<int>(int);\n";
        const auto use = root / "src" / "use.cpp";
        std::ofstream(use)
            << "#include \"utility.hpp\"\n"
            << "int use_identity() { return Utility::identity(42); }\n";
        const auto database = root / "compile_commands.json";
        std::ofstream(database)
            << "[{\"directory\":\"" << root.string() << "\",\"file\":\"src/utility.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/utility.cpp\"]},"
            << "{\"directory\":\"" << root.string() << "\",\"file\":\"src/use.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/use.cpp\"]}]";

        BuildTrace trace;
        trace.template_evidence = TemplateEvidence::PerSpecializationTimingWithLocations;
        trace.template_semantic_validated = true;
        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats candidate;
        candidate.full_signature = "Utility::identity<int>";
        analysis.templates.templates.push_back(std::move(candidate));
        SuggesterOptions options;
        options.compile_commands_path = database;
        const SuggestionContext context{trace, analysis, options, root};

        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        ASSERT_EQ(result.value().suggestions.front().edits.size(), 1u);
        EXPECT_EQ(
            result.value().suggestions.front().edits.front().new_text,
            "extern template int Utility::identity<int>(int);\n"
        );
        std::filesystem::remove_all(root, ec);
    }

    TEST_F(TemplateSuggesterTest, EmitsCanonicalExternEditForConstRefMemberTemplate) {
        const auto root = std::filesystem::temp_directory_path() /
            ("bha-template-qualified-member-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        std::error_code ec;
        std::filesystem::create_directories(root / "include", ec);
        std::filesystem::create_directories(root / "src", ec);
        const auto header = root / "include" / "utility.hpp";
        std::ofstream(header)
            << "#pragma once\n"
            << "struct Utility {\n"
            << "  template <typename T> T identity(T value) const & { return value; }\n"
            << "};\n";
        const auto owner = root / "src" / "utility.cpp";
        std::ofstream(owner)
            << "#include \"utility.hpp\"\n"
            << "template int Utility::identity<int>(int) const &;\n";
        const auto use = root / "src" / "use.cpp";
        std::ofstream(use)
            << "#include \"utility.hpp\"\n"
            << "int use_identity() { return Utility{}.identity(42); }\n";
        const auto database = root / "compile_commands.json";
        std::ofstream(database)
            << "[{\"directory\":\"" << root.string() << "\",\"file\":\"src/utility.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/utility.cpp\"]},"
            << "{\"directory\":\"" << root.string() << "\",\"file\":\"src/use.cpp\",\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/use.cpp\"]}]";

        BuildTrace trace;
        trace.template_evidence = TemplateEvidence::PerSpecializationTimingWithLocations;
        trace.template_semantic_validated = true;
        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats candidate;
        candidate.full_signature = "Utility::identity<int>";
        analysis.templates.templates.push_back(std::move(candidate));
        SuggesterOptions options;
        options.compile_commands_path = database;
        const SuggestionContext context{trace, analysis, options, root};

        const auto result = suggester_.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        ASSERT_EQ(result.value().suggestions.front().edits.size(), 1u);
        EXPECT_EQ(
            result.value().suggestions.front().edits.front().new_text,
            "extern template int Utility::identity<int>(int) const &;\n"
        );
        std::filesystem::remove_all(root, ec);
    }

}  // namespace bha::suggestions
