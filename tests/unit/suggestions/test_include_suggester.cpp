#include "bha/suggestions/include_suggester.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::suggestions {
    class IncludeSuggesterTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            fake_root_ = fs::temp_directory_path() / "bha-fake-clang-tidy";
            std::error_code ec;
            fs::remove_all(fake_root_, ec);
            fs::create_directories(fake_root_, ec);
#ifdef _WIN32
            fake_binary_ = fake_root_ / "clang-tidy.cmd";
            write_file(fake_binary_,
                "@echo off\n"
                "setlocal EnableDelayedExpansion\n"
                "set file=%BHA_FAKE_CLANG_TIDY_SOURCE%\n"
                "if \"%BHA_FAKE_CLANG_TIDY_MODE%\"==\"unused\" echo !file!:2:1: warning: included header unused.hpp is not used directly [misc-include-cleaner]\n"
            );
#else
            fake_binary_ = fake_root_ / "clang-tidy";
            write_file(fake_binary_,
                "#!/usr/bin/env bash\n"
                "if [[ \"${BHA_FAKE_CLANG_TIDY_MODE:-}\" == unused ]]; then\n"
                "  printf '%s\\n' \"${BHA_FAKE_CLANG_TIDY_SOURCE}:2:1: warning: included header unused.hpp is not used directly [misc-include-cleaner]\"\n"
                "fi\n"
            );
#endif
            fs::permissions(fake_binary_,
                fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                fs::perm_options::add, ec);
            ASSERT_EQ(set_env("BHA_CLANG_TIDY", fake_binary_.string()), 0);
        }

        static void TearDownTestSuite() {
            unset_env("BHA_CLANG_TIDY");
            unset_env("BHA_FAKE_CLANG_TIDY_MODE");
            unset_env("BHA_FAKE_CLANG_TIDY_SOURCE");
            std::error_code ec;
            fs::remove_all(fake_root_, ec);
        }

        void SetUp() override {
            root_ = fs::temp_directory_path() / (
                "bha-include-suggester-test-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            );
            std::error_code ec;
            fs::create_directories(root_, ec);
            unset_env("BHA_FAKE_CLANG_TIDY_MODE");
            unset_env("BHA_FAKE_CLANG_TIDY_SOURCE");
        }

        void TearDown() override {
            std::error_code ec;
            fs::remove_all(root_, ec);
        }

        static int set_env(const char* name, const std::string& value) {
#ifdef _WIN32
            return _putenv_s(name, value.c_str());
#else
            return setenv(name, value.c_str(), 1);
#endif
        }

        static int unset_env(const char* name) {
#ifdef _WIN32
            return _putenv_s(name, "");
#else
            return unsetenv(name);
#endif
        }

        static void write_file(const fs::path& path, const std::string& content) {
            fs::create_directories(path.parent_path());
            std::ofstream output(path);
            ASSERT_TRUE(output.good());
            output << content;
        }

        void write_compile_database(const fs::path& source) {
            const fs::path build = root_ / "build";
            write_file(build / "compile_commands.json",
                "[{\"directory\":\"" + build.generic_string() +
                "\",\"command\":\"clang++ -c " + source.generic_string() +
                "\",\"file\":\"" + source.generic_string() + "\"}]\n");
            ASSERT_EQ(set_env("BHA_FAKE_CLANG_TIDY_SOURCE", source.generic_string()), 0);
        }

        fs::path root_;
        std::unique_ptr<IncludeSuggester> suggester_ = std::make_unique<IncludeSuggester>();
        static fs::path fake_root_;
        static fs::path fake_binary_;
    };

    fs::path IncludeSuggesterTest::fake_root_;
    fs::path IncludeSuggesterTest::fake_binary_;

    TEST_F(IncludeSuggesterTest, ReportsOnlyIncludeRemoval) {
        EXPECT_EQ(suggester_->supported_types(), std::vector<SuggestionType>{SuggestionType::IncludeRemoval});
    }

    TEST_F(IncludeSuggesterTest, RequiresCompilationDatabaseEvidence) {
        analyzers::AnalysisResult analysis;
        const BuildTrace trace;
        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, root_};

        const auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(IncludeSuggesterTest, MapsClangDiagnosticToExactIncludeEdit) {
        const fs::path source = root_ / "main.cpp";
        write_file(root_ / "unused.hpp", "#pragma once\n");
        write_file(source, "#include <vector>\n#include \"unused.hpp\"\nint main() { return 0; }\n");
        write_compile_database(source);
        ASSERT_EQ(set_env("BHA_FAKE_CLANG_TIDY_MODE", "unused"), 0);

        const BuildTrace trace;
        analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = root_ / "build" / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, root_};

        const auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::IncludeRemoval);
        EXPECT_TRUE(suggestion.is_safe);
        EXPECT_EQ(suggestion.estimated_savings, Duration::zero());
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_EQ(suggestion.edits.front().file, source);
        EXPECT_EQ(suggestion.edits.front().start_line, 1u);
        EXPECT_EQ(suggestion.edits.front().end_line, 2u);
    }

    TEST_F(IncludeSuggesterTest, IgnoresDiagnosticsThatDoNotPointToIncludes) {
        const fs::path source = root_ / "main.cpp";
        write_file(source, "#include \"unused.hpp\"\nint main() { return 0; }\n");
        write_compile_database(source);
        ASSERT_EQ(set_env("BHA_FAKE_CLANG_TIDY_MODE", "unused"), 0);

        const BuildTrace trace;
        analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = root_ / "build" / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, root_};

        const auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}  // namespace bha::suggestions
