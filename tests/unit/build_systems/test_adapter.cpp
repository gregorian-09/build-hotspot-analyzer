#include "bha/build_systems/adapter.hpp"
#include <gtest/gtest.h>

using namespace bha::build_systems;

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
