//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/template_suggester.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ranges>

namespace bha::suggestions
{
    class TemplateSuggesterTest : public testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<TemplateSuggester>();
            const auto unique_suffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ) + "-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
            temp_root_ = std::filesystem::temp_directory_path() / ("bha-template-suggester-test-" + unique_suffix);
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

        std::unique_ptr<TemplateSuggester> suggester_;
        std::filesystem::path temp_root_;
    };

    TEST_F(TemplateSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "TemplateSuggester");
    }

    TEST_F(TemplateSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(TemplateSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::ExplicitTemplate);
    }

    TEST_F(TemplateSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options, {}};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(TemplateSuggesterTest, SuggestsForExpensiveTemplate) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);

        const auto header_path = temp_root_ / "include" / "my_template.hpp";
        const auto source_a = temp_root_ / "src" / "a.cpp";
        const auto source_b = temp_root_ / "src" / "b.cpp";

        write_file(
            header_path,
            "#pragma once\n"
            "template<typename T>\n"
            "struct MyContainer {\n"
            "    T value{};\n"
            "};\n"
        );
        write_file(source_a, "#include \"../include/my_template.hpp\"\n");
        write_file(source_b, "#include \"../include/my_template.hpp\"\n");

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name = "MyContainer<int>";
        tmpl.full_signature = "MyContainer<int>";
        tmpl.total_time = std::chrono::milliseconds(500);
        tmpl.instantiation_count = 20;
        tmpl.files_using = {source_a.string(), source_b.string()};
        tmpl.locations.push_back(SourceLocation{header_path, 2, 1});
        analysis.templates.templates.push_back(tmpl);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().suggestions.size(), 1u);

        if (!result.value().suggestions.empty()) {
            const auto& suggestion = result.value().suggestions[0];
            EXPECT_EQ(suggestion.type, SuggestionType::ExplicitTemplate);
            EXPECT_TRUE(suggestion.is_safe);
            EXPECT_GT(suggestion.estimated_savings.count(), 0);
            EXPECT_FALSE(suggestion.edits.empty());
        }
    }

    TEST_F(TemplateSuggesterTest, SkipsStdTemplates) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name = "std::vector<int>";
        tmpl.total_time = std::chrono::milliseconds(500);
        tmpl.instantiation_count = 100;
        analysis.templates.templates.push_back(tmpl);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(TemplateSuggesterTest, SkipsLowCount) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name = "RareTemplate<double>";
        tmpl.total_time = std::chrono::milliseconds(500);
        tmpl.instantiation_count = 2;
        analysis.templates.templates.push_back(tmpl);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(TemplateSuggesterTest, SuggestsForFunctionTemplateInstantiation) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(45);

        const auto header_path = temp_root_ / "include" / "functions.hpp";
        const auto source_a = temp_root_ / "src" / "a.cpp";
        const auto source_b = temp_root_ / "src" / "b.cpp";

        write_file(
            header_path,
            "#pragma once\n"
            "namespace fn_probe {\n"
            "template<typename T>\n"
            "T heavy(T value);\n"
            "template<typename T>\n"
            "T heavy(T value) { return value + static_cast<T>(1); }\n"
            "}\n"
        );
        write_file(source_a, "#include \"../include/functions.hpp\"\nint a(){ return fn_probe::heavy<int>(1); }\n");
        write_file(source_b, "#include \"../include/functions.hpp\"\nint b(){ return fn_probe::heavy<int>(2); }\n");

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name = "int fn_probe::heavy<int>(int)";
        tmpl.full_signature = "int fn_probe::heavy<int>(int)";
        tmpl.total_time = std::chrono::milliseconds(450);
        tmpl.instantiation_count = 9;
        tmpl.files_using = {source_a.string(), source_b.string()};
        tmpl.locations.push_back(SourceLocation{header_path, 3, 1});
        analysis.templates.templates.push_back(tmpl);

        SuggesterOptions options;
        options.heuristics.templates.min_instantiation_count = 2;
        options.heuristics.templates.min_total_time = std::chrono::milliseconds(1);
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        const auto it = std::ranges::find_if(
            result.value().suggestions,
            [](const Suggestion& suggestion) {
                return suggestion.type == SuggestionType::ExplicitTemplate;
            }
        );
        ASSERT_NE(it, result.value().suggestions.end());

        bool has_extern = false;
        bool has_explicit = false;
        for (const auto& edit : it->edits) {
            if (edit.new_text.find("extern template int fn_probe::heavy<int>(int);") != std::string::npos) {
                has_extern = true;
            }
            if (edit.new_text.find("template int fn_probe::heavy<int>(int);") != std::string::npos) {
                has_explicit = true;
            }
        }
        EXPECT_TRUE(has_extern);
        EXPECT_TRUE(has_explicit);
    }

    TEST_F(TemplateSuggesterTest, SkipsTemplatesWithLambdaClosureArguments) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(45);

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name =
            "absl::StringResizeAndOverwrite<std::basic_string<char>, "
            "(lambda at /tmp/absl/strings/str_cat.h:476:29)>";
        tmpl.full_signature = tmpl.name;
        tmpl.total_time = std::chrono::milliseconds(450);
        tmpl.instantiation_count = 9;
        tmpl.files_using = {
            (temp_root_ / "src" / "a.cpp").string(),
            (temp_root_ / "src" / "b.cpp").string()
        };
        analysis.templates.templates.push_back(tmpl);

        SuggesterOptions options;
        options.heuristics.templates.min_instantiation_count = 2;
        options.heuristics.templates.min_total_time = std::chrono::milliseconds(1);
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }
}
