#include "bha/suggestions/template_semantic_index.hpp"

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
            EXPECT_EQ(match->source_file, source);
        }

    }  // namespace
}  // namespace bha::suggestions
