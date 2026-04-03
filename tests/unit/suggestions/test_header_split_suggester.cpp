//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/header_split_suggester.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>

namespace bha::suggestions
{
    class HeaderSplitSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<HeaderSplitSuggester>();
            const auto unique_suffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ) + "-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
            temp_root_ = std::filesystem::temp_directory_path() / ("bha-header-split-test-" + unique_suffix);
            std::error_code ec;
            std::filesystem::remove_all(temp_root_, ec);
            std::filesystem::create_directories(temp_root_, ec);
        }

        void TearDown() override {
            std::error_code ec;
            std::filesystem::remove_all(temp_root_, ec);
        }

        static void write_file(const std::filesystem::path& path, const std::string& content) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path);
            ASSERT_TRUE(out.good());
            out << content;
        }

        std::unique_ptr<HeaderSplitSuggester> suggester_;
        std::filesystem::path temp_root_;
    };

    TEST_F(HeaderSplitSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "HeaderSplitSuggester");
    }

    TEST_F(HeaderSplitSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(HeaderSplitSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::HeaderSplit);
    }

    TEST_F(HeaderSplitSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options, {}};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(HeaderSplitSuggesterTest, SuggestsForLargeHeader) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "big_header.h";
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 30;
        header.including_files = 15;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().suggestions.size(), 1u);

        if (!result.value().suggestions.empty()) {
            const auto& suggestion = result.value().suggestions[0];
            EXPECT_EQ(suggestion.type, SuggestionType::HeaderSplit);
            EXPECT_FALSE(suggestion.is_safe);
            EXPECT_GT(suggestion.estimated_savings.count(), 0);
            EXPECT_FALSE(suggestion.implementation_steps.empty());
        }
    }

    TEST_F(HeaderSplitSuggesterTest, SkipsSmallHeaders) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "small_header.h";
        header.total_parse_time = std::chrono::milliseconds(50);
        header.inclusion_count = 10;
        header.including_files = 5;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(HeaderSplitSuggesterTest, SkipsRarelyIncludedHeaders) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "rare_header.h";
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 2;
        header.including_files = 2;
        analysis.dependencies.headers.push_back(header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(HeaderSplitSuggesterTest, SkipsAlreadySplitHeaders) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;

        analyzers::DependencyAnalysisResult::HeaderInfo fwd_header;
        fwd_header.path = "types_fwd.h";
        fwd_header.total_parse_time = std::chrono::milliseconds(500);
        fwd_header.including_files = 20;
        analysis.dependencies.headers.push_back(fwd_header);

        analyzers::DependencyAnalysisResult::HeaderInfo types_header;
        types_header.path = "widget_types.h";
        types_header.total_parse_time = std::chrono::milliseconds(500);
        types_header.including_files = 20;
        analysis.dependencies.headers.push_back(types_header);

        analyzers::DependencyAnalysisResult::HeaderInfo decl_header;
        decl_header.path = "api_decl.h";
        decl_header.total_parse_time = std::chrono::milliseconds(500);
        decl_header.including_files = 20;
        analysis.dependencies.headers.push_back(decl_header);

        analyzers::DependencyAnalysisResult::HeaderInfo impl_header;
        impl_header.path = "core_impl.h";
        impl_header.total_parse_time = std::chrono::milliseconds(500);
        impl_header.including_files = 20;
        analysis.dependencies.headers.push_back(impl_header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_EQ(result.value().items_skipped, 4u);
    }

    TEST_F(HeaderSplitSuggesterTest, SkipsNonHeaders) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo source;
        source.path = "source.cpp";
        source.total_parse_time = std::chrono::milliseconds(500);
        source.including_files = 20;
        analysis.dependencies.headers.push_back(source);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(HeaderSplitSuggesterTest, PrioritizesByEstimatedSavings) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);

        analyzers::AnalysisResult analysis;

        analyzers::DependencyAnalysisResult::HeaderInfo small_header;
        small_header.path = "small.h";
        small_header.total_parse_time = std::chrono::milliseconds(300);
        small_header.including_files = 10;
        analysis.dependencies.headers.push_back(small_header);

        analyzers::DependencyAnalysisResult::HeaderInfo big_header;
        big_header.path = "big.h";
        big_header.total_parse_time = std::chrono::milliseconds(600);
        big_header.including_files = 30;
        analysis.dependencies.headers.push_back(big_header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_GE(result.value().suggestions.size(), 2u);

        EXPECT_TRUE(result.value().suggestions[0].estimated_savings >=
                  result.value().suggestions[1].estimated_savings);
    }

    TEST_F(HeaderSplitSuggesterTest, EstimatedSavingsRemainBoundedByAggregateParseTime) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(20);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "huge_header.h";
        header.total_parse_time = std::chrono::seconds(2);
        header.including_files = 200;
        header.inclusion_count = 260;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());
        EXPECT_LT(result.value().suggestions.front().estimated_savings,
                  std::chrono::milliseconds(650));
    }

    TEST_F(HeaderSplitSuggesterTest, CalculatesCorrectPriority) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(120);

        analyzers::AnalysisResult analysis;

        // Critical: > 1000ms, >= 50 includers
        analyzers::DependencyAnalysisResult::HeaderInfo critical_header;
        critical_header.path = "critical.h";
        critical_header.total_parse_time = std::chrono::milliseconds(1500);
        critical_header.including_files = 60;
        analysis.dependencies.headers.push_back(critical_header);

        // High: > 500ms, >= 20 includers
        analyzers::DependencyAnalysisResult::HeaderInfo high_header;
        high_header.path = "high.h";
        high_header.total_parse_time = std::chrono::milliseconds(600);
        high_header.including_files = 25;
        analysis.dependencies.headers.push_back(high_header);

        // Medium: > 200ms, >= 10 includers
        analyzers::DependencyAnalysisResult::HeaderInfo medium_header;
        medium_header.path = "medium.h";
        medium_header.total_parse_time = std::chrono::milliseconds(300);
        medium_header.including_files = 12;
        analysis.dependencies.headers.push_back(medium_header);

        // Low: default
        analyzers::DependencyAnalysisResult::HeaderInfo low_header;
        low_header.path = "low.h";
        low_header.total_parse_time = std::chrono::milliseconds(250);
        low_header.including_files = 6;
        analysis.dependencies.headers.push_back(low_header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_GE(result.value().suggestions.size(), 4u);

        // Find suggestions by id
        std::unordered_map<std::string, Priority> priorities;
        for (const auto& s : result.value().suggestions) {
            if (s.id.find("critical.h") != std::string::npos) {
                priorities["critical"] = s.priority;
            } else if (s.id.find("high.h") != std::string::npos) {
                priorities["high"] = s.priority;
            } else if (s.id.find("medium.h") != std::string::npos) {
                priorities["medium"] = s.priority;
            } else if (s.id.find("low.h") != std::string::npos) {
                priorities["low"] = s.priority;
            }
        }

        EXPECT_EQ(priorities["critical"], Priority::Critical);
        EXPECT_EQ(priorities["high"], Priority::High);
        EXPECT_EQ(priorities["medium"], Priority::Medium);
        EXPECT_EQ(priorities["low"], Priority::Low);
    }

    TEST_F(HeaderSplitSuggesterTest, GeneratesDirectEditsForForwardDeclSplit) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);

        const auto header_path = temp_root_ / "monolith.hpp";
        write_file(
            header_path,
            "#pragma once\n"
            "namespace demo {\n"
            "struct Config {\n"
            "    int value;\n"
            "};\n"
            "class Widget {\n"
            "public:\n"
            "    void run();\n"
            "};\n"
            "}\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = header_path;
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 20;
        header.including_files = 10;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        const auto& suggestion = result.value().suggestions.front();
        EXPECT_TRUE(suggestion.is_safe);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        ASSERT_FALSE(suggestion.edits.empty());
        EXPECT_TRUE(std::ranges::any_of(suggestion.edits, [&](const TextEdit& edit) {
            return edit.file.filename() == "monolith_fwd.hpp" &&
                   edit.new_text.find("class Widget;") != std::string::npos;
        }));
        EXPECT_TRUE(std::ranges::any_of(suggestion.edits, [&](const TextEdit& edit) {
            return edit.file == header_path &&
                   edit.new_text.find("#include \"monolith_fwd.hpp\"") != std::string::npos;
        }));
    }

    TEST_F(HeaderSplitSuggesterTest, SkipsHeaderWithExistingForwardCompanionInclude) {
        BuildTrace trace;

        const auto header_path = temp_root_ / "monolith.hpp";
        const auto fwd_path = temp_root_ / "monolith_fwd.hpp";
        write_file(
            header_path,
            "#pragma once\n"
            "#include \"monolith_fwd.hpp\"\n"
            "namespace demo {\n"
            "class Widget {\n"
            "public:\n"
            "    void run();\n"
            "};\n"
            "}\n"
        );
        write_file(
            fwd_path,
            "#pragma once\n"
            "namespace demo {\n"
            "class Widget;\n"
            "}\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = header_path;
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 20;
        header.including_files = 10;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(HeaderSplitSuggesterTest, IgnoresMacroAnnotationsWhenGeneratingForwardHeader) {
        BuildTrace trace;

        const auto header_path = temp_root_ / "spinlock.hpp";
        write_file(
            header_path,
            "#pragma once\n"
            "namespace demo {\n"
            "class ABSL_LOCKABLE ABSL_ATTRIBUTE_WARN_UNUSED SpinLock {\n"
            "public:\n"
            "    void lock();\n"
            "};\n"
            "}\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = header_path;
        header.total_parse_time = std::chrono::milliseconds(600);
        header.inclusion_count = 24;
        header.including_files = 12;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        const auto& suggestion = result.value().suggestions.front();
        ASSERT_FALSE(suggestion.edits.empty());
        EXPECT_TRUE(std::ranges::any_of(suggestion.edits, [&](const TextEdit& edit) {
            return edit.file.filename() == "spinlock_fwd.hpp" &&
                   edit.new_text.find("class SpinLock;") != std::string::npos &&
                   edit.new_text.find("class ABSL_LOCKABLE;") == std::string::npos;
        }));
    }

    TEST_F(HeaderSplitSuggesterTest, PreservesNamespaceWrapperMacrosWhenGeneratingForwardHeader) {
        BuildTrace trace;

        const auto config_header = temp_root_ / "project_config.h";
        const auto header_path = temp_root_ / "widget.hpp";
        write_file(
            config_header,
            "#pragma once\n"
            "#define PROJECT_NAMESPACE_BEGIN inline namespace v1 {\n"
            "#define PROJECT_NAMESPACE_END }\n"
        );
        write_file(
            header_path,
            "#pragma once\n"
            "#include \"project_config.h\"\n"
            "namespace demo {\n"
            "PROJECT_NAMESPACE_BEGIN\n"
            "namespace detail {\n"
            "class Widget {\n"
            "public:\n"
            "    void run();\n"
            "};\n"
            "}  // namespace detail\n"
            "PROJECT_NAMESPACE_END\n"
            "}  // namespace demo\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = header_path;
        header.total_parse_time = std::chrono::milliseconds(600);
        header.inclusion_count = 24;
        header.including_files = 12;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        const auto& suggestion = result.value().suggestions.front();
        const auto create_edit = std::ranges::find_if(suggestion.edits, [&](const TextEdit& edit) {
            return edit.file.filename() == "widget_fwd.hpp";
        });
        ASSERT_NE(create_edit, suggestion.edits.end());
        EXPECT_NE(create_edit->new_text.find("#ifdef __cplusplus"), std::string::npos);
        EXPECT_NE(create_edit->new_text.find("#include \"project_config.h\""), std::string::npos);
        EXPECT_NE(create_edit->new_text.find("namespace demo {"), std::string::npos);
        EXPECT_NE(create_edit->new_text.find("PROJECT_NAMESPACE_BEGIN"), std::string::npos);
        EXPECT_NE(create_edit->new_text.find("namespace detail {"), std::string::npos);
        EXPECT_NE(create_edit->new_text.find("class Widget;"), std::string::npos);
        EXPECT_NE(create_edit->new_text.find("PROJECT_NAMESPACE_END"), std::string::npos);
        EXPECT_NE(create_edit->new_text.find("#endif  // __cplusplus"), std::string::npos);
    }

    TEST_F(HeaderSplitSuggesterTest, DowngradesMacroWrappedForwardHeaderWithoutResolvableSupportInclude) {
        BuildTrace trace;

        const auto header_path = temp_root_ / "widget.hpp";
        write_file(
            header_path,
            "#pragma once\n"
            "#define PROJECT_NAMESPACE_BEGIN inline namespace v1 {\n"
            "#define PROJECT_NAMESPACE_END }\n"
            "namespace demo {\n"
            "PROJECT_NAMESPACE_BEGIN\n"
            "namespace detail {\n"
            "class Widget {\n"
            "public:\n"
            "    void run();\n"
            "};\n"
            "}  // namespace detail\n"
            "PROJECT_NAMESPACE_END\n"
            "}  // namespace demo\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = header_path;
        header.total_parse_time = std::chrono::milliseconds(600);
        header.inclusion_count = 24;
        header.including_files = 12;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        const auto& suggestion = result.value().suggestions.front();
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
    }

    TEST_F(HeaderSplitSuggesterTest, FindsOnlyPrimaryIncludeBlockForInsertion) {
        const auto header_path = temp_root_ / "feature_header.hpp";
        write_file(
            header_path,
            "#pragma once\n"
            "\n"
            "#include \"base.hpp\"\n"
            "\n"
            "#if defined(ENABLE_FEATURE)\n"
            "#include \"feature.hpp\"\n"
            "#endif\n"
            "\n"
            "namespace demo {\n"
            "class Widget;\n"
            "}\n"
        );

        const std::optional<std::size_t> insertion_line = find_include_insertion_line(header_path);
        ASSERT_TRUE(insertion_line.has_value());
        EXPECT_EQ(*insertion_line, 2u);
    }

    TEST_F(HeaderSplitSuggesterTest, FallsBackToHeaderGuardForPreferredIncludeInsertion) {
        const auto header_path = temp_root_ / "guarded_header.hpp";
        write_file(
            header_path,
            "// Copyright example\n"
            "// Header banner\n"
            "\n"
            "#ifndef DEMO_GUARDED_HEADER_HPP_\n"
            "#define DEMO_GUARDED_HEADER_HPP_\n"
            "\n"
            "#ifdef __cplusplus\n"
            "namespace demo {\n"
            "class Widget;\n"
            "}  // namespace demo\n"
            "#endif\n"
            "\n"
            "#endif  // DEMO_GUARDED_HEADER_HPP_\n"
        );

        const std::optional<std::size_t> insertion_line = find_preferred_include_insertion_line(header_path);
        ASSERT_TRUE(insertion_line.has_value());
        EXPECT_EQ(*insertion_line, 4u);

        const auto insertion = make_preferred_include_insertion_edit(header_path, "#include \"widget_fwd.h\"");
        EXPECT_EQ(insertion.edit.start_line, 5u);
        EXPECT_EQ(insertion.edit.new_text, "\n#include \"widget_fwd.h\"\n");
        EXPECT_EQ(insertion.inserted_line_one_based, 7u);
    }

    TEST_F(HeaderSplitSuggesterTest, FallsBackToLeadingPreambleForPreferredIncludeInsertion) {
        const auto header_path = temp_root_ / "banner_only.hpp";
        write_file(
            header_path,
            "// Copyright example\n"
            "// Header banner\n"
            "\n"
            "namespace demo {\n"
            "class Widget;\n"
            "}  // namespace demo\n"
        );

        const std::optional<std::size_t> insertion_line = find_leading_preamble_line(header_path);
        ASSERT_TRUE(insertion_line.has_value());
        EXPECT_EQ(*insertion_line, 2u);

        const auto insertion = make_preferred_include_insertion_edit(header_path, "#include \"widget_fwd.h\"");
        EXPECT_EQ(insertion.edit.start_line, 3u);
        EXPECT_EQ(insertion.edit.new_text, "\n#include \"widget_fwd.h\"\n");
        EXPECT_EQ(insertion.inserted_line_one_based, 5u);
    }

}
