//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/include_suggester.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::suggestions
{
    class IncludeSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<IncludeSuggester>();
            temp_root_ = std::filesystem::temp_directory_path() / "bha-include-suggester-test";
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

        std::unique_ptr<IncludeSuggester> suggester_;
        std::filesystem::path temp_root_;
    };

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

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, temp_root_};

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
        BuildTrace trace;

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

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, temp_root_};

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
}
