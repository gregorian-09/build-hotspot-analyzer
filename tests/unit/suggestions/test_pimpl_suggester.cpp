#include "bha/suggestions/pimpl_suggester.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <utility>

namespace bha::suggestions {
    namespace {
        class PimplSuggesterTest : public ::testing::Test {
        protected:
            void SetUp() override {
                root_ = std::filesystem::temp_directory_path() / (
                    "bha-pimpl-suggester-test-" +
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

            void write_compile_commands(const std::filesystem::path& source) {
                std::ofstream database(root_ / "compile_commands.json");
                database << "[{\"directory\":\"" << root_.generic_string() << "\","
                         << "\"file\":\"" << source.generic_string() << "\","
                         << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-I"
                         << (root_ / "include").generic_string() << "\",\"-c\",\""
                         << source.generic_string() << "\"]}]";
            }

            void write_measured_source(const std::filesystem::path& source) {
                std::ofstream(source)
                    << "#include \"widget.hpp\"\n"
                    << "int demo::Widget::value_copy() const { return value_; }\n";
            }

            analyzers::AnalysisResult measured_analysis(
                const std::filesystem::path& source
            ) const {
                analyzers::AnalysisResult analysis;
                analyzers::FileAnalysisResult file;
                file.file = source;
                file.compile_time = std::chrono::milliseconds(1);
                analysis.files.push_back(std::move(file));
                return analysis;
            }

            std::filesystem::path root_;
        };
    }

    TEST(PimplSuggesterContractTest, ReportsIdentity) {
        PIMPLSuggester suggester;
        EXPECT_EQ(suggester.name(), "PIMPLSuggester");
        EXPECT_FALSE(suggester.description().empty());
        EXPECT_EQ(suggester.suggestion_type(), SuggestionType::PIMPLPattern);
    }

    TEST(PimplSuggesterContractTest, RequiresCompilationDatabase) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        const auto result = PIMPLSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code,
                  "pimpl.evidence.compile_database_required");
    }

    TEST_F(PimplSuggesterTest, EmitsOnlyAstBackedAdvisoryEvidence) {
        const auto header = root_ / "include" / "widget.hpp";
        std::ofstream(header)
            << "#pragma once\n"
            << "namespace demo {\n"
            << "class Widget {\n"
            << "private:\n"
            << "    int value_;\n"
            << "public:\n"
            << "    int value_copy() const;\n"
            << "};\n"
            << "}\n";
        const auto source = root_ / "src" / "widget.cpp";
        write_measured_source(source);
        write_compile_commands(source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const BuildTrace trace;
        const auto analysis = measured_analysis(source);
        const SuggestionContext context{trace, analysis, options, root_};

        const auto result = PIMPLSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
#if BHA_HAVE_CLANG_TOOLING
        ASSERT_EQ(result.value().suggestions.size(), 1u)
            << "diagnostics=" << result.value().diagnostics.size();
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::PIMPLPattern);
        EXPECT_EQ(suggestion.target_file.path, header);
        EXPECT_EQ(suggestion.target_file.line_start, 3u);
        EXPECT_EQ(suggestion.estimated_savings, Duration::zero());
        EXPECT_DOUBLE_EQ(suggestion.estimated_savings_percent, 0.0);
        EXPECT_TRUE(suggestion.edits.empty());
        EXPECT_TRUE(suggestion.after_code.code.empty());
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_NE(suggestion.auto_apply_blocked_reason->find("structural refactor"),
                  std::string::npos);
#else
        EXPECT_TRUE(result.value().suggestions.empty());
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code,
                  "pimpl.evidence.clang_tooling_required");
#endif
    }

    TEST_F(PimplSuggesterTest, RejectsUnsupportedAstShapes) {
        const auto header = root_ / "include" / "widget.hpp";
        std::ofstream(header)
            << "#pragma once\n"
            << "class Widget {\n"
            << "private: int value_;\n"
            << "public: virtual int value_copy() const { return value_; }\n"
            << "};\n";
        const auto source = root_ / "src" / "widget.cpp";
        std::ofstream(source) << "#include \"widget.hpp\"\n";
        write_compile_commands(source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const BuildTrace trace;
        const auto analysis = measured_analysis(source);
        const SuggestionContext context{trace, analysis, options, root_};

        const auto result = PIMPLSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
#if BHA_HAVE_CLANG_TOOLING
        EXPECT_TRUE(result.value().suggestions.empty());
#else
        EXPECT_TRUE(result.value().suggestions.empty());
#endif
    }

    TEST_F(PimplSuggesterTest, DoesNotInferHeadersForSourceLocalClasses) {
        const auto source = root_ / "src" / "widget.cpp";
        std::ofstream(source)
            << "class Widget { private: int value_; };\n"
            << "int use_widget() { return 0; }\n";
        write_compile_commands(source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const BuildTrace trace;
        const auto analysis = measured_analysis(source);
        const SuggestionContext context{trace, analysis, options, root_};

        const auto result = PIMPLSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}
