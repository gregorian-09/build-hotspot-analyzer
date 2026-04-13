//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/include_suggester.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::suggestions
{
    class IncludeSuggesterTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            fake_clang_tidy_root_ = std::filesystem::temp_directory_path() / "bha-fake-clang-tidy";
            std::error_code ec;
            std::filesystem::remove_all(fake_clang_tidy_root_, ec);
            std::filesystem::create_directories(fake_clang_tidy_root_, ec);
            fake_clang_tidy_path_ = fake_clang_tidy_root_ / "clang-tidy";
            write_file(
                fake_clang_tidy_path_,
                R"(#!/usr/bin/env bash
mode="${BHA_FAKE_CLANG_TIDY_MODE:-}"
file=""
for arg in "$@"; do
  if [[ "$arg" == *.cpp || "$arg" == *.cc || "$arg" == *.cxx || "$arg" == *.c ]]; then
    file="$arg"
  fi
done
case "$mode" in
  conditional)
    printf '%s\n' "$file:2:1: warning: included header platform/windows_only.hpp is not used directly [misc-include-cleaner]"
    ;;
  direct)
    printf '%s\n' "$file:1:1: warning: included header local_unused.hpp is not used directly [misc-include-cleaner]"
    ;;
  protected)
    printf '%s\n' "$file:1:1: warning: included header port/malloc.h is not used directly [misc-include-cleaner]"
    ;;
  self)
    printf '%s\n' "$file:1:1: warning: included header widget.h is not used directly [misc-include-cleaner]"
    ;;
  mixed)
    printf '%s\n' "$file:1:1: warning: included header local_unused.hpp is not used directly [misc-include-cleaner]"
    printf '%s\n' "$file:4:1: warning: included header windows.h is not used directly [misc-include-cleaner]"
    ;;
