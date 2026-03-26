#include "bha/build_systems/adapter.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace bha::build_systems;
namespace fs = std::filesystem;

namespace {
    struct TempDir {
        fs::path root;

        TempDir() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root = fs::temp_directory_path() / ("bha-buildsys-" + std::to_string(stamp));
            fs::create_directories(root);
        }

        ~TempDir() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    };

    void write_file(const fs::path& path, const std::string& content) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }
}

TEST(BuildOptionsTest, DefaultValues) {
    const BuildOptions options;
    EXPECT_EQ(options.build_type, "Release");
    EXPECT_EQ(options.parallel_jobs, 0);
    EXPECT_TRUE(options.enable_tracing);
    EXPECT_FALSE(options.enable_memory_profiling);
    EXPECT_FALSE(options.clean_first);
    EXPECT_FALSE(options.verbose);
    EXPECT_TRUE(options.compiler.empty());
}

TEST(BuildOptionsTest, MemoryProfilingFlag) {
    BuildOptions options;
    options.enable_memory_profiling = true;
    EXPECT_TRUE(options.enable_memory_profiling);
    EXPECT_TRUE(options.enable_tracing);
}

TEST(BuildResultTest, DefaultValues) {
    const BuildResult result;
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.trace_files.empty());
    EXPECT_TRUE(result.memory_files.empty());
    EXPECT_EQ(result.build_time, bha::Duration::zero());
    EXPECT_EQ(result.files_compiled, 0);
}

TEST(BuildResultTest, MemoryFiles) {
    BuildResult result;
    result.success = true;
    result.memory_files.emplace_back("/path/to/file.su");
    result.memory_files.emplace_back("/path/to/file.map");

    EXPECT_EQ(result.memory_files.size(), 2);
    EXPECT_EQ(result.memory_files[0].string(), "/path/to/file.su");
    EXPECT_EQ(result.memory_files[1].string(), "/path/to/file.map");
}

TEST(BuildSystemRegistryTest, Singleton) {
    auto& registry1 = BuildSystemRegistry::instance();
    auto& registry2 = BuildSystemRegistry::instance();
    EXPECT_EQ(&registry1, &registry2);
}

TEST(BuildSystemRegistryTest, AdaptersNotEmpty) {
    register_all_adapters();
    const auto& registry = BuildSystemRegistry::instance();
    EXPECT_FALSE(registry.adapters().empty());
    EXPECT_GE(registry.adapters().size(), 4);
}

TEST(BuildSystemRegistryTest, GetByName) {
    register_all_adapters();
    const auto& registry = BuildSystemRegistry::instance();

    auto* cmake = registry.get("CMake");
    EXPECT_NE(cmake, nullptr);
    EXPECT_EQ(cmake->name(), "CMake");

    auto* ninja = registry.get("Ninja");
    EXPECT_NE(ninja, nullptr);
    EXPECT_EQ(ninja->name(), "Ninja");

    auto* nonexistent = registry.get("Nonexistent");
    EXPECT_EQ(nonexistent, nullptr);
}

TEST(BuildSystemRegistryTest, DetectsUnrealProjectFromUprojectAndTargetRules) {
    register_all_adapters();
    const auto& registry = BuildSystemRegistry::instance();

    TempDir temp;
    write_file(temp.root / "Sample.uproject", "{\n}");
    write_file(
        temp.root / "Source" / "SampleEditor.Target.cs",
        "using UnrealBuildTool;\n"
        "public class SampleEditorTarget : TargetRules {\n"
        "  public SampleEditorTarget(ReadOnlyTargetRules Target) : base(Target) {}\n"
        "}\n"
    );

    auto* adapter = registry.detect(temp.root);
    ASSERT_NE(adapter, nullptr);
    EXPECT_EQ(adapter->name(), "Unreal");
}

TEST(BuildSystemRegistryTest, PrefersAvailableAdapterWhenHigherConfidenceToolIsMissing) {
    register_all_adapters();
    const auto& registry = BuildSystemRegistry::instance();

#ifdef _WIN32
    if (std::system("where bazel >nul 2>nul") == 0 || std::system("where bazelisk >nul 2>nul") == 0) {
        GTEST_SKIP() << "Bazel is installed; availability fallback is not exercised";
    }
#else
    if (std::system("command -v bazel >/dev/null 2>&1") == 0 ||
        std::system("command -v bazelisk >/dev/null 2>&1") == 0) {
        GTEST_SKIP() << "Bazel is installed; availability fallback is not exercised";
    }
#endif

    TempDir temp;
    write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(sample)\n");
    write_file(temp.root / "MODULE.bazel", "module(name = \"sample\")\n");

    auto* adapter = registry.detect(temp.root);
    ASSERT_NE(adapter, nullptr);
    EXPECT_EQ(adapter->name(), "CMake");
}
