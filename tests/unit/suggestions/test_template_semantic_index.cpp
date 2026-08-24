#include "bha/suggestions/template_semantic_index.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ranges>

namespace bha::suggestions {
    namespace {

        class TemplateSemanticIndexTest : public ::testing::Test {
        protected:
            void SetUp() override {
                root_ = std::filesystem::temp_directory_path() /
                    ("bha-template-semantic-index-test-" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()
                    ));
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
                std::filesystem::create_directories(root_ / "src", ec);
            }

            void TearDown() override {
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
            }

            std::filesystem::path root_;
        };

        TEST_F(TemplateSemanticIndexTest, ReplaysCompileCommandAndIndexesExplicitClassInstantiation) {
            const auto source = root_ / "src" / "main.cpp";
            std::ofstream(source)
                << "template <typename T> struct Box { T value{}; };\n"
                << "using BoxAlias = Box<int>;\n"
                << "Box<int> make_box();\n"
                << "Box<int>* box_pointer = nullptr;\n"
                << "constexpr auto box_size = sizeof(Box<int>);\n"
                << "void destroy_box(Box<int>* value) { delete value; }\n"
                << "template struct Box<int>;\n";

            const auto database = root_ / "compile_commands.json";
            std::ofstream(database)
                << "[{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/main.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/main.cpp\"]}]";

            ProjectIndex project_index(root_, database);
            TemplateSemanticIndex index(project_index);
            index.build();

            if (index.status() == TemplateSemanticStatus::Unavailable) {
                GTEST_SKIP() << index.diagnostic();
            }

            ASSERT_EQ(index.status(), TemplateSemanticStatus::Parsed) << index.diagnostic();
            const auto match = std::ranges::find_if(
                index.records(),
                [](const auto& record) {
                    return record.template_name == "Box" &&
                           record.specialization.find("Box<int") != std::string::npos;
                }
            );
            ASSERT_NE(match, index.records().end());
            EXPECT_TRUE(match->complete_definition);
            EXPECT_TRUE(match->has_explicit_instantiation);
            EXPECT_EQ(match->declaration_kind, "class");
            EXPECT_EQ(match->canonical_extern_declaration, "extern template class Box<int>;");
            EXPECT_EQ(match->canonical_explicit_definition, "template class Box<int>;");
            EXPECT_GT(match->declaration_line, 0u);
            EXPECT_TRUE(match->has_external_linkage);
            EXPECT_TRUE(match->has_single_explicit_definition);
            EXPECT_FALSE(match->has_dependent_arguments);
            EXPECT_FALSE(match->has_unsupported_scope);
            EXPECT_FALSE(match->use_files.empty());
            ASSERT_FALSE(match->uses.empty());
            EXPECT_TRUE(std::ranges::any_of(match->uses, [](const auto& use) {
                return use.kind == "function-return" && use.requires_complete_type;
            }));
            EXPECT_TRUE(std::ranges::any_of(match->uses, [](const auto& use) {
                return use.kind == "variable-declaration" && !use.requires_complete_type;
            }));
            EXPECT_TRUE(std::ranges::any_of(match->uses, [](const auto& use) {
                return use.kind == "type-trait" && use.requires_complete_type;
            }));
            EXPECT_TRUE(std::ranges::any_of(match->uses, [](const auto& use) {
                return use.kind == "delete-expression" && use.requires_complete_type;
            }));
            EXPECT_EQ(match->source_file, source);
            EXPECT_EQ(index.find_exact(match->specialization), &*match);
            EXPECT_EQ(index.find_exact("Box<double>"), nullptr);

            EXPECT_TRUE(std::filesystem::is_regular_file(
                root_ / ".bha" / "template-semantic-index-v3.json"
            ));
            TemplateSemanticIndex cached_index(project_index);
            cached_index.build();
            ASSERT_EQ(cached_index.status(), TemplateSemanticStatus::Parsed);
            ASSERT_EQ(cached_index.records().size(), index.records().size());
            EXPECT_EQ(cached_index.find_exact(match->specialization)->specialization, match->specialization);
        }

        TEST_F(TemplateSemanticIndexTest, ReportsMissingCompilationDatabase) {
            ProjectIndex project_index(root_);
            TemplateSemanticIndex index(project_index);

            index.build();

            EXPECT_TRUE(
                index.status() == TemplateSemanticStatus::NoCompilationDatabase ||
                index.status() == TemplateSemanticStatus::Unavailable
            );
            EXPECT_TRUE(index.records().empty());
        }

