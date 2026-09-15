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
            fake_root_ = fs::temp_directory_path() / (
                "bha-fake-clang-tidy-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            );
            std::error_code ec;
            fs::remove_all(fake_root_, ec);
            fs::create_directories(fake_root_, ec);
#ifdef _WIN32
            fake_binary_ = fake_root_ / "clang-tidy.cmd";
            write_file(fake_root_ / "clang-cl.exe", "");
            write_file(fake_binary_,
                "@echo off\n"
                "setlocal EnableDelayedExpansion\n"
                "set file=%BHA_FAKE_CLANG_TIDY_SOURCE%\n"
                "set fixes=\n"
                ":next_argument\n"
                "if \"%~1\"==\"\" goto emit\n"
                "set argument=%~1\n"
                "if /I \"!argument:~0,15!\"==\"--export-fixes=\" set fixes=!argument:~15!\n"
                "shift\n"
                "goto next_argument\n"
                ":emit\n"
                "if not \"%BHA_FAKE_CLANG_TIDY_MODE%\"==\"unused\" exit /b 0\n"
                "if \"!fixes!\"==\"\" exit /b 0\n"
                "> \"!fixes!\" echo MainSourceFile: '!file!'\n"
                ">> \"!fixes!\" echo Diagnostics:\n"
                ">> \"!fixes!\" echo   - DiagnosticName: misc-include-cleaner\n"
                ">> \"!fixes!\" echo     DiagnosticMessage:\n"
                ">> \"!fixes!\" echo       Message: included header unused.hpp is not used directly\n"
                ">> \"!fixes!\" echo       FilePath: '!file!'\n"
                ">> \"!fixes!\" echo       FileOffset: !BHA_FAKE_CLANG_TIDY_OFFSET!\n"
                ">> \"!fixes!\" echo       Replacements:\n"
                ">> \"!fixes!\" echo         - FilePath: '!file!'\n"
                ">> \"!fixes!\" echo           Offset: !BHA_FAKE_CLANG_TIDY_OFFSET!\n"
                ">> \"!fixes!\" echo           Length: !BHA_FAKE_CLANG_TIDY_LENGTH!\n"
                ">> \"!fixes!\" echo           ReplacementText: ''\n"
            );
#else
            fake_binary_ = fake_root_ / "clang-tidy";
            write_file(fake_binary_,
                "#!/usr/bin/env bash\n"
                "if [[ \"${BHA_FAKE_CLANG_TIDY_MODE:-}\" == unused ]]; then\n"
                "  fixes=\"\"\n"
                "  for argument in \"$@\"; do\n"
                "    case \"$argument\" in\n"
                "      --export-fixes=*) fixes=\"${argument#--export-fixes=}\" ;;\n"
                "    esac\n"
                "  done\n"
                "  if [[ -n \"$fixes\" ]]; then\n"
                "    cat > \"$fixes\" <<EOF\n"
                "MainSourceFile: '${BHA_FAKE_CLANG_TIDY_SOURCE}'\n"
                "Diagnostics:\n"
                "  - DiagnosticName: misc-include-cleaner\n"
                "    DiagnosticMessage:\n"
                "      Message: included header unused.hpp is not used directly\n"
                "      FilePath: '${BHA_FAKE_CLANG_TIDY_SOURCE}'\n"
                "      FileOffset: ${BHA_FAKE_CLANG_TIDY_OFFSET}\n"
                "      Replacements:\n"
                "        - FilePath: '${BHA_FAKE_CLANG_TIDY_SOURCE}'\n"
                "          Offset: ${BHA_FAKE_CLANG_TIDY_OFFSET}\n"
                "          Length: ${BHA_FAKE_CLANG_TIDY_LENGTH}\n"
                "          ReplacementText: ''\n"
                "EOF\n"
                "  fi\n"
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
            unset_env("BHA_FAKE_CLANG_TIDY_OFFSET");
            unset_env("BHA_FAKE_CLANG_TIDY_LENGTH");
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
            unset_env("BHA_FAKE_CLANG_TIDY_OFFSET");
            unset_env("BHA_FAKE_CLANG_TIDY_LENGTH");
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
            std::ifstream source_input(source, std::ios::binary);
            const std::string content(
                (std::istreambuf_iterator<char>(source_input)),
                std::istreambuf_iterator<char>()
            );
            const auto include_offset = content.find("#include \"unused.hpp\"");
            ASSERT_NE(include_offset, std::string::npos);
            const auto line_end = content.find('\n', include_offset);
            const auto include_length =
                (line_end == std::string::npos ? content.size() : line_end + 1) - include_offset;
#ifdef _WIN32
            const std::string compiler = "cl.exe";
#else
            const std::string compiler = "clang++";
#endif
            write_file(build / "compile_commands.json",
                "[{\"directory\":\"" + build.generic_string() +
                "\",\"command\":\"" + compiler + " -I" + (root_ / "include").generic_string() +
                " -c " + source.generic_string() +
                "\",\"file\":\"" + source.generic_string() + "\"}]\n");
            ASSERT_EQ(set_env("BHA_FAKE_CLANG_TIDY_SOURCE", source.generic_string()), 0);
            ASSERT_EQ(set_env("BHA_FAKE_CLANG_TIDY_OFFSET", std::to_string(include_offset)), 0);
            ASSERT_EQ(set_env("BHA_FAKE_CLANG_TIDY_LENGTH", std::to_string(include_length)), 0);
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
        write_file(
            root_ / "include" / "vector",
            "#pragma once\n"
            "namespace std { template <typename T> class vector {}; }\n"
        );
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
#if !BHA_HAVE_CLANG_TOOLING
        EXPECT_TRUE(result.value().suggestions.empty());
        return;
#else
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::IncludeRemoval);
        EXPECT_TRUE(suggestion.is_safe);
        EXPECT_EQ(suggestion.estimated_savings, Duration::zero());
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_EQ(suggestion.edits.front().file, source);
        EXPECT_EQ(suggestion.edits.front().start_line, 1u);
        EXPECT_EQ(suggestion.edits.front().end_line, 2u);
#endif
    }

    TEST_F(IncludeSuggesterTest, IgnoresDiagnosticsThatDoNotPointToIncludes) {
        const fs::path source = root_ / "main.cpp";
        write_file(source, "#include \"unused.hpp\"\nint main() { return 0; }\n");
        write_compile_database(source);
        ASSERT_EQ(set_env("BHA_FAKE_CLANG_TIDY_OFFSET", "999999"), 0);
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
