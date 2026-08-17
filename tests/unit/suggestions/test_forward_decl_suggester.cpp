#include "bha/suggestions/forward_decl_semantic_index.hpp"
#include "bha/suggestions/forward_decl_suggester.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <ranges>

namespace bha::suggestions {
    namespace {
        class ForwardDeclSuggesterTest : public ::testing::Test {
        protected:
            void SetUp() override {
                root_ = std::filesystem::temp_directory_path() / "bha-forward-decl-suggester-test";
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
                std::filesystem::create_directories(root_ / "include", ec);
                std::filesystem::create_directories(root_ / "src", ec);
            }

            void TearDown() override {
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
            }

            std::filesystem::path root_;
        };

        void write_compile_commands(
            const std::filesystem::path& root,
            const std::filesystem::path& source
        ) {
            std::ofstream(root / "compile_commands.json")
                << "[{\"directory\":\"" << root.string() << "\","
                << "\"file\":\"src/use.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-Iinclude\",\"-c\",\"src/use.cpp\"]}]";
            (void)source;
        }
    }

    TEST(ForwardDeclSuggesterContractTest, ReportsIdentity) {
        ForwardDeclSuggester suggester;
        EXPECT_EQ(suggester.name(), "ForwardDeclSuggester");
        EXPECT_FALSE(suggester.description().empty());
        EXPECT_EQ(suggester.suggestion_type(), SuggestionType::ForwardDeclaration);
    }

    TEST(ForwardDeclSuggesterContractTest, RequiresCompilationDatabase) {
        ForwardDeclSuggester suggester;
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        const auto result = suggester.suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "forward_decl.semantic.index_required");
    }

    TEST_F(ForwardDeclSuggesterTest, EmitsOnlyAstBackedForwardDeclarationEdit) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box* make_box(Box& input);\n";
        write_compile_commands(root_, source);

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header_info;
        header_info.path = header;
        header_info.total_parse_time = std::chrono::milliseconds(1000);
        header_info.inclusion_count = 1;
        header_info.including_files = 1;
        header_info.included_by = {source};
        analysis.dependencies.headers.push_back(header_info);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.confidence, 1.0);
        EXPECT_TRUE(suggestion.is_safe);
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_EQ(suggestion.edits.front().file, source);
        EXPECT_NE(suggestion.edits.front().new_text.find("struct Box;"), std::string::npos);
        EXPECT_EQ(suggestion.estimated_savings, Duration::zero());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsAstProvenCompleteTypeUse) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box make_box();\n";
        write_compile_commands(root_, source);

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header_info;
        header_info.path = header;
        header_info.total_parse_time = std::chrono::milliseconds(1000);
        header_info.included_by = {source};
        analysis.dependencies.headers.push_back(header_info);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}
