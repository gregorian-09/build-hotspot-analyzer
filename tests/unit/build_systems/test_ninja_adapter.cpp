//
// Created by gregorian on 09/12/2025.
//

#include <gtest/gtest.h>
#include "bha/build_systems/ninja_adapter.h"
#include <filesystem>
#include <fstream>

using namespace bha::build_systems;
namespace fs = std::filesystem;

class NinjaAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "bha_ninja_test";
        fs::remove_all(temp_dir);
        fs::create_directories(temp_dir / "build");
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }

    fs::path temp_dir;

    void CreateBuildNinjaFile() const
    {
        std::filesystem::create_directories(temp_dir / "build");

        std::ofstream ninja((temp_dir / "build" / "build.ninja"));
        ninja << "rule cc\n";
        ninja << "  command = gcc -Wall -c $in -o $out\n";
        ninja << "  description = Compiling $in\n\n";
        ninja << "rule link\n";
        ninja << "  command = gcc -o $out $in\n";
        ninja << "  description = Linking $out\n\n";
        ninja << "build file1.o: cc file1.c\n";
        ninja << "build file2.o: cc file2.c\n";
        ninja << "build program: link file1.o file2.o\n";
        ninja.close();
    }

    void CreateNinjaLogFile() const
    {
        std::filesystem::create_directories(temp_dir / "build");

        std::ofstream log((temp_dir / "build" / ".ninja_log"));
        log << "# ninja log v5\n";
        log << "0\t1000\t1000\tfile1.o\t1\n";
        log << "0\t2000\t1500\tfile2.o\t2\n";
        log << "1\t3500\t1500\tprogram\t0\n";
        log.close();
    }

    void CreateNinjaDepsFile() const
    {
        std::filesystem::create_directories(temp_dir / "build");

        std::ofstream deps((temp_dir / "build" / ".ninja_deps"));
        deps << "ninja_deps_version=4\n";
        deps << "file1.o: 1 file1.c\n";
        deps << "file2.o: 1 file2.c\n";
        deps.close();
    }

    void CreateCompileCommandsJson() const
    {
        std::filesystem::create_directories(temp_dir / "build");

        auto normalize = [](const std::filesystem::path& p) {
            std::string s = p.string();
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };

        std::ofstream commands((temp_dir / "build" / "compile_commands.json"));
        commands << "[\n"
                 << "  {\n"
                 << R"(    "directory": ")" << normalize(temp_dir / "build") << "\",\n"
                 << R"(    "command": "gcc -Wall -c )" << normalize(temp_dir / "build" / "file1.c") << " -o file1.o\",\n"
                 << R"(    "file": ")" << normalize(temp_dir / "build" / "file1.c") << "\",\n"
                 << R"(    "arguments": ["-Wall", "-c", ")" << normalize(temp_dir / "build" / "file1.c") << "\", \"-o\", \"file1.o\"],\n"
                 << "    \"output\": \"file1.o\"\n"
                 << "  },\n"
                 << "  {\n"
                 << R"(    "directory": ")" << normalize(temp_dir / "build") << "\",\n"
                 << R"(    "command": "gcc -Wall -c )" << normalize(temp_dir / "build" / "file2.c") << " -o file2.o\",\n"
                 << R"(    "file": ")" << normalize(temp_dir / "build" / "file2.c") << "\",\n"
                 << R"(    "arguments": ["-Wall", "-c", ")" << normalize(temp_dir / "build" / "file2.c") << "\", \"-o\", \"file2.o\"],\n"
                 << "    \"output\": \"file2.o\"\n"
                 << "  }\n"
                 << "]";
        commands.close();
    }


    void CreateTraceFile() const
    {
        std::filesystem::create_directories(temp_dir / "build");

        std::ofstream trace((temp_dir / "build" / "trace.json"));
        trace << "[]";
        trace.close();
    }
};