        TEST_F(TemplateSemanticIndexTest, RejectsMemberTemplateScopeForAutomaticOwnership) {
            const auto source = root_ / "src" / "member.cpp";
            std::ofstream(source)
                << "struct Owner { template <typename T> struct Nested { T value{}; }; };\n"
                << "Owner::Nested<int> nested;\n"
                << "template struct Owner::Nested<int>;\n";
            const auto database = root_ / "compile_commands.json";
            std::ofstream(database)
                << "[{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/member.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/member.cpp\"]}]";

            ProjectIndex project_index(root_, database);
            TemplateSemanticIndex index(project_index);
            index.build();

            if (index.status() == TemplateSemanticStatus::Unavailable) {
                GTEST_SKIP() << index.diagnostic();
            }

            ASSERT_EQ(index.status(), TemplateSemanticStatus::Parsed) << index.diagnostic();
            const auto match = std::ranges::find_if(
                index.records(),
                [](const auto& record) {
                    return record.template_name.find("Nested") != std::string::npos;
                }
            );
            ASSERT_NE(match, index.records().end());
            EXPECT_TRUE(match->has_unsupported_scope);
        }

        TEST_F(TemplateSemanticIndexTest, RejectsDependentUseContextForAutomaticOwnership) {
            const auto source = root_ / "src" / "dependent.cpp";
            std::ofstream(source)
                << "template <typename T> struct Box { T value{}; };\n"
                << "template <typename U> struct Holder { Box<int> value; };\n"
                << "Box<int> make_box();\n"
                << "template struct Box<int>;\n";

            const auto database = root_ / "compile_commands.json";
            std::ofstream(database)
                << "[{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/dependent.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/dependent.cpp\"]}]";

            ProjectIndex project_index(root_, database);
            TemplateSemanticIndex index(project_index);
            index.build();

            if (index.status() == TemplateSemanticStatus::Unavailable) {
                GTEST_SKIP() << index.diagnostic();
            }

            ASSERT_EQ(index.status(), TemplateSemanticStatus::Parsed) << index.diagnostic();
            const auto* match = index.find_exact("Box<int>");
            ASSERT_NE(match, nullptr);
            EXPECT_TRUE(match->has_dependent_use_context);
            EXPECT_TRUE(std::ranges::any_of(match->uses, [](const auto& use) {
                return use.kind == "field-declaration" && use.in_dependent_context;
            }));
        }

        TEST_F(TemplateSemanticIndexTest, RejectsDuplicateExplicitInstantiationOwners) {
            const auto first = root_ / "src" / "one.cpp";
            const auto second = root_ / "src" / "two.cpp";
            const auto source =
                "template <typename T> struct Box { T value{}; };\n"
                "template struct Box<int>;\n";
            std::ofstream(first) << source;
            std::ofstream(second) << source;

            const auto database = root_ / "compile_commands.json";
            std::ofstream(database)
                << "[{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/one.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/one.cpp\"]},"
                << "{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/two.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/two.cpp\"]}]";

            ProjectIndex project_index(root_, database);
            TemplateSemanticIndex index(project_index);
            index.build();

            if (index.status() == TemplateSemanticStatus::Unavailable) {
                GTEST_SKIP() << index.diagnostic();
            }

            ASSERT_EQ(index.status(), TemplateSemanticStatus::Parsed) << index.diagnostic();
            const auto* match = index.find_exact("Box<int>");
            ASSERT_NE(match, nullptr);
            EXPECT_EQ(match->explicit_definition_files.size(), 2u);
            EXPECT_FALSE(match->has_single_explicit_definition);
        }

        TEST_F(TemplateSemanticIndexTest, RejectsDeclarationIdentityConflictAcrossConfigurations) {
            const auto header = root_ / "box.hpp";
            std::ofstream(header)
                << "#ifdef BHA_FUNCTION_FORM\n"
                << "template <typename T> int Box(T value) { return value; }\n"
                << "#else\n"
                << "template <typename T> struct Box { T value{}; };\n"
                << "#endif\n";

            const auto class_source = root_ / "src" / "class.cpp";
            std::ofstream(class_source)
                << "#include \"../box.hpp\"\n"
                << "template struct Box<int>;\n";
            const auto function_source = root_ / "src" / "function.cpp";
            std::ofstream(function_source)
                << "#include \"../box.hpp\"\n"
                << "template int Box<int>(int);\n";

            const auto database = root_ / "compile_commands.json";
            std::ofstream(database)
                << "[{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/class.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/class.cpp\"]},"
                << "{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/function.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-DBHA_FUNCTION_FORM\","
                << "\"-c\",\"src/function.cpp\"]}]";

            ProjectIndex project_index(root_, database);
            TemplateSemanticIndex index(project_index);
            index.build();

            if (index.status() == TemplateSemanticStatus::Unavailable) {
                GTEST_SKIP() << index.diagnostic();
            }

            ASSERT_EQ(index.status(), TemplateSemanticStatus::Parsed) << index.diagnostic();
            const auto* match = index.find_exact("Box<int>");
            ASSERT_NE(match, nullptr);
            EXPECT_TRUE(match->has_declaration_identity_conflict);
        }

        TEST_F(TemplateSemanticIndexTest, RejectsTranslationUnitWithClangDiagnostics) {
            const auto source = root_ / "src" / "broken.cpp";
            std::ofstream(source) << "template <typename T> struct Broken { T value; };\n"
                                  << "Broken<int> broken(\n";

            const auto database = root_ / "compile_commands.json";
            std::ofstream(database)
                << "[{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/broken.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/broken.cpp\"]}]";

            ProjectIndex project_index(root_, database);
            TemplateSemanticIndex index(project_index);
            index.build();

            if (index.status() == TemplateSemanticStatus::Unavailable) {
                GTEST_SKIP() << index.diagnostic();
            }

            EXPECT_EQ(index.status(), TemplateSemanticStatus::Failed);
            EXPECT_TRUE(index.records().empty());
            EXPECT_FALSE(index.diagnostic().empty());
        }

        TEST_F(TemplateSemanticIndexTest, RejectsUnsupportedLanguageMode) {
            const auto source = root_ / "src" / "unsupported.cpp";
            std::ofstream(source)
                << "template <typename T> struct Box { T value{}; };\n"
                << "template struct Box<int>;\n";
            const auto database = root_ / "compile_commands.json";
            std::ofstream(database)
                << "[{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/unsupported.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++2b-invalid\",\"-c\",\"src/unsupported.cpp\"]}]";

            ProjectIndex project_index(root_, database);
            TemplateSemanticIndex index(project_index);
            index.build();

            if (index.status() == TemplateSemanticStatus::Unavailable) {
                GTEST_SKIP() << index.diagnostic();
            }

            EXPECT_EQ(index.status(), TemplateSemanticStatus::Failed);
            EXPECT_TRUE(index.records().empty());
            EXPECT_FALSE(index.diagnostic().empty());
        }

        TEST_F(TemplateSemanticIndexTest, ReusesUnchangedTranslationUnitCacheEntries) {
            const auto first = root_ / "src" / "first.cpp";
            const auto second = root_ / "src" / "second.cpp";
            std::ofstream(first)
                << "template <typename T> struct First { T value{}; };\n"
                << "template struct First<int>;\n";
            std::ofstream(second)
                << "template <typename T> struct Second { T value{}; };\n"
                << "template struct Second<double>;\n";
            const auto database = root_ / "compile_commands.json";
            std::ofstream(database)
                << "[{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/first.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/first.cpp\"]},"
                << "{\"directory\":\"" << root_.string() << "\","
                << "\"file\":\"src/second.cpp\","
                << "\"arguments\":[\"clang++\",\"-std=c++20\",\"-c\",\"src/second.cpp\"]}]";

            ProjectIndex project_index(root_, database);
            TemplateSemanticIndex initial(project_index);
            initial.build();
            if (initial.status() == TemplateSemanticStatus::Unavailable) {
                GTEST_SKIP() << initial.diagnostic();
            }
            ASSERT_EQ(initial.status(), TemplateSemanticStatus::Parsed) << initial.diagnostic();
            ASSERT_NE(initial.find_exact("First<int>"), nullptr);
            ASSERT_NE(initial.find_exact("Second<double>"), nullptr);

            std::ofstream(first, std::ios::trunc)
                << "template <typename T> struct First { T value{}; };\n"
                << "template struct First<long long>;\n";

            TemplateSemanticIndex refreshed(project_index);
            refreshed.build();

            ASSERT_EQ(refreshed.status(), TemplateSemanticStatus::Parsed) << refreshed.diagnostic();
            EXPECT_EQ(refreshed.find_exact("First<int>"), nullptr);
            EXPECT_NE(refreshed.find_exact("First<long long>"), nullptr);
            EXPECT_NE(refreshed.find_exact("Second<double>"), nullptr);
        }

    }  // namespace
}  // namespace bha::suggestions
