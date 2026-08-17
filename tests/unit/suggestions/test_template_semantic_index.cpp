#include "bha/suggestions/template_semantic_index.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ranges>

namespace bha::suggestions {
    namespace {

        class TemplateSemanticIndexTest : public ::testing::Test {
        protected:
            void SetUp() override {
                root_ = std::filesystem::temp_directory_path() / "bha-template-semantic-index-test";
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
                root_ / ".bha" / "template-semantic-index-v1.json"
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

    }  // namespace
}  // namespace bha::suggestions
