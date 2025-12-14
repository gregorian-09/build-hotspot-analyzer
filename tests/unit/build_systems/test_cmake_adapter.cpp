//
// Created by gregorian on 09/12/2025.
//

#include <gtest/gtest.h>
#include "bha/build_systems/cmake_adapter.h"
#include <filesystem>
#include <fstream>

using namespace bha::build_systems;
namespace fs = std::filesystem;

class CMakeAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "bha_cmake_test";
        fs::remove_all(temp_dir);
        fs::create_directories(temp_dir / "build");
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }

    fs::path temp_dir;

    void CreateCMakeCacheFile() const
    {
        std::filesystem::create_directories(temp_dir / "build");
        std::ofstream cache((temp_dir / "build" / "CMakeCache.txt"));
        cache << "# This file is the cache file for the binary tree to what to pre-load.\n";
        cache << "# CMake Version 3.31.0\n";
        cache << "CMAKE_VERSION:UNINITIALIZED=3.31.0\n";
        cache << "CMAKE_HOME_DIRECTORY:INTERNAL=" << (temp_dir / "src").string() << "\n";
        cache << "CMAKE_CXX_FLAGS:STRING=-Wall\n";
        cache << "CMAKE_C_FLAGS:STRING=-Wall\n";
        cache.close();
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
                 << R"(    "command": "g++ -std=c++17 -Wall -o file1.o -c )" << normalize(temp_dir / "src/file1.cpp") << "\",\n"
                 << R"(    "file": ")" << normalize(temp_dir / "src/file1.cpp") << "\",\n"
                 << "    \"arguments\": [\"-std=c++17\", \"-Wall\"],\n"
                 << "    \"output\": \"file1.o\"\n"
                 << "  },\n"
                 << "  {\n"
                 << R"(    "directory": ")" << normalize(temp_dir / "build") << "\",\n"
                 << R"(    "command": "g++ -std=c++17 -Wall -o file2.o -c )" << normalize(temp_dir / "src/file2.cpp") << "\",\n"
                 << R"(    "file": ")" << normalize(temp_dir / "src/file2.cpp") << "\",\n"
                 << "    \"arguments\": [\"-std=c++17\", \"-Wall\"],\n"
                 << "    \"output\": \"file2.o\"\n"
                 << "  }\n"
                 << "]";
        commands.close();
    }

    void CreateTimeTraceFile() const
    {
        std::ofstream trace((temp_dir / "build" / "file1.time-trace.json"));
        trace << "[]";
        trace.close();
    }

    void CreateTargetDirectoriesFile() const
    {
        fs::create_directories(temp_dir / "build" / "CMakeFiles");
        std::ofstream targets((temp_dir / "build" / "CMakeFiles" / "TargetDirectories.txt"));
        targets << "target1\n";
        targets << "target2\n";
        targets.close();
    }
};

TEST_F(CMakeAdapterTest, DetectCMakeBuildSystem) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.detect_build_system((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& info = result.value();
    EXPECT_EQ(info.type, BuildSystemType::CMAKE);
    EXPECT_EQ(info.build_directory, (temp_dir / "build").string());
    EXPECT_NE(info.version, "");
}

TEST_F(CMakeAdapterTest, DetectBuildSystemWithoutCMakeCache) {
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.detect_build_system((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& info = result.value();
    EXPECT_EQ(info.type, BuildSystemType::CMAKE);
}

TEST_F(CMakeAdapterTest, ExtractCompileCommandsSuccessfully) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_success());
    const auto& commands = result.value();
    EXPECT_EQ(commands.size(), 2);

    auto normalize = [](const std::filesystem::path& p) {
        std::string s = p.string();
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };

    EXPECT_EQ(commands[0].file, normalize(temp_dir / "src/file1.cpp"));
    EXPECT_EQ(commands[0].directory, normalize(temp_dir / "build"));
    EXPECT_EQ(commands[1].file, normalize(temp_dir / "src/file2.cpp"));
    EXPECT_EQ(commands[0].file, normalize((temp_dir / "src/file1.cpp").string()));
    EXPECT_EQ(commands[0].directory, normalize((temp_dir / "build").string()));
    EXPECT_EQ(commands[1].file, normalize((temp_dir / "src/file2.cpp").string()));
}

TEST_F(CMakeAdapterTest, ExtractCompileCommandsWithoutCompileCommandsJson) {
    CreateCMakeCacheFile();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::FILE_NOT_FOUND);
}

