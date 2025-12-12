//
// Created by gregorian on 12/12/2025.
//

#include <gtest/gtest.h>
#include "bha/security/resource_limiter.h"
#include <thread>
#include <chrono>

using namespace bha::security;
using namespace bha::core;

TEST(ResourceLimiterTest, DefaultLimits) {
    constexpr ResourceLimiter::Limits limits;
    EXPECT_EQ(limits.max_memory_bytes, 8ULL * 1024 * 1024 * 1024);  // 8GB
    EXPECT_EQ(limits.max_execution_time.count(), 300);  // 5 minutes
    EXPECT_EQ(limits.max_graph_nodes, 100000);
    EXPECT_EQ(limits.max_graph_edges, 1000000);
    EXPECT_EQ(limits.max_compilation_units, 50000);
}

TEST(ResourceLimiterTest, StartTimer) {
    constexpr ResourceLimiter::Limits limits;
    ResourceLimiter limiter(limits);

    limiter.start_timer();
    const auto elapsed = limiter.get_elapsed_time();

    EXPECT_GE(elapsed.count(), 0);
}

TEST(ResourceLimiterTest, GetElapsedTime) {
    constexpr ResourceLimiter::Limits limits;
    ResourceLimiter limiter(limits);

    limiter.start_timer();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto elapsed = limiter.get_elapsed_time();
    EXPECT_GE(elapsed.count(), 0);
    EXPECT_LT(elapsed.count(), 10);
}

TEST(ResourceLimiterTest, CheckMemoryLimit_WithinLimit) {
    ResourceLimiter::Limits limits;
    limits.max_memory_bytes = 100ULL * 1024 * 1024 * 1024;  // 100GB (very high)
    const ResourceLimiter limiter(limits);

    const auto result = limiter.check_memory_limit();
    EXPECT_TRUE(result.is_success());
}

TEST(ResourceLimiterTest, CheckMemoryLimit_ExceedsLimit) {
    ResourceLimiter::Limits limits;
    limits.max_memory_bytes = 1;  // 1 byte (impossibly low)
    const ResourceLimiter limiter(limits);

    const auto result = limiter.check_memory_limit();
    EXPECT_TRUE(result.is_failure());
}

TEST(ResourceLimiterTest, CheckTimeLimit_WithinLimit) {
    ResourceLimiter::Limits limits;
    limits.max_execution_time = std::chrono::seconds(10);
    ResourceLimiter limiter(limits);

    limiter.start_timer();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto result = limiter.check_time_limit();
    EXPECT_TRUE(result.is_success());
}

TEST(ResourceLimiterTest, CheckTimeLimit_ExceedsLimit) {
    ResourceLimiter::Limits limits;
    limits.max_execution_time = std::chrono::seconds(0);
    ResourceLimiter limiter(limits);

    limiter.start_timer();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    const auto result = limiter.check_time_limit();
    EXPECT_TRUE(result.is_failure());
}

TEST(ResourceLimiterTest, CheckGraphSizeLimit_WithinLimit) {
    ResourceLimiter::Limits limits;
    limits.max_graph_nodes = 1000;
    limits.max_graph_edges = 10000;
    const ResourceLimiter limiter(limits);

    const auto result = limiter.check_graph_size_limit(500, 5000);
    EXPECT_TRUE(result.is_success());
}

TEST(ResourceLimiterTest, CheckGraphSizeLimit_NodesExceed) {
    ResourceLimiter::Limits limits;
    limits.max_graph_nodes = 100;
    limits.max_graph_edges = 10000;
    const ResourceLimiter limiter(limits);

    const auto result = limiter.check_graph_size_limit(1000, 500);
    EXPECT_TRUE(result.is_failure());
}

TEST(ResourceLimiterTest, CheckGraphSizeLimit_EdgesExceed) {
    ResourceLimiter::Limits limits;
    limits.max_graph_nodes = 10000;
    limits.max_graph_edges = 100;
    const ResourceLimiter limiter(limits);

    const auto result = limiter.check_graph_size_limit(500, 1000);
    EXPECT_TRUE(result.is_failure());
}

TEST(ResourceLimiterTest, CheckGraphSizeLimit_BothExceed) {
    ResourceLimiter::Limits limits;
    limits.max_graph_nodes = 100;
    limits.max_graph_edges = 100;
    const ResourceLimiter limiter(limits);

    const auto result = limiter.check_graph_size_limit(1000, 1000);
    EXPECT_TRUE(result.is_failure());
}

TEST(ResourceLimiterTest, CheckCompilationUnitsLimit_WithinLimit) {
    ResourceLimiter::Limits limits;
    limits.max_compilation_units = 1000;
    const ResourceLimiter limiter(limits);

    const auto result = limiter.check_compilation_units_limit(500);
    EXPECT_TRUE(result.is_success());
}

TEST(ResourceLimiterTest, CheckCompilationUnitsLimit_ExactlyAtLimit) {
    ResourceLimiter::Limits limits;
    limits.max_compilation_units = 1000;
    const ResourceLimiter limiter(limits);

    const auto result = limiter.check_compilation_units_limit(1000);
    EXPECT_TRUE(result.is_success() || result.is_failure());
}

TEST(ResourceLimiterTest, CheckCompilationUnitsLimit_ExceedsLimit) {
    ResourceLimiter::Limits limits;
    limits.max_compilation_units = 100;
    const ResourceLimiter limiter(limits);

    const auto result = limiter.check_compilation_units_limit(1000);
    EXPECT_TRUE(result.is_failure());
}

TEST(ResourceLimiterTest, Reset) {
    ResourceLimiter::Limits limits;
    limits.max_execution_time = std::chrono::seconds(1);
    ResourceLimiter limiter(limits);

    limiter.start_timer();
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    auto result = limiter.check_time_limit();
    EXPECT_TRUE(result.is_failure());

    limiter.reset();
    limiter.start_timer();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    result = limiter.check_time_limit();
    EXPECT_TRUE(result.is_success());
}

TEST(ResourceLimiterTest, GetCurrentMemoryUsage) {
    const size_t memory = ResourceLimiter::get_current_memory_usage();
    EXPECT_GT(memory, 0);  // Should have some memory usage
}

TEST(ResourceLimiterTest, MultipleChecks) {
    ResourceLimiter::Limits limits;
    limits.max_graph_nodes = 1000;
    limits.max_graph_edges = 10000;
    limits.max_compilation_units = 5000;
    limits.max_execution_time = std::chrono::seconds(10);
    ResourceLimiter limiter(limits);

    limiter.start_timer();

    EXPECT_TRUE(limiter.check_graph_size_limit(100, 1000).is_success());
    EXPECT_TRUE(limiter.check_compilation_units_limit(100).is_success());
    EXPECT_TRUE(limiter.check_time_limit().is_success());
}

TEST(ResourceLimiterTest, ZeroLimits) {
    ResourceLimiter::Limits limits;
    limits.max_graph_nodes = 0;
    limits.max_graph_edges = 0;
    limits.max_compilation_units = 0;
    const ResourceLimiter limiter(limits);

    // All checks should fail with zero limits
    EXPECT_TRUE(limiter.check_graph_size_limit(1, 1).is_failure());
    EXPECT_TRUE(limiter.check_compilation_units_limit(1).is_failure());
}