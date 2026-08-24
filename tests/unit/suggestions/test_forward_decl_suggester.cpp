#include "bha/suggestions/forward_decl_semantic_index.hpp"
#include "bha/suggestions/forward_decl_suggester.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <ranges>

namespace bha::suggestions {
    namespace {
        class ForwardDeclSuggesterTest : public ::testing::Test {
        protected:
            void SetUp() override {
                root_ = std::filesystem::temp_directory_path() / "bha-forward-decl-suggester-test";
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
                std::filesystem::create_directories(root_ / "include", ec);
                std::filesystem::create_directories(root_ / "src", ec);
            }

            void TearDown() override {
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
            }

            std::filesystem::path root_;
        };

        void write_compile_commands(
            const std::filesystem::path& root,
            const std::filesystem::path& source
        ) {
            std::ofstream(root / "compile_commands.json")
                << "[{\"directory\":\"" << root.string() << "\","
                << "\"file\":\"src/use.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-I" << (root / "include").string()
                << "\",\"-c\",\"" << (root / "src/use.cpp").string() << "\"]}]";
            (void)source;
        }

        void write_nested_compile_commands(
            const std::filesystem::path& root
        ) {
            std::error_code ec;
            std::filesystem::create_directories(root / "build", ec);
            std::ofstream(root / "build" / "compile_commands.json")
                << "[{\"directory\":\".\","
                << "\"file\":\"../src/use.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-I../include\",\"-c\",\"../src/use.cpp\"]}]";
        }

        void write_conditional_compile_commands(
            const std::filesystem::path& root
        ) {
            std::ofstream(root / "compile_commands.json")
                << "[{\"directory\":\"" << root.string() << "\","
                << "\"file\":\"src/use.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-I"
                << (root / "include").string() << "\",\"-c\",\""
                << (root / "src/use.cpp").string() << "\"]},"
                << "{\"directory\":\"" << root.string() << "\","
                << "\"file\":\"src/other.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-DBHA_CLASS_FORM\",\"-I"
                << (root / "include").string() << "\",\"-c\",\""
                << (root / "src/other.cpp").string() << "\"]}]";
        }

        analyzers::AnalysisResult dependency_analysis(
            const std::filesystem::path& header,
            const std::filesystem::path& source
        ) {
            analyzers::AnalysisResult analysis;
            analyzers::DependencyAnalysisResult::HeaderInfo info;
            info.path = header;
            info.total_parse_time = std::chrono::milliseconds(1000);
            info.inclusion_count = 1;
            info.including_files = 1;
            info.included_by = {source};
            analysis.dependencies.headers.push_back(std::move(info));
            return analysis;
        }
    }

    TEST(ForwardDeclSuggesterContractTest, ReportsIdentity) {
        ForwardDeclSuggester suggester;
        EXPECT_EQ(suggester.name(), "ForwardDeclSuggester");
        EXPECT_FALSE(suggester.description().empty());
        EXPECT_EQ(suggester.suggestion_type(), SuggestionType::ForwardDeclaration);
    }