TEST_F(CMakeAdapterTest, ExtractCompileCommandsWithInvalidJson) {
    CreateCMakeCacheFile();
    std::ofstream commands((temp_dir / "build" / "compile_commands.json"));
    commands << "{ invalid json ]";
    commands.close();

    CMakeAdapter adapter((temp_dir / "build").string());
    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::PARSE_ERROR);
}

TEST_F(CMakeAdapterTest, ExtractCompileCommandsWithEmptyArray) {
    CreateCMakeCacheFile();
    std::ofstream commands((temp_dir / "build" / "compile_commands.json"));
    commands << "[]";
    commands.close();

    CMakeAdapter adapter((temp_dir / "build").string());
    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_success());
    const auto& cmds = result.value();
    EXPECT_EQ(cmds.size(), 0);
}

TEST_F(CMakeAdapterTest, GetTraceFiles) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CreateTimeTraceFile();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_trace_files((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& files = result.value();
    EXPECT_EQ(files.size(), 1);
    EXPECT_TRUE(files[0].find("time-trace") != std::string::npos);
}

TEST_F(CMakeAdapterTest, GetTraceFilesWhenNoneExist) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_trace_files((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& files = result.value();
    EXPECT_EQ(files.size(), 0);
}

TEST_F(CMakeAdapterTest, GetTargets) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CreateTargetDirectoriesFile();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_targets();

    ASSERT_TRUE(result.is_success());
    const auto& targets = result.value();
    EXPECT_EQ(targets.size(), 2);
    EXPECT_TRUE(targets.contains("target1"));
    EXPECT_TRUE(targets.contains("target2"));
}

TEST_F(CMakeAdapterTest, GetTargetsWhenNoTargetsFile) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_targets();

    ASSERT_TRUE(result.is_success());
    const auto& targets = result.value();
    EXPECT_EQ(targets.size(), 0);
}

TEST_F(CMakeAdapterTest, GetBuildOrder) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_build_order();

    ASSERT_TRUE(result.is_success());
    const auto& order = result.value();
    EXPECT_EQ(order.size(), 2);

    auto normalize = [](const std::filesystem::path& p) {
        std::string s = p.string();
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };

    EXPECT_EQ(order[0], normalize((temp_dir / "src/file1.cpp").string()));
    EXPECT_EQ(order[1], normalize((temp_dir / "src/file2.cpp").string()));
}

TEST_F(CMakeAdapterTest, GetBuildOrderWithoutCompileCommands) {
    CreateCMakeCacheFile();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_build_order();

    ASSERT_TRUE(result.is_failure());
}

TEST_F(CMakeAdapterTest, EnableTracingForClang) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "clang");

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(result.value());

    // Verify the cache was modified
    auto lines_opt = std::ifstream((temp_dir / "build" / "CMakeCache.txt"));
    std::string content((std::istreambuf_iterator<char>(lines_opt)),
                       std::istreambuf_iterator<char>());
    EXPECT_TRUE(content.find("-ftime-trace") != std::string::npos);
}

TEST_F(CMakeAdapterTest, EnableTracingForGCC) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "gcc");

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(result.value());
}

TEST_F(CMakeAdapterTest, EnableTracingForMSVC) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "msvc");

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(result.value());
}

TEST_F(CMakeAdapterTest, EnableTracingForUnsupportedCompiler) {
    CreateCMakeCacheFile();
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "unknown");

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::UNSUPPORTED_FORMAT);
}

TEST_F(CMakeAdapterTest, EnableTracingWithoutCMakeCache) {
    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "gcc");

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::FILE_NOT_FOUND);
}

TEST_F(CMakeAdapterTest, EnableTracingDoesNotDuplicateFlag) {
    std::ofstream cache((temp_dir / "build" / "CMakeCache.txt"));
    cache << "CMAKE_CXX_FLAGS:STRING=-Wall -ftime-trace\n";
    cache << "CMAKE_C_FLAGS:STRING=-Wall\n";
    cache.close();

    CreateCompileCommandsJson();
    CMakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.enable_tracing((temp_dir / "build").string(), "clang");

    ASSERT_TRUE(result.is_success());
}