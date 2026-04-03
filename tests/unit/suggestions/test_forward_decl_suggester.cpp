//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/forward_decl_suggester.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::suggestions
{
    class ForwardDeclSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<ForwardDeclSuggester>();
            const auto unique_suffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ) + "-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
            temp_root_ = std::filesystem::temp_directory_path() / ("bha-forward-decl-test-" + unique_suffix);
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

        std::unique_ptr<ForwardDeclSuggester> suggester_;
        std::filesystem::path temp_root_;
    };

    TEST_F(ForwardDeclSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "ForwardDeclSuggester");
    }

    TEST_F(ForwardDeclSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(ForwardDeclSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::ForwardDeclaration);
    }

    TEST_F(ForwardDeclSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options, {}};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, SuggestsSafeForwardDeclarationEdit) {
        BuildTrace trace;

        const auto heavy_header = temp_root_ / "heavy_widget.h";
        const auto consumer_header = temp_root_ / "consumer.h";
        const auto consumer_source = temp_root_ / "consumer.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class HeavyWidget {\n"
            "public:\n"
            "    void ping();\n"
            "};\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"heavy_widget.h\"\n"
            "class Consumer {\n"
            "public:\n"
            "    HeavyWidget* ptr;\n"
            "    void process(HeavyWidget& ref);\n"
            "};\n"
        );
        write_file(
            consumer_source,
            "#include \"consumer.h\"\n"
            "void Consumer::process(HeavyWidget&) {}\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(100);
        header.inclusion_count = 5;
        header.including_files = 3;
        header.included_by = {consumer_header};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 1;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const auto& suggestion = result.value().suggestions[0];
        EXPECT_EQ(suggestion.type, SuggestionType::ForwardDeclaration);
        EXPECT_TRUE(suggestion.is_safe);
        ASSERT_FALSE(suggestion.edits.empty());
        EXPECT_EQ(suggestion.edits.front().new_text, "\nclass HeavyWidget;\n\n");
        EXPECT_TRUE(std::ranges::any_of(
            suggestion.edits,
            [&consumer_source](const TextEdit& edit) { return edit.file == consumer_source; }
        ));
    }

    TEST_F(ForwardDeclSuggesterTest, SkipsWhenHeaderDereferencesForwardDeclaredPointer) {
        BuildTrace trace;

        const auto heavy_header = temp_root_ / "callback.h";
        const auto consumer_header = temp_root_ / "consumer.h";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class Callback {\n"
            "public:\n"
            "    bool IsVisible() const;\n"
            "};\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"callback.h\"\n"
            "class Consumer {\n"
            "public:\n"
            "    Callback* callback_{};\n"
            "    bool visible() const { return callback_->IsVisible(); }\n"
            "};\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(100);
        header.inclusion_count = 5;
        header.including_files = 1;
        header.included_by = {consumer_header};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 1;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, IgnoresMacroAnnotationsWhenExtractingForwardDeclType) {
        BuildTrace trace;

        const auto heavy_header = temp_root_ / "heavy_widget.h";
        const auto consumer_header = temp_root_ / "consumer.h";
        const auto consumer_source = temp_root_ / "consumer.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class ABSL_LOCKABLE ABSL_ATTRIBUTE_WARN_UNUSED HeavyWidget {\n"
            "public:\n"
            "    void ping();\n"
            "};\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"heavy_widget.h\"\n"
            "class Consumer {\n"
            "public:\n"
            "    HeavyWidget* ptr;\n"
            "};\n"
        );
        write_file(
            consumer_source,
            "#include \"consumer.h\"\n"
            "void use_widget(HeavyWidget* widget) { (void)widget; }\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(100);
        header.inclusion_count = 5;
        header.including_files = 3;
        header.included_by = {consumer_header};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 1;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const auto& suggestion = result.value().suggestions[0];
        ASSERT_FALSE(suggestion.edits.empty());
        EXPECT_EQ(suggestion.edits.front().new_text, "\nclass HeavyWidget;\n\n");
    }

    TEST_F(ForwardDeclSuggesterTest, PreservesNamespaceWrapperMacrosWhenSupportIncludeExists) {
        BuildTrace trace;

        const auto config_header = temp_root_ / "project_config.h";
        const auto heavy_header = temp_root_ / "heavy_widget.h";
        const auto consumer_header = temp_root_ / "consumer.h";
        const auto consumer_source = temp_root_ / "consumer.cpp";

        write_file(
            config_header,
            "#pragma once\n"
            "#define PROJECT_NAMESPACE_BEGIN inline namespace v1 {\n"
            "#define PROJECT_NAMESPACE_END }\n"
        );
        write_file(
            heavy_header,
            "#pragma once\n"
            "#include \"project_config.h\"\n"
            "namespace demo {\n"
            "PROJECT_NAMESPACE_BEGIN\n"
            "namespace detail {\n"
            "class Widget {\n"
            "public:\n"
            "    void ping();\n"
            "};\n"
            "}  // namespace detail\n"
            "PROJECT_NAMESPACE_END\n"
            "}  // namespace demo\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"heavy_widget.h\"\n"
            "class Consumer {\n"
            "public:\n"
            "    demo::detail::Widget* ptr;\n"
            "};\n"
        );
        write_file(
            consumer_source,
            "#include \"consumer.h\"\n"
            "void use_widget(demo::detail::Widget* widget) { (void)widget; }\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(100);
        header.inclusion_count = 5;
        header.including_files = 3;
        header.included_by = {consumer_header};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 1;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const auto& suggestion = result.value().suggestions[0];
        const auto header_edit = std::ranges::find_if(suggestion.edits, [&](const TextEdit& edit) {
            return edit.file == consumer_header;
        });
        ASSERT_NE(header_edit, suggestion.edits.end());
        EXPECT_NE(header_edit->new_text.find("#include \"project_config.h\""), std::string::npos);
        EXPECT_NE(header_edit->new_text.find("PROJECT_NAMESPACE_BEGIN"), std::string::npos);
        EXPECT_NE(header_edit->new_text.find("namespace demo {"), std::string::npos);
        EXPECT_NE(header_edit->new_text.find("namespace detail {"), std::string::npos);
        EXPECT_NE(header_edit->new_text.find("class Widget;"), std::string::npos);
        EXPECT_NE(header_edit->new_text.find("PROJECT_NAMESPACE_END"), std::string::npos);
    }

    TEST_F(ForwardDeclSuggesterTest, DoesNotDuplicateExistingSupportInclude) {
        BuildTrace trace;

        const auto config_header = temp_root_ / "project_config.h";
        const auto heavy_header = temp_root_ / "heavy_widget.h";
        const auto consumer_header = temp_root_ / "consumer.h";
        const auto consumer_source = temp_root_ / "consumer.cpp";

        write_file(
            config_header,
            "#pragma once\n"
            "#define PROJECT_NAMESPACE_BEGIN inline namespace v1 {\n"
            "#define PROJECT_NAMESPACE_END }\n"
        );
        write_file(
            heavy_header,
            "#pragma once\n"
            "#include \"project_config.h\"\n"
            "namespace demo {\n"
            "PROJECT_NAMESPACE_BEGIN\n"
            "class Widget {\n"
            "public:\n"
            "    void ping();\n"
            "};\n"
            "PROJECT_NAMESPACE_END\n"
            "}  // namespace demo\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"project_config.h\"\n"
            "#include \"heavy_widget.h\"\n"
            "class Consumer {\n"
            "public:\n"
            "    demo::Widget* ptr;\n"
            "};\n"
        );
        write_file(
            consumer_source,
            "#include \"consumer.h\"\n"
            "void use_widget(demo::Widget* widget) { (void)widget; }\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(100);
        header.inclusion_count = 5;
        header.including_files = 3;
        header.included_by = {consumer_header};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 1;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const auto& suggestion = result.value().suggestions[0];
        const auto header_edit = std::ranges::find_if(suggestion.edits, [&](const TextEdit& edit) {
            return edit.file == consumer_header;
        });
        ASSERT_NE(header_edit, suggestion.edits.end());
        EXPECT_EQ(header_edit->new_text.find("#include \"project_config.h\""), std::string::npos);
        EXPECT_NE(header_edit->new_text.find("PROJECT_NAMESPACE_BEGIN"), std::string::npos);
        EXPECT_NE(header_edit->new_text.find("class Widget;"), std::string::npos);
    }

    TEST_F(ForwardDeclSuggesterTest, SkipsNamespaceWrapperMacrosWithoutResolvableSupportInclude) {
        BuildTrace trace;

        const auto heavy_header = temp_root_ / "heavy_widget.h";
        const auto consumer_header = temp_root_ / "consumer.h";

        write_file(
            heavy_header,
            "#pragma once\n"
            "#define PROJECT_NAMESPACE_BEGIN inline namespace v1 {\n"
            "#define PROJECT_NAMESPACE_END }\n"
            "namespace demo {\n"
            "PROJECT_NAMESPACE_BEGIN\n"
            "namespace detail {\n"
            "class Widget {};\n"
            "}  // namespace detail\n"
            "PROJECT_NAMESPACE_END\n"
            "}  // namespace demo\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"heavy_widget.h\"\n"
            "class Consumer {\n"
            "public:\n"
            "    demo::detail::Widget* ptr;\n"
            "};\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(100);
        header.inclusion_count = 5;
        header.including_files = 3;
        header.included_by = {consumer_header};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 1;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, SkipsByValueTypeUsage) {
        BuildTrace trace;

        const auto heavy_header = temp_root_ / "heavy_widget.h";
        const auto consumer_header = temp_root_ / "consumer_by_value.h";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class HeavyWidget {};\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"heavy_widget.h\"\n"
            "class ConsumerByValue {\n"
            "    HeavyWidget value;\n"
            "};\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(120);
        header.inclusion_count = 4;
        header.included_by = {consumer_header};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 1;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, SkipsInheritanceUsage) {
        BuildTrace trace;

        const auto base_header = temp_root_ / "base_widget.h";
        const auto consumer_header = temp_root_ / "consumer_inherit.h";

        write_file(
            base_header,
            "#pragma once\n"
            "class BaseWidget {\n"
            "public:\n"
            "    virtual ~BaseWidget() = default;\n"
            "};\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"base_widget.h\"\n"
            "class ConsumerInherit : public BaseWidget {\n"
            "public:\n"
            "    void run();\n"
            "};\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = base_header;
        header.total_parse_time = std::chrono::milliseconds(120);
        header.inclusion_count = 4;
        header.included_by = {consumer_header};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 1;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, SkipsNonHeaders) {
        const BuildTrace trace;

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "source.cpp";
        header.total_parse_time = std::chrono::milliseconds(100);
        header.inclusion_count = 5;
        analysis.dependencies.headers.push_back(header);

        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, FindsHeaderIncluderWhenTraceListsOnlySourceFile) {
        BuildTrace trace;

        const auto heavy_header = temp_root_ / "heavy_widget.h";
        const auto consumer_header = temp_root_ / "consumer.h";
        const auto consumer_source = temp_root_ / "consumer.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "class HeavyWidget {\n"
            "public:\n"
            "    void ping();\n"
            "};\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"heavy_widget.h\"\n"
            "class Consumer {\n"
            "public:\n"
            "    HeavyWidget* ptr;\n"
            "    void process(const HeavyWidget& ref);\n"
            "};\n"
        );
        write_file(
            consumer_source,
            "#include \"consumer.h\"\n"
            "void use(Consumer&) {}\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(120);
        header.inclusion_count = 6;
        header.included_by = {consumer_source};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 1;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.target_file.path, consumer_header);
        ASSERT_FALSE(suggestion.edits.empty());
        EXPECT_EQ(suggestion.edits.front().new_text, "\nclass HeavyWidget;\n\n");
    }

    TEST_F(ForwardDeclSuggesterTest, SuggestsForwardDeclarationForNamespacedType) {
        BuildTrace trace;

        const auto heavy_header = temp_root_ / "widget.hpp";
        const auto consumer_header = temp_root_ / "consumer_ns.hpp";
        const auto consumer_source = temp_root_ / "consumer_ns.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "namespace demo {\n"
            "class Widget {\n"
            "public:\n"
            "    void ping();\n"
            "};\n"
            "}\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"widget.hpp\"\n"
            "namespace demo {\n"
            "class Consumer {\n"
            "public:\n"
            "    Widget* ptr;\n"
            "    void process(const Widget& ref);\n"
            "    void reset(Widget* value);\n"
            "};\n"
            "}\n"
        );
        write_file(
            consumer_source,
            "#include \"consumer_ns.hpp\"\n"
            "namespace demo { void use(Consumer&) {} }\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(120);
        header.inclusion_count = 6;
        header.included_by = {consumer_source};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 3;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        EXPECT_EQ(result.value().suggestions.front().target_file.path, consumer_header);
    }

    TEST_F(ForwardDeclSuggesterTest, AddsIncludeToSourceInSrcDirectoryLayout) {
        BuildTrace trace;

        const auto include_dir = temp_root_ / "include";
        const auto src_dir = temp_root_ / "src";
        const auto heavy_header = include_dir / "widget.hpp";
        const auto consumer_header = include_dir / "consumer.hpp";
        const auto consumer_source = src_dir / "consumer.cpp";

        write_file(
            heavy_header,
            "#pragma once\n"
            "namespace demo {\n"
            "class Widget {\n"
            "public:\n"
            "    void ping();\n"
            "};\n"
            "}\n"
        );
        write_file(
            consumer_header,
            "#pragma once\n"
            "#include \"widget.hpp\"\n"
            "namespace demo {\n"
            "class Consumer {\n"
            "public:\n"
            "    Widget* ptr;\n"
            "    void process(const Widget& ref);\n"
            "    void reset(Widget* value);\n"
            "};\n"
            "}\n"
        );
        write_file(
            consumer_source,
            "#include \"consumer.hpp\"\n"
            "namespace demo {\n"
            "void Consumer::process(const Widget& ref) { ref.ping(); }\n"
            "void Consumer::reset(Widget* value) { (void)value; }\n"
            "}\n"
        );

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = heavy_header;
        header.total_parse_time = std::chrono::milliseconds(150);
        header.inclusion_count = 6;
        header.included_by = {consumer_source};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        options.heuristics.forward_decl.min_usage_sites = 3;
        SuggestionContext context{trace, analysis, options, temp_root_};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);

        const auto& suggestion = result.value().suggestions.front();
        EXPECT_TRUE(std::ranges::any_of(
            suggestion.edits,
            [&consumer_source](const TextEdit& edit) { return edit.file == consumer_source; }
        ));
    }
}