esac
)"
            );
            std::filesystem::permissions(
                fake_clang_tidy_path_,
                std::filesystem::perms::owner_exec |
                    std::filesystem::perms::owner_read |
                    std::filesystem::perms::owner_write,
                std::filesystem::perm_options::add,
                ec
            );
            ASSERT_EQ(setenv("BHA_CLANG_TIDY", fake_clang_tidy_path_.c_str(), 1), 0);
        }

        static void TearDownTestSuite() {
            std::error_code ec;
            std::filesystem::remove_all(fake_clang_tidy_root_, ec);
            unsetenv("BHA_CLANG_TIDY");
            unsetenv("BHA_FAKE_CLANG_TIDY_MODE");
        }

        void SetUp() override {
            suggester_ = std::make_unique<IncludeSuggester>();
            const auto unique_suffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ) + "-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
            temp_root_ = std::filesystem::temp_directory_path() / ("bha-include-suggester-test-" + unique_suffix);
            std::error_code ec;
            std::filesystem::remove_all(temp_root_, ec);
            std::filesystem::create_directories(temp_root_, ec);
            unsetenv("BHA_FAKE_CLANG_TIDY_MODE");
        }

        void TearDown() override {
            std::error_code ec;
            std::filesystem::remove_all(temp_root_, ec);
            unsetenv("BHA_FAKE_CLANG_TIDY_MODE");
        }

        static void write_file(const std::filesystem::path& path, const std::string& content) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path);
            ASSERT_TRUE(out.good());
            out << content;
        }

        static void write_compile_commands(const std::filesystem::path& build_dir, const std::filesystem::path& source) {
            write_file(
                build_dir / "compile_commands.json",
                "[\n"
                "  {\n"
                "    \"directory\": \"" + build_dir.generic_string() + "\",\n"
                "    \"command\": \"clang++ -c " + source.generic_string() + "\",\n"
                "    \"file\": \"" + source.generic_string() + "\"\n"
                "  }\n"
                "]\n"
            );
        }

        std::unique_ptr<IncludeSuggester> suggester_;
        std::filesystem::path temp_root_;
        static std::filesystem::path fake_clang_tidy_root_;
        static std::filesystem::path fake_clang_tidy_path_;
    };

    std::filesystem::path IncludeSuggesterTest::fake_clang_tidy_root_;
    std::filesystem::path IncludeSuggesterTest::fake_clang_tidy_path_;

    TEST_F(IncludeSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "IncludeSuggester");
    }

    TEST_F(IncludeSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(IncludeSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::IncludeRemoval);
    }

    TEST_F(IncludeSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options, {}};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(IncludeSuggesterTest, RequiresSemanticEvidenceForIncludeRemoval) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);
        for (int i = 0; i < 3; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";
            IncludeInfo include;
            include.header = "bloated_header.h";
            include.parse_time = std::chrono::milliseconds(120);
            include.depth = 1;
            unit.includes.push_back(std::move(include));
            trace.units.push_back(std::move(unit));
        }

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "bloated_header.h";
        header.total_parse_time = std::chrono::milliseconds(200);
        header.inclusion_count = 30;
        header.including_files = 3;
        analysis.dependencies.headers.push_back(header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(IncludeSuggesterTest, SkipsCheapHeaders) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "cheap_header.h";
        header.total_parse_time = std::chrono::milliseconds(10);
        header.inclusion_count = 100;
        analysis.dependencies.headers.push_back(header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(IncludeSuggesterTest, SuggestsSafeMoveToCppWhenForwardDeclarationExists) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(30);

        const auto heavy_header = temp_root_ / "heavy.hpp";
        const auto widget_header = temp_root_ / "widget.hpp";
        const auto widget_source = temp_root_ / "widget.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class Heavy {\n"
            "public:\n"
            "    int value() const;\n"
            "};\n"
        );
        write_file(
            widget_header,
            "#pragma once\n"
            "class Heavy;\n"
            "#include \"heavy.hpp\"\n"
            "class Widget {\n"
            "public:\n"
            "    void run(Heavy& arg);\n"
            "private:\n"
            "    Heavy* ptr{};\n"
            "};\n"
        );
        write_file(
            widget_source,
            "#include \"widget.hpp\"\n"
            "void Widget::run(Heavy&) {}\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult file_header;
        file_header.file = widget_header;
        analysis.files.push_back(file_header);
        analyzers::FileAnalysisResult file_source;
        file_source.file = widget_source;
        analysis.files.push_back(file_source);

        analyzers::DependencyAnalysisResult::HeaderInfo dep_header;
        dep_header.path = heavy_header;
        dep_header.total_parse_time = std::chrono::milliseconds(250);
        dep_header.inclusion_count = 8;
        dep_header.including_files = 1;
        dep_header.included_by = {widget_header};
        analysis.dependencies.headers.push_back(dep_header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());

        auto it = std::find_if(
            result.value().suggestions.begin(),
            result.value().suggestions.end(),
            [](const Suggestion& suggestion) {
                return suggestion.type == SuggestionType::MoveToCpp;
            }
        );
        ASSERT_NE(it, result.value().suggestions.end());
        EXPECT_TRUE(it->is_safe);
        EXPECT_GE(it->edits.size(), 2u);
    }

    TEST_F(IncludeSuggesterTest, SkipsMoveToCppWhenByValueUsageRequiresCompleteType) {
        const BuildTrace trace;

        const auto heavy_header = temp_root_ / "heavy.hpp";
        const auto widget_header = temp_root_ / "widget.hpp";
        const auto widget_source = temp_root_ / "widget.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class Heavy { int v; };\n"
        );
        write_file(
            widget_header,
            "#pragma once\n"
            "class Heavy;\n"
            "#include \"heavy.hpp\"\n"
            "class Widget {\n"
            "private:\n"
            "    Heavy value;\n"
            "};\n"
        );
        write_file(
            widget_source,
            "#include \"widget.hpp\"\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult file_header;
        file_header.file = widget_header;
        analysis.files.push_back(file_header);
        analyzers::FileAnalysisResult file_source;
        file_source.file = widget_source;
        analysis.files.push_back(file_source);

        analyzers::DependencyAnalysisResult::HeaderInfo dep_header;
        dep_header.path = heavy_header;
        dep_header.total_parse_time = std::chrono::milliseconds(250);
        dep_header.inclusion_count = 8;
        dep_header.including_files = 1;
        dep_header.included_by = {widget_header};
        analysis.dependencies.headers.push_back(dep_header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());

        auto it = std::find_if(
            result.value().suggestions.begin(),
            result.value().suggestions.end(),
            [](const Suggestion& suggestion) {
                return suggestion.type == SuggestionType::MoveToCpp;
            }
        );
        EXPECT_EQ(it, result.value().suggestions.end());
    }

    TEST_F(IncludeSuggesterTest, SkipsMoveToCppWhenHeaderDereferencesForwardDeclaredPointer) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(30);

        const auto heavy_header = temp_root_ / "callback.hpp";
        const auto widget_header = temp_root_ / "widget.hpp";
        const auto widget_source = temp_root_ / "widget.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class Callback {\n"
            "public:\n"
            "    bool IsVisible() const;\n"
            "};\n"
        );
        write_file(
            widget_header,
            "#pragma once\n"
            "class Callback;\n"
            "#include \"callback.hpp\"\n"
            "class Widget {\n"
            "public:\n"
            "    bool visible() const { return callback_->IsVisible(); }\n"
            "private:\n"
            "    Callback* callback_{};\n"
            "};\n"
        );
        write_file(widget_source, "#include \"widget.hpp\"\n");

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult file_header;
        file_header.file = widget_header;
        analysis.files.push_back(file_header);
        analyzers::FileAnalysisResult file_source;
        file_source.file = widget_source;
        analysis.files.push_back(file_source);

        analyzers::DependencyAnalysisResult::HeaderInfo dep_header;
        dep_header.path = heavy_header;
        dep_header.total_parse_time = std::chrono::milliseconds(250);
        dep_header.inclusion_count = 8;
        dep_header.including_files = 1;
        dep_header.included_by = {widget_header};
        analysis.dependencies.headers.push_back(dep_header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());

        auto it = std::find_if(
            result.value().suggestions.begin(),
            result.value().suggestions.end(),
            [](const Suggestion& suggestion) {
                return suggestion.type == SuggestionType::MoveToCpp;
            }
        );
        EXPECT_EQ(it, result.value().suggestions.end());
    }

    TEST_F(IncludeSuggesterTest, SkipsMoveToCppWhenAliasExportIsStillUsedInHeader) {
        const BuildTrace trace;

        const auto heavy_header = temp_root_ / "types.hpp";
        const auto widget_header = temp_root_ / "widget.hpp";
        const auto widget_source = temp_root_ / "widget.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class Forwardable {};\n"
            "using AliasType = int;\n"
        );
        write_file(
            widget_header,
            "#pragma once\n"
            "class Forwardable;\n"
            "#include \"types.hpp\"\n"
            "class Widget {\n"
            "public:\n"
            "    const AliasType& alias() const;\n"
            "private:\n"
            "    Forwardable* forwardable_{};\n"
            "};\n"
        );
        write_file(widget_source, "#include \"widget.hpp\"\n");

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult file_header;
        file_header.file = widget_header;
        analysis.files.push_back(file_header);
        analyzers::FileAnalysisResult file_source;
        file_source.file = widget_source;
        analysis.files.push_back(file_source);

        analyzers::DependencyAnalysisResult::HeaderInfo dep_header;
        dep_header.path = heavy_header;
        dep_header.total_parse_time = std::chrono::milliseconds(250);
        dep_header.inclusion_count = 8;
        dep_header.including_files = 1;
        dep_header.included_by = {widget_header};
        analysis.dependencies.headers.push_back(dep_header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());

        auto it = std::find_if(
            result.value().suggestions.begin(),
            result.value().suggestions.end(),
            [](const Suggestion& suggestion) {
                return suggestion.type == SuggestionType::MoveToCpp;
            }
        );
        EXPECT_EQ(it, result.value().suggestions.end());
    }

    TEST_F(IncludeSuggesterTest, SkipsMoveToCppWhenNotAllUsedSymbolsHaveForwardDeclarations) {
        const BuildTrace trace;

        const auto heavy_header = temp_root_ / "types.hpp";
        const auto widget_header = temp_root_ / "widget.hpp";
        const auto widget_source = temp_root_ / "widget.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class Foo {};\n"
            "class Bar {};\n"
        );
        write_file(
            widget_header,
            "#pragma once\n"
            "class Foo;\n"
            "#include \"types.hpp\"\n"
            "class Widget {\n"
            "private:\n"
            "    Foo* foo_{};\n"
            "    Bar* bar_{};\n"
            "};\n"
        );
        write_file(widget_source, "#include \"widget.hpp\"\n");

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult file_header;
        file_header.file = widget_header;
        analysis.files.push_back(file_header);
        analyzers::FileAnalysisResult file_source;
        file_source.file = widget_source;
        analysis.files.push_back(file_source);

        analyzers::DependencyAnalysisResult::HeaderInfo dep_header;
        dep_header.path = heavy_header;
        dep_header.total_parse_time = std::chrono::milliseconds(250);
        dep_header.inclusion_count = 8;
        dep_header.including_files = 1;
        dep_header.included_by = {widget_header};
        analysis.dependencies.headers.push_back(dep_header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());

        auto it = std::find_if(
            result.value().suggestions.begin(),
            result.value().suggestions.end(),
            [](const Suggestion& suggestion) {
                return suggestion.type == SuggestionType::MoveToCpp;
            }
        );
        EXPECT_EQ(it, result.value().suggestions.end());
    }

    TEST_F(IncludeSuggesterTest, SuggestsMoveToCppWhenTransitiveExportsAreUnused) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(30);

        const auto dep_header = temp_root_ / "dep.hpp";
        const auto wrapper_header = temp_root_ / "wrapper.hpp";
        const auto widget_header = temp_root_ / "widget.hpp";
        const auto widget_source = temp_root_ / "widget.cpp";

        write_file(
            dep_header,
            "#pragma once\n"
            "struct HelperType { int value; };\n"
        );
        write_file(
            wrapper_header,
            "#pragma once\n"
            "#include \"dep.hpp\"\n"
            "class Wrapper {};\n"
        );
        write_file(
            widget_header,
            "#pragma once\n"
            "class Wrapper;\n"
            "#include \"wrapper.hpp\"\n"
            "class Widget {\n"
            "private:\n"
            "    Wrapper* wrapper_{};\n"
            "};\n"
        );
        write_file(widget_source, "#include \"widget.hpp\"\n");

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult file_header;
        file_header.file = widget_header;
        analysis.files.push_back(file_header);
        analyzers::FileAnalysisResult file_source;
        file_source.file = widget_source;
        analysis.files.push_back(file_source);

        analyzers::DependencyAnalysisResult::HeaderInfo dep_header_info;
        dep_header_info.path = wrapper_header;
        dep_header_info.total_parse_time = std::chrono::milliseconds(250);
        dep_header_info.inclusion_count = 8;
        dep_header_info.including_files = 1;
        dep_header_info.included_by = {widget_header};
        analysis.dependencies.headers.push_back(dep_header_info);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());

        auto it = std::find_if(
            result.value().suggestions.begin(),
            result.value().suggestions.end(),
            [](const Suggestion& suggestion) {
                return suggestion.type == SuggestionType::MoveToCpp;
            }
        );
        ASSERT_NE(it, result.value().suggestions.end());
        EXPECT_TRUE(it->is_safe);
    }

    TEST_F(IncludeSuggesterTest, SkipsMoveToCppWhenHeaderDependsOnTransitiveExportedSymbol) {
        const BuildTrace trace;

        const auto dep_header = temp_root_ / "dep.hpp";
        const auto wrapper_header = temp_root_ / "wrapper.hpp";
        const auto widget_header = temp_root_ / "widget.hpp";
        const auto widget_source = temp_root_ / "widget.cpp";

        write_file(
            dep_header,
            "#pragma once\n"
            "struct NeededType { int value; };\n"
        );
        write_file(
            wrapper_header,
            "#pragma once\n"
            "#include \"dep.hpp\"\n"
            "class Wrapper {};\n"
        );
        write_file(
            widget_header,
            "#pragma once\n"
            "class Wrapper;\n"
            "#include \"wrapper.hpp\"\n"
            "class Widget {\n"
            "private:\n"
            "    NeededType needed_{};\n"
            "    Wrapper* wrapper_{};\n"
            "};\n"
        );
        write_file(widget_source, "#include \"widget.hpp\"\n");

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult file_header;
        file_header.file = widget_header;
        analysis.files.push_back(file_header);
        analyzers::FileAnalysisResult file_source;
        file_source.file = widget_source;
        analysis.files.push_back(file_source);

        analyzers::DependencyAnalysisResult::HeaderInfo dep_header_info;
        dep_header_info.path = wrapper_header;
        dep_header_info.total_parse_time = std::chrono::milliseconds(250);
        dep_header_info.inclusion_count = 8;
        dep_header_info.including_files = 1;
        dep_header_info.included_by = {widget_header};
        analysis.dependencies.headers.push_back(dep_header_info);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());

        auto it = std::find_if(
            result.value().suggestions.begin(),
            result.value().suggestions.end(),
            [](const Suggestion& suggestion) {
                return suggestion.type == SuggestionType::MoveToCpp;
            }
        );
        EXPECT_EQ(it, result.value().suggestions.end());
    }

    TEST_F(IncludeSuggesterTest, DowngradesConditionalPlatformIncludesToAdvisory) {
        ASSERT_EQ(setenv("BHA_FAKE_CLANG_TIDY_MODE", "conditional", 1), 0);

        const auto build_dir = temp_root_ / "build";
        const auto source = temp_root_ / "main.cpp";
        write_file(
            source,
            "#ifdef _WIN32\n"
            "#include \"platform/windows_only.hpp\"\n"
            "#endif\n"
            "int main() { return 0; }\n"
        );
        write_compile_commands(build_dir, source);

        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = build_dir / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const Suggestion& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::IncludeRemoval);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_NE(
            suggestion.auto_apply_blocked_reason->find("conditional compilation"),
            std::string::npos
        );
        EXPECT_NE(
            suggestion.auto_apply_blocked_reason->find("platform-specific"),
            std::string::npos
        );
    }

    TEST_F(IncludeSuggesterTest, SplitsSafeAndPlatformSpecificUnusedIncludeSuggestions) {
        ASSERT_EQ(setenv("BHA_FAKE_CLANG_TIDY_MODE", "mixed", 1), 0);

        const auto build_dir = temp_root_ / "build";
        const auto source = temp_root_ / "main.cpp";
        write_file(
            source,
            "#include \"local_unused.hpp\"\n"
            "int used = 0;\n"
            "#ifdef _WIN32\n"
            "#include <windows.h>\n"
            "#endif\n"
            "int main() { return used; }\n"
        );
        write_compile_commands(build_dir, source);

        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = build_dir / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 2u);

        const auto direct = std::find_if(
            result.value().suggestions.begin(),
            result.value().suggestions.end(),
            [](const Suggestion& suggestion) {
                return suggestion.application_mode == SuggestionApplicationMode::DirectEdits;
            }
        );
        ASSERT_NE(direct, result.value().suggestions.end());
        EXPECT_EQ(direct->edits.size(), 1u);
        EXPECT_TRUE(direct->is_safe);
        EXPECT_NE(direct->title.find("local_unused.hpp"), std::string::npos);

        const auto advisory = std::find_if(
            result.value().suggestions.begin(),
            result.value().suggestions.end(),
            [](const Suggestion& suggestion) {
                return suggestion.application_mode == SuggestionApplicationMode::Advisory;
            }
        );
        ASSERT_NE(advisory, result.value().suggestions.end());
        EXPECT_FALSE(advisory->is_safe);
        EXPECT_TRUE(advisory->edits.empty());
        EXPECT_NE(advisory->title.find("windows.h"), std::string::npos);
    }

    TEST_F(IncludeSuggesterTest, DowngradesUnusedIncludeRemovalWhenSourceCallsHeaderApi) {
        ASSERT_EQ(setenv("BHA_FAKE_CLANG_TIDY_MODE", "direct", 1), 0);

        const auto build_dir = temp_root_ / "build";
        const auto source = temp_root_ / "spinlock.cc";
        const auto header = temp_root_ / "local_unused.hpp";
        write_file(
            header,
            "#pragma once\n"
            "#include <atomic>\n"
            "namespace demo {\n"
            "void SpinLockWake(std::atomic<int>* word, bool all);\n"
            "}\n"
        );
        write_file(
            source,
            "#include \"local_unused.hpp\"\n"
            "#include <atomic>\n"
            "int main() {\n"
            "  std::atomic<int> word{0};\n"
            "  demo::SpinLockWake(&word, false);\n"
            "  return 0;\n"
            "}\n"
        );
        write_compile_commands(build_dir, source);

        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = build_dir / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const Suggestion& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::IncludeRemoval);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_NE(
            suggestion.auto_apply_blocked_reason->find("callable API"),
            std::string::npos
        );
    }

    TEST_F(IncludeSuggesterTest, DirectIncludeRemovalConsumesTrailingSeparatorBlankLine) {
        ASSERT_EQ(setenv("BHA_FAKE_CLANG_TIDY_MODE", "direct", 1), 0);

        const auto build_dir = temp_root_ / "build";
        const auto source = temp_root_ / "main.cpp";
        const auto header = temp_root_ / "local_unused.hpp";
        write_file(header, "#pragma once\n");
        write_file(
            source,
            "#include \"local_unused.hpp\"\n"
            "\n"
            "int main() { return 0; }\n"
        );
        write_compile_commands(build_dir, source);

        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = build_dir / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const Suggestion& suggestion = result.value().suggestions.front();
        ASSERT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_EQ(suggestion.edits.front().start_line, 0u);
        EXPECT_EQ(suggestion.edits.front().end_line, 2u);
    }

    TEST_F(IncludeSuggesterTest, DowngradesProtectedIncludeRemovalsToAdvisory) {
        ASSERT_EQ(setenv("BHA_FAKE_CLANG_TIDY_MODE", "protected", 1), 0);

        const auto build_dir = temp_root_ / "build";
        const auto source = temp_root_ / "blob_contents.cc";
        const auto header = temp_root_ / "port" / "malloc.h";
        write_file(header, "#pragma once\n");
        write_file(
            source,
            "#include \"port/malloc.h\"\n"
            "int main() { return 0; }\n"
        );
        write_compile_commands(build_dir, source);

        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = build_dir / "compile_commands.json";
        options.protected_include_patterns = {R"((^|.*/)port/)"};
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const Suggestion& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::IncludeRemoval);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_NE(
            suggestion.auto_apply_blocked_reason->find("protected include policy"),
            std::string::npos
        );
    }

    TEST_F(IncludeSuggesterTest, DowngradesPrimarySelfHeaderRemovalToAdvisory) {
        ASSERT_EQ(setenv("BHA_FAKE_CLANG_TIDY_MODE", "self", 1), 0);

        const auto build_dir = temp_root_ / "build";
        const auto source = temp_root_ / "widget.cc";
        const auto header = temp_root_ / "widget.h";
        write_file(header, "#pragma once\nint widget_value();\n");
        write_file(
            source,
            "#include \"widget.h\"\n"
            "int widget_value() { return 7; }\n"
        );
        write_compile_commands(build_dir, source);

        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = build_dir / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const Suggestion& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::IncludeRemoval);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_NE(
            suggestion.auto_apply_blocked_reason->find("primary self-header include"),
            std::string::npos
        );
    }
}