    TEST(ForwardDeclSuggesterContractTest, RequiresCompilationDatabase) {
        ForwardDeclSuggester suggester;
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;
        const SuggestionContext context{trace, analysis, options, {}};

        const auto result = suggester.suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "forward_decl.semantic.index_required");
    }

    TEST_F(ForwardDeclSuggesterTest, EmitsOnlyAstBackedForwardDeclarationEdit) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box* make_box(Box& input);\n";
        write_compile_commands(root_, source);

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header_info;
        header_info.path = header;
        header_info.total_parse_time = std::chrono::milliseconds(1000);
        header_info.inclusion_count = 1;
        header_info.including_files = 1;
        header_info.included_by = {source};
        analysis.dependencies.headers.push_back(header_info);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.confidence, 1.0);
        EXPECT_TRUE(suggestion.is_safe);
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_EQ(suggestion.edits.front().file, source);
        EXPECT_NE(suggestion.edits.front().new_text.find("struct Box;"), std::string::npos);
        EXPECT_EQ(suggestion.estimated_savings, Duration::zero());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsAstProvenCompleteTypeUse) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box make_box();\n";
        write_compile_commands(root_, source);

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header_info;
        header_info.path = header;
        header_info.total_parse_time = std::chrono::milliseconds(1000);
        header_info.included_by = {source};
        analysis.dependencies.headers.push_back(header_info);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsAliasUse) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header)
            << "#pragma once\nstruct Box { int value; };\nusing BoxAlias = Box;\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "BoxAlias* make_box(BoxAlias& input);\n";
        write_compile_commands(root_, source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsTemplateDeclaration) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header)
            << "#pragma once\ntemplate <typename T> struct Box { T value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box<int>* make_box(Box<int>& input);\n";
        write_compile_commands(root_, source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsDependentUse) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "template <typename T> struct Holder { Box* value; };\n";
        write_compile_commands(root_, source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsNestedTemplateArgumentUse) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "#include <vector>\n"
            << "std::vector<Box*>* make_boxes();\n";
        write_compile_commands(root_, source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsArrayUse) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box boxes[2];\n";
        write_compile_commands(root_, source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, PreservesInlineNamespaceInDeclaration) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header)
            << "#pragma once\n"
            << "namespace api { inline namespace v2 { struct Box { int value; }; } }\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "api::Box* make_box();\n";
        write_compile_commands(root_, source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& text = result.value().suggestions.front().edits.front().new_text;
        EXPECT_NE(text.find("inline namespace v2"), std::string::npos);
    }

    TEST_F(ForwardDeclSuggesterTest, ReplaysRelativeCompileArgumentsFromNestedDatabase) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box* make_box();\n";
        write_nested_compile_commands(root_);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "build" / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        EXPECT_TRUE(result.value().suggestions.front().is_safe);
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsConflictingDeclarationKindsAcrossCommands) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header)
            << "#pragma once\n"
            << "#ifdef BHA_CLASS_FORM\n"
            << "class Box { public: int value; };\n"
            << "#else\n"
            << "struct Box { int value; };\n"
            << "#endif\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "Box* make_box();\n";
        const auto other = root_ / "src" / "other.cpp";
        std::ofstream(other)
            << "#include \"box.hpp\"\n"
            << "Box* make_other();\n";
        write_conditional_compile_commands(root_);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo info;
        info.path = header;
        info.total_parse_time = std::chrono::milliseconds(1000);
        info.inclusion_count = 2;
        info.including_files = 2;
        info.included_by = {source, other};
        analysis.dependencies.headers.push_back(std::move(info));
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsExpressionCompleteTypeUses) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "#include <typeinfo>\n"
            << "Box* make_box() { Box* value = nullptr; (void)sizeof(*value); "
            << "(void)value->value; (void)static_cast<Box*>(value); (void)typeid(*value); "
            << "return value; }\n";
        write_compile_commands(root_, source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsCStyleObjectCast) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "void cast_box(Box* value) { (void)(Box)*value; }\n";
        write_compile_commands(root_, source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(ForwardDeclSuggesterTest, RejectsThrownObject) {
        const auto header = root_ / "include" / "box.hpp";
        std::ofstream(header) << "#pragma once\nstruct Box { int value; };\n";
        const auto source = root_ / "src" / "use.cpp";
        std::ofstream(source)
            << "#include \"box.hpp\"\n"
            << "[[noreturn]] void throw_box(Box* value) { throw *value; }\n";
        write_compile_commands(root_, source);

        SuggesterOptions options;
        options.compile_commands_path = root_ / "compile_commands.json";
        BuildTrace trace;
        const auto analysis = dependency_analysis(header, source);
        const SuggestionContext context{trace, analysis, options, root_};
        const auto result = ForwardDeclSuggester{}.suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}
