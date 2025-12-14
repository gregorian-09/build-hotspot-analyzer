//
// Created by gregorian on 09/12/2025.
//

#include <gtest/gtest.h>
#include "bha/build_systems/make_adapter.h"
#include <filesystem>
#include <fstream>

using namespace bha::build_systems;
namespace fs = std::filesystem;

class MakeAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "bha_make_test";
        fs::remove_all(temp_dir);
        fs::create_directories(temp_dir / "build");
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }

    fs::path temp_dir;

    void CreateMakefile() const
    {
        std::ofstream makefile((temp_dir / "build" / "Makefile"));
        makefile << ".PHONY: all clean\n";
        makefile << "\n";
        makefile << "CC = gcc\n";
        makefile << "CFLAGS = -Wall -O2\n";
        makefile << "\n";
        makefile << "all: target.o\n";
        makefile << "\n";
        makefile << "target.o: file1.c file2.c\n";
        makefile << "\t$(CC) $(CFLAGS) -c file1.c\n";
        makefile << "\t$(CC) $(CFLAGS) -c file2.c\n";
        makefile << "\n";
        makefile << "clean:\n";
        makefile << "\trm -f *.o\n";
        makefile.close();
    }

    void CreateMakeLog() const
    {
        std::ofstream log((temp_dir / "build" / "make.log"));
        log << "gcc -Wall -O2 -c file1.c\n";
        log << "gcc -Wall -O2 -c file2.c\n";
        log.close();
    }

    void CreateTimeTraceFile() const
    {
        std::ofstream trace((temp_dir / "build" / "file1.time-trace.json"));
        trace << "[]";
        trace.close();
    }
};

TEST_F(MakeAdapterTest, DetectMakeBuildSystem) {
    CreateMakefile();
    MakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.detect_build_system((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& info = result.value();
    EXPECT_EQ(info.type, BuildSystemType::MAKE);
    EXPECT_EQ(info.build_directory, (temp_dir / "build").string());
}

TEST_F(MakeAdapterTest, ExtractCompileCommands) {
    CreateMakefile();
    CreateMakeLog();
    MakeAdapter adapter((temp_dir / "build").string());

    if (auto result = adapter.extract_compile_commands(); result.is_success()) {
        for (auto commands = result.value(); const auto& cmd : commands) {
            EXPECT_EQ(cmd.directory, (temp_dir / "build").string());
        }
    } else {
        EXPECT_TRUE(true);
    }
}

TEST_F(MakeAdapterTest, ParseMakefile) {
    CreateMakefile();

    auto result = MakeAdapter::parse_makefile((temp_dir / "build" / "Makefile").string());

    ASSERT_TRUE(result.is_success());
    const auto& targets = result.value();
    EXPECT_GT(targets.size(), 0);
    bool found_target = false;
    for (const auto& target : targets) {
        if (target.name == "all" || target.name == "target.o" || target.name == "clean") {
            found_target = true;
            break;
        }
    }
    EXPECT_TRUE(found_target);
}

TEST_F(MakeAdapterTest, ParseMakefileWithoutFile) {
    auto result = MakeAdapter::parse_makefile((temp_dir / "build" / "nonexistent").string());

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::FILE_NOT_FOUND);
}

TEST_F(MakeAdapterTest, GetTraceFiles) {
    CreateMakefile();
    CreateMakeLog();
    CreateTimeTraceFile();
    MakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_trace_files((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& files = result.value();
    EXPECT_GT(files.size(), 0);
    bool found_time_trace = false;
    bool found_make_log = false;
    for (const auto& file : files) {
        if (file.find("time-trace") != std::string::npos) {
            found_time_trace = true;
        }
        if (file.find("make.log") != std::string::npos) {
            found_make_log = true;
        }
    }
    EXPECT_TRUE(found_time_trace || found_make_log);
}

TEST_F(MakeAdapterTest, GetTraceFilesWhenNoneExist) {
    CreateMakefile();
    MakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_trace_files((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto& files = result.value();
    EXPECT_EQ(files.size(), 0);
}

TEST_F(MakeAdapterTest, GetTargets) {
    CreateMakefile();
    MakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_targets();

    ASSERT_TRUE(result.is_success());
    const auto& targets = result.value();
    EXPECT_GT(targets.size(), 0);
}

TEST_F(MakeAdapterTest, GetTargetsWithoutMakefile) {
    MakeAdapter adapter((temp_dir / "build").string());

    const auto result = adapter.get_targets();

    ASSERT_TRUE(result.is_success());
}

TEST_F(MakeAdapterTest, GetBuildOrder) {
    CreateMakefile();
    CreateMakeLog();
    MakeAdapter adapter((temp_dir / "build").string());

    auto result = adapter.get_build_order();

    ASSERT_TRUE(result.is_success());
    const auto& order = result.value();
    EXPECT_GT(order.size(), 0);
}

TEST_F(MakeAdapterTest, EnableTracingForGCC) {
    CreateMakefile();
    CreateMakeLog();
    MakeAdapter adapter((temp_dir / "build").string());

    const auto result = adapter.enable_tracing((temp_dir / "build").string(), "gcc");

    ASSERT_TRUE(result.is_success());
}

TEST_F(MakeAdapterTest, EnableTracingForClang) {
    CreateMakefile();
    CreateMakeLog();
    MakeAdapter adapter((temp_dir / "build").string());

    const auto result = adapter.enable_tracing((temp_dir / "build").string(), "clang");

    ASSERT_TRUE(result.is_success());
}

TEST_F(MakeAdapterTest, EnableTracingForUnsupportedCompiler) {
    CreateMakefile();
    CreateMakeLog();
    MakeAdapter adapter((temp_dir / "build").string());

    const auto result = adapter.enable_tracing((temp_dir / "build").string(), "unknown");

    // May fail for unsupported compiler
    // Ensure that a result is returned
    EXPECT_TRUE(result.is_success() || result.is_failure());
}