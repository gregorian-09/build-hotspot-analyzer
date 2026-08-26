#include "bha/suggestions/pch_suggester.hpp"
#include "bha/suggestions/suggester.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::suggestions {
    namespace {
        class PCHSuggesterTest : public ::testing::Test {
        protected:
            void SetUp() override {
                root_ = std::filesystem::temp_directory_path() / (
                    "bha-pch-evidence-test-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                );
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
                std::filesystem::create_directories(root_ / "include", ec);
                std::filesystem::create_directories(root_ / "src", ec);
            }

            void TearDown() override {
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
            }

            void write_sources() {
                std::ofstream(root_ / "include" / "common.hpp") << "#pragma once\nstruct Common {};\n";
                std::ofstream(root_ / "src" / "one.cpp") << "#include \"common.hpp\"\nCommon one;\n";
                std::ofstream(root_ / "src" / "two.cpp") << "#include \"common.hpp\"\nCommon two;\n";
            }

            void write_database(const std::string& second_standard = "c++20") {
                std::ofstream(root_ / "compile_commands.json")
                    << "[{\"directory\":\"" << root_.generic_string() << "\","
                    << "\"file\":\"" << (root_ / "src" / "one.cpp").generic_string() << "\","
                    << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-I"
                    << (root_ / "include").generic_string() << "\",\"-c\",\""
                    << (root_ / "src" / "one.cpp").generic_string() << "\"]},"
                    << "{\"directory\":\"" << root_.generic_string() << "\","
                    << "\"file\":\"" << (root_ / "src" / "two.cpp").generic_string() << "\","
                    << "\"arguments\":[\"clang++\",\"-std=" << second_standard << "\",\"-I"
                    << (root_ / "include").generic_string() << "\",\"-c\",\""
                    << (root_ / "src" / "two.cpp").generic_string() << "\"]}]";
            }

            analyzers::AnalysisResult dependency_analysis(
                const std::filesystem::path& header,
                const std::vector<std::filesystem::path>& includers
            ) const {
                analyzers::AnalysisResult analysis;
                analyzers::DependencyAnalysisResult::HeaderInfo info;
                info.path = header;
                info.total_parse_time = std::chrono::milliseconds(1000);
                info.inclusion_count = includers.size();
                info.including_files = includers.size();
                info.included_by = includers;
                analysis.dependencies.headers.push_back(std::move(info));
                return analysis;
            }

            std::filesystem::path root_;
        };
    }

    TEST(PCHSuggesterContractTest, ReportsIdentity) {
        PCHSuggester suggester;
        EXPECT_EQ(suggester.name(), "PCHSuggester");
        EXPECT_FALSE(suggester.description().empty());
        EXPECT_EQ(suggester.suggestion_type(), SuggestionType::PCHOptimization);
    }

    TEST(PCHSuggesterContractTest, RequiresCompilationDatabase) {
        PCHSuggester suggester;
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        const auto result = suggester.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "pch.evidence.compile_database_required");
    }

    TEST_F(PCHSuggesterTest, EmitsAdvisoryOnlyForRepeatedExactEnvironment) {
        write_sources();
        write_database();
        const auto header = root_ / "include" / "common.hpp";
        const auto one = root_ / "src" / "one.cpp";
        const auto two = root_ / "src" / "two.cpp";

        BuildTrace trace;
        const auto analysis = dependency_analysis(header, {one, two});
        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = PCHSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        EXPECT_EQ(suggestion.estimated_savings, Duration::zero());
        EXPECT_EQ(suggestion.confidence, 1.0);
    }

    TEST_F(PCHSuggesterTest, RejectsDifferentCompileEnvironments) {
        write_sources();
        write_database("c++17");
        const auto analysis = dependency_analysis(
            root_ / "include" / "common.hpp",
            {root_ / "src" / "one.cpp", root_ / "src" / "two.cpp"}
        );
        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const SuggestionContext context{BuildTrace{}, analysis, options, root_};
        const auto result = PCHSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_skipped, 1u);
    }

    TEST_F(PCHSuggesterTest, RejectsMissingIncluderCompileCommand) {
        write_sources();
        write_database();
        const auto missing = root_ / "src" / "missing.cpp";
        const auto analysis = dependency_analysis(
            root_ / "include" / "common.hpp",
            {root_ / "src" / "one.cpp", missing}
        );
        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const SuggestionContext context{BuildTrace{}, analysis, options, root_};
        const auto result = PCHSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(PCHSuggesterTest, RejectsDuplicateIncludeEventsFromOneTranslationUnit) {
        write_sources();
        write_database();
        const auto header = root_ / "include" / "common.hpp";
        const auto one = root_ / "src" / "one.cpp";
        const auto analysis = dependency_analysis(header, {one, one});
        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const SuggestionContext context{BuildTrace{}, analysis, options, root_};

        const auto result = PCHSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(PCHSuggesterTest, RejectsMixedLanguageCompileEnvironments) {
        write_sources();
        write_database();
        const auto header = root_ / "include" / "common.hpp";
        const auto one = root_ / "src" / "one.cpp";
        const auto two = root_ / "src" / "two.cpp";
        std::ofstream(root_ / "compile_commands.json")
            << "[{\"directory\":\"" << root_.generic_string() << "\","
            << "\"file\":\"" << one.generic_string() << "\","
            << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-I"
            << (root_ / "include").generic_string() << "\",\"-c\",\""
            << one.generic_string() << "\"]},"
            << "{\"directory\":\"" << root_.generic_string() << "\","
            << "\"file\":\"" << two.generic_string() << "\","
            << "\"arguments\":[\"clang++\",\"-x\",\"c\",\"-I"
            << (root_ / "include").generic_string() << "\",\"-c\",\""
            << two.generic_string() << "\"]}]";
        const auto analysis = dependency_analysis(header, {one, two});
        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const SuggestionContext context{BuildTrace{}, analysis, options, root_};

        const auto result = PCHSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}
