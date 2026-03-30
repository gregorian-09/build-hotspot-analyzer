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

    TEST_F(TemplateSuggesterTest, EmitsAdvisoryForTemplatesWithLambdaClosureArguments) {
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
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::ExplicitTemplate);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_NE(suggestion.auto_apply_blocked_reason->find("lambda closure type"), std::string::npos);
    }

    TEST_F(TemplateSuggesterTest, TreatsFunctionTypeTemplateArgumentsAsClassTemplates) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(45);

        const auto header_path = temp_root_ / "include" / "function_ref.hpp";
        const auto source_a = temp_root_ / "src" / "a.cpp";
        const auto source_b = temp_root_ / "src" / "b.cpp";

        write_file(
            header_path,
            "#pragma once\n"
            "namespace absl {\n"
            "template <typename Sig>\n"
            "class FunctionRef {};\n"
            "}  // namespace absl\n"
        );
        write_file(source_a, "#include \"../include/function_ref.hpp\"\n");
        write_file(source_b, "#include \"../include/function_ref.hpp\"\n");

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name = "absl::FunctionRef<void (int, absl::FunctionRef<void (int &)>)>";
        tmpl.full_signature = tmpl.name;
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
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        ASSERT_FALSE(suggestion.edits.empty());

        bool has_class_extern = false;
        for (const auto& edit : suggestion.edits) {
            if (edit.new_text.find("extern template class absl::FunctionRef<void (int, absl::FunctionRef<void (int &)>)>;") != std::string::npos) {
                has_class_extern = true;
            }
        }
        EXPECT_TRUE(has_class_extern);
    }

    TEST_F(TemplateSuggesterTest, KeepsQualifiedNestedAbseilTypesInFunctionRefInstantiations) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(45);

        const auto header_path = temp_root_ / "include" / "hash.h";
        const auto source_a = temp_root_ / "src" / "a.cpp";
        const auto source_b = temp_root_ / "src" / "b.cpp";

        write_file(
            header_path,
            "#pragma once\n"
            "namespace absl {\n"
            "class HashState {};\n"
            "template <typename Sig>\n"
            "class FunctionRef {};\n"
            "}  // namespace absl\n"
        );
        write_file(source_a, "#include \"../include/hash.h\"\n");
        write_file(source_b, "#include \"../include/hash.h\"\n");

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name =
            "absl::FunctionRef<void (absl::HashState, absl::FunctionRef<void (absl::HashState &)>)>";
        tmpl.full_signature = tmpl.name;
        tmpl.total_time = std::chrono::milliseconds(450);
        tmpl.instantiation_count = 9;
        tmpl.files_using = {source_a.string(), source_b.string()};
        tmpl.locations.push_back(SourceLocation{header_path, 4, 1});
        analysis.templates.templates.push_back(tmpl);

        SuggesterOptions options;
        options.heuristics.templates.min_instantiation_count = 2;
        options.heuristics.templates.min_total_time = std::chrono::milliseconds(1);
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        ASSERT_FALSE(suggestion.edits.empty());

        bool has_expected_extern = false;
        for (const auto& edit : suggestion.edits) {
            if (edit.new_text.find(
                    "extern template class absl::FunctionRef<void (absl::HashState, absl::FunctionRef<void (absl::HashState &)>)>;")
                != std::string::npos) {
                has_expected_extern = true;
            }
        }
        EXPECT_TRUE(has_expected_extern);
    }

    TEST_F(TemplateSuggesterTest, ChoosesHeaderThatExposesDependentTypesForExternTemplate) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(45);

        const auto function_ref_header = temp_root_ / "absl" / "functional" / "function_ref.h";
        const auto hash_header = temp_root_ / "absl" / "hash" / "hash.h";
        const auto source_a = temp_root_ / "src" / "a.cpp";
        const auto source_b = temp_root_ / "src" / "b.cpp";

        write_file(
            function_ref_header,
            "#pragma once\n"
            "namespace absl {\n"
            "template <typename Sig>\n"
            "class FunctionRef {};\n"
            "}  // namespace absl\n"
        );
        write_file(
            hash_header,
            "#pragma once\n"
            "#include \"../functional/function_ref.h\"\n"
            "namespace absl {\n"
            "class HashState {};\n"
            "}  // namespace absl\n"
        );
        write_file(source_a, "#include \"../absl/hash/hash.h\"\n");
        write_file(source_b, "#include \"../absl/hash/hash.h\"\n");

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name =
            "absl::FunctionRef<void (absl::HashState, absl::FunctionRef<void (absl::HashState &)>)>";
        tmpl.full_signature = tmpl.name;
        tmpl.total_time = std::chrono::milliseconds(450);
        tmpl.instantiation_count = 9;
        tmpl.files_using = {source_a.string(), source_b.string()};
        tmpl.locations.push_back(SourceLocation{function_ref_header, 3, 1});
        analysis.templates.templates.push_back(tmpl);

        SuggesterOptions options;
        options.heuristics.templates.min_instantiation_count = 2;
        options.heuristics.templates.min_total_time = std::chrono::milliseconds(1);
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        EXPECT_FALSE(suggestion.edits.empty());

        bool has_hash_header_extern = false;
        bool instantiation_uses_existing_source = false;
        bool created_new_instantiation_tu = false;
        for (const auto& edit : suggestion.edits) {
            if (edit.file == hash_header &&
                edit.new_text.find(
                    "extern template class absl::FunctionRef<void (absl::HashState, absl::FunctionRef<void (absl::HashState &)>)>;")
                    != std::string::npos) {
                has_hash_header_extern = true;
            }
            if (edit.file == source_a &&
                edit.new_text.find(
                    "template class absl::FunctionRef<void (absl::HashState, absl::FunctionRef<void (absl::HashState &)>)>;")
                    != std::string::npos) {
                instantiation_uses_existing_source = true;
            }
            if (edit.file.filename() == "template_instantiations.cpp") {
                created_new_instantiation_tu = true;
            }
        }
        EXPECT_TRUE(has_hash_header_extern);
        EXPECT_TRUE(instantiation_uses_existing_source);
        EXPECT_FALSE(created_new_instantiation_tu);
    }

    TEST_F(TemplateSuggesterTest, DowngradesWhenNoCompiledSourceOrBuildOwnershipIsProvable) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(45);

        const auto header_path = temp_root_ / "include" / "my_template.hpp";
        const auto wrapper_header = temp_root_ / "include" / "api.hpp";
        const auto wrapper_header_b = temp_root_ / "include" / "api_extra.hpp";
        const auto cmake_path = temp_root_ / "CMakeLists.txt";

        write_file(
            header_path,
            "#pragma once\n"
            "template<typename T>\n"
            "struct MyContainer {\n"
            "    T value{};\n"
            "};\n"
        );
        write_file(
            wrapper_header,
            "#pragma once\n"
            "#include \"my_template.hpp\"\n"
        );
        write_file(
            wrapper_header_b,
            "#pragma once\n"
            "#include \"my_template.hpp\"\n"
        );
        write_file(
            cmake_path,
            "custom_cc_library(\n"
            "  NAME my_lib\n"
            "  SRCS impl.cc\n"
            ")\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::TemplateAnalysisResult::TemplateStats tmpl;
        tmpl.name = "MyContainer<int>";
        tmpl.full_signature = tmpl.name;
        tmpl.total_time = std::chrono::milliseconds(500);
        tmpl.instantiation_count = 12;
        tmpl.files_using = {wrapper_header.string(), wrapper_header_b.string()};
        tmpl.locations.push_back(SourceLocation{header_path, 2, 1});
        analysis.templates.templates.push_back(tmpl);

        SuggesterOptions options;
        options.heuristics.templates.min_instantiation_count = 2;
        options.heuristics.templates.min_total_time = std::chrono::milliseconds(1);
        const SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_NE(suggestion.auto_apply_blocked_reason->find("No existing compiled source file"), std::string::npos);
    }
}
