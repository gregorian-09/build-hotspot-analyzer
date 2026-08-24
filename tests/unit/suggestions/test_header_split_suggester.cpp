#include "bha/suggestions/header_split_suggester.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::suggestions {
    namespace {
        class HeaderSplitSuggesterTest : public ::testing::Test {
        protected:
            void SetUp() override {
                root_ = std::filesystem::temp_directory_path() / "bha-header-split-suggester-test";
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
                std::filesystem::create_directories(root_ / "include", ec);
                std::filesystem::create_directories(root_ / "src", ec);
            }

            void TearDown() override {
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
            }

            void write_compile_commands() {
                std::ofstream(root_ / "compile_commands.json")
                    << "[{\"directory\":\"" << root_.string() << "\","
                    << "\"file\":\"src/use.cpp\","
                    << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-I"
                    << (root_ / "include").string() << "\",\"-c\",\""
                    << (root_ / "src/use.cpp").string() << "\"]}]";
            }

            std::filesystem::path root_;
        };

        analyzers::AnalysisResult dependency_analysis(
            const std::filesystem::path& header,
            const std::filesystem::path& source
        ) {
            analyzers::AnalysisResult analysis;
            analyzers::DependencyAnalysisResult::HeaderInfo info;
            info.path = header;
            info.total_parse_time = std::chrono::milliseconds(1000);
            info.inclusion_count = 1;
            info.including_files = 1;
            info.included_by = {source};
            analysis.dependencies.headers.push_back(std::move(info));
            return analysis;
        }
    }

    TEST(HeaderSplitSuggesterContractTest, ReportsIdentity) {
        HeaderSplitSuggester suggester;
        EXPECT_EQ(suggester.name(), "HeaderSplitSuggester");
        EXPECT_FALSE(suggester.description().empty());
        EXPECT_EQ(suggester.suggestion_type(), SuggestionType::HeaderSplit);
    }

    TEST(HeaderSplitSuggesterContractTest, RequiresCompilationDatabase) {
        HeaderSplitSuggester suggester;
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        const auto result = suggester.suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "header_split.semantic.index_required");
    }

    TEST_F(HeaderSplitSuggesterTest, EmitsOnlyAstBackedCompanionHeaderSplit) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box* make_box(Box& input);\n";
        write_compile_commands();

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = HeaderSplitSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u)
            << "analyzed=" << result.value().items_analyzed
            << " skipped=" << result.value().items_skipped;
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.confidence, 1.0);
        EXPECT_TRUE(suggestion.is_safe);
        ASSERT_EQ(suggestion.edits.size(), 2u);
        EXPECT_EQ(suggestion.edits.front().file.filename(), "box_fwd.hpp");
        EXPECT_NE(suggestion.edits.front().new_text.find("struct Box;"), std::string::npos);
        ASSERT_TRUE(suggestion.edits.back().has_byte_range());
        EXPECT_GT(*suggestion.edits.back().byte_length, 0u);
        EXPECT_EQ(suggestion.estimated_savings, Duration::zero());
    }

    TEST_F(HeaderSplitSuggesterTest, IgnoresDependencyHeadersOutsideProjectRoot) {
        const auto external_header = root_.parent_path() / "bha-header-split-external.hpp";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source) << "struct External;\nExternal* use_external();\n";
        write_compile_commands();

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(external_header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = HeaderSplitSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_analyzed, 0u);
    }

    TEST_F(HeaderSplitSuggesterTest, RejectsCompleteTypeUse) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box make_box();\n";
        write_compile_commands();

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = HeaderSplitSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}