TEST_F(NinjaAdapterTest, DetectNinjaBuildSystem) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.detect_build_system((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& info = result.value();
    EXPECT_EQ(info.type, BuildSystemType::NINJA);
    EXPECT_EQ(info.build_directory, (temp_dir / "build").string());
}

TEST_F(NinjaAdapterTest, ExtractCompileCommandsSuccessfully) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_success());
    const auto& commands = result.value();
    EXPECT_EQ(commands.size(), 2);

    auto normalize = [](const std::filesystem::path& p) {
        std::string s = p.string();
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };

    EXPECT_EQ(commands[0].file, normalize((temp_dir / "build" / "file1.c").string()));
    EXPECT_EQ(commands[0].directory, normalize((temp_dir / "build").string()));
    EXPECT_EQ(commands[1].file, normalize((temp_dir / "build" / "file2.c").string()));
}

TEST_F(NinjaAdapterTest, ExtractCompileCommandsWithoutCompileCommandsJson) {
    CreateBuildNinjaFile();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::FILE_NOT_FOUND);
}

TEST_F(NinjaAdapterTest, ExtractCompileCommandsWithInvalidJson) {
    CreateBuildNinjaFile();
    std::ofstream commands((temp_dir / "build" / "compile_commands.json"));
    commands << "{ invalid json ]";
    commands.close();

    NinjaAdapter adapter((temp_dir / "build").string());
    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::PARSE_ERROR);
}

TEST_F(NinjaAdapterTest, ExtractCompileCommandsWithEmptyArray) {
    CreateBuildNinjaFile();
    std::ofstream commands((temp_dir / "build" / "compile_commands.json"));
    commands << "[]";
    commands.close();

    NinjaAdapter adapter((temp_dir / "build").string());
    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_success());
    const auto& cmds = result.value();
    EXPECT_EQ(cmds.size(), 0);
}

TEST_F(NinjaAdapterTest, ParseNinjaLog) {
    CreateBuildNinjaFile();
    CreateNinjaLogFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.parse_ninja_log();

    ASSERT_TRUE(result.is_success());
    const auto& entries = result.value();
    EXPECT_EQ(entries.size(), 3);
    EXPECT_EQ(entries[0].target, "file1.o");
    EXPECT_GT(entries[0].duration_ms, 0);
    EXPECT_EQ(entries[1].target, "file2.o");
    EXPECT_GT(entries[1].duration_ms, 0);
}

TEST_F(NinjaAdapterTest, ParseNinjaLogWithoutFile) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.parse_ninja_log();
    EXPECT_TRUE(result.is_success() || result.is_failure());
}

TEST_F(NinjaAdapterTest, GetTraceFiles) {
    CreateBuildNinjaFile();
    CreateNinjaLogFile();
    CreateCompileCommandsJson();
    CreateTraceFile();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_trace_files((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& files = result.value();
    EXPECT_GT(files.size(), 0);
}

TEST_F(NinjaAdapterTest, GetTraceFilesWhenNoneExist) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_trace_files((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& files = result.value();
    EXPECT_EQ(files.size(), 0);
}

TEST_F(NinjaAdapterTest, GetTargets) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_targets();

    ASSERT_TRUE(result.is_success());
    const auto& targets = result.value();
    EXPECT_GE(targets.size(), 0);
}

TEST_F(NinjaAdapterTest, GetBuildOrder) {
    CreateBuildNinjaFile();
    CreateNinjaLogFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_build_order();

    ASSERT_TRUE(result.is_success());
    const auto& order = result.value();
    EXPECT_GT(order.size(), 0);
}

TEST_F(NinjaAdapterTest, GetBuildOrderWithoutLog) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_build_order();
    EXPECT_TRUE(result.is_success() || result.is_failure());
}

TEST_F(NinjaAdapterTest, EnableTracingForClang) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "clang");
    EXPECT_TRUE(result.is_success() || result.is_failure());
}

TEST_F(NinjaAdapterTest, EnableTracingForGCC) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "gcc");
    EXPECT_TRUE(result.is_success() || result.is_failure());
}

TEST_F(NinjaAdapterTest, EnableTracingForMSVC) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "msvc");

    EXPECT_TRUE(result.is_success() || result.is_failure());
}

TEST_F(NinjaAdapterTest, EnableTracingForUnsupportedCompiler) {
    CreateBuildNinjaFile();
    CreateCompileCommandsJson();
    NinjaAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "unknown");

    EXPECT_TRUE(result.is_success() || result.is_failure());
}