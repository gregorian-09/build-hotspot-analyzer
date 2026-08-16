#include "bha/project_index.hpp"

#include <fstream>
#include <gtest/gtest.h>

namespace bha {
    namespace {

        struct ProjectIndexFixture : ::testing::Test {
            fs::path root;

            void SetUp() override {
                root = fs::temp_directory_path() / "bha_project_index_test";
                std::error_code ec;
                fs::remove_all(root, ec);
                fs::create_directories(root / "src", ec);
                fs::create_directories(root / "include", ec);
                fs::create_directories(root / "build", ec);
                std::ofstream(root / "src" / "main.cpp") << "int main() { return 0; }\n";
                std::ofstream(root / "include" / "api.hpp") << "#pragma once\n";
                std::ofstream(root / "build" / "generated.cpp") << "int generated;\n";
            }

            void TearDown() override {
                std::error_code ec;
                fs::remove_all(root, ec);
            }
        };

        TEST_F(ProjectIndexFixture, IndexesSourcesAndHeadersWithoutBuildOutput) {
            ProjectIndex index(root);
            const auto sources = index.files(ProjectFileKind::Source);
            const auto headers = index.files(ProjectFileKind::Header);
            ASSERT_EQ(sources.size(), 1u);
            ASSERT_EQ(headers.size(), 1u);
            EXPECT_EQ(sources.front().filename(), "main.cpp");
            EXPECT_EQ(headers.front().filename(), "api.hpp");
        }

        TEST_F(ProjectIndexFixture, CachesFileReadsAndResolvesBasenames) {
            ProjectIndex index(root);
            const auto first = index.read_file("src/main.cpp");
            const auto second = index.read_file("main.cpp");
            ASSERT_TRUE(first.has_value());
            ASSERT_TRUE(second.has_value());
            EXPECT_EQ(*first, *second);
            EXPECT_EQ(index.find_file("api.hpp"), root / "include" / "api.hpp");
        }

        TEST_F(ProjectIndexFixture, LoadsCompileCommandsAndNormalizesSourceArguments) {
            const fs::path database = root / "compile_commands.json";
            std::ofstream(database)
                << "[{\"directory\":\"" << root.string() << "\","
                << "\"file\":\"src/main.cpp\","
                << "\"arguments\":[\"clang++\",\"-Iinclude\",\"-c\",\"src/main.cpp\"]}]";

            ProjectIndex index(root, database);
            const auto command = index.compile_command_for("main.cpp");
            ASSERT_TRUE(command.has_value());
            EXPECT_EQ(index.compile_commands_status(), CompilationDatabaseStatus::Loaded);
            EXPECT_EQ(index.compile_commands().size(), 1u);
            ASSERT_EQ(command->command_line.size(), 4u);
            EXPECT_EQ(command->source_file, root / "src" / "main.cpp");
            EXPECT_EQ(command->command_line.back(), (root / "src" / "main.cpp").string());
        }

        TEST_F(ProjectIndexFixture, ReportsMissingCompileDatabase) {
            ProjectIndex index(root, root / "missing-compile_commands.json");

            EXPECT_EQ(index.compile_commands_status(), CompilationDatabaseStatus::NotFound);
            EXPECT_TRUE(index.compile_commands().empty());
        }

    }  // namespace
}  // namespace bha
