#include "bha/parsers/memory_parser.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

TEST(MemoryParserTest, ParseStackUsageFile) {
    const fs::path temp_file = fs::temp_directory_path() / "test.su";
    std::ofstream file(temp_file);
    file << "main.cpp:42:10:foo\t256\tstatic\n";
    file << "main.cpp:58:5:bar\t512\tdynamic\n";
    file << "main.cpp:100:8:baz\t128\tstatic\n";
    file.close();

    auto result = bha::parsers::parse_stack_usage_file(temp_file);
    ASSERT_TRUE(result.is_ok());

    const auto& [max_stack_bytes] = result.value();
    EXPECT_EQ(max_stack_bytes, 256);

    fs::remove(temp_file);
}

TEST(MemoryParserTest, ParseStackUsageFileNonexistent) {
    const fs::path fake_file = "/nonexistent/file.su";

    const auto result = bha::parsers::parse_stack_usage_file(fake_file);
    ASSERT_FALSE(result.is_ok());
}

TEST(MemoryParserTest, ParseStackUsageFileBounded) {
    const fs::path temp_file = fs::temp_directory_path() / "test_bounded.su";
    std::ofstream file(temp_file);
    file << "test.cpp:10:5:func1\t1024\tstatic\n";
    file << "test.cpp:20:5:func2\t2048\tbounded\n";
    file << "test.cpp:30:5:func3\t4096\tdynamic,bounded\n";
    file.close();

    auto result = bha::parsers::parse_stack_usage_file(temp_file);
    ASSERT_TRUE(result.is_ok());

    const auto& [max_stack_bytes] = result.value();
    EXPECT_EQ(max_stack_bytes, 4096);

    fs::remove(temp_file);
}

TEST(MemoryParserTest, ParseStackUsageFileEmpty) {
    const fs::path temp_file = fs::temp_directory_path() / "test_empty.su";
    std::ofstream file(temp_file);
    file.close();

    auto result = bha::parsers::parse_stack_usage_file(temp_file);
    ASSERT_TRUE(result.is_ok());

    const auto& metrics = result.value();
    EXPECT_FALSE(metrics.has_data());

    fs::remove(temp_file);
}

TEST(MemoryParserTest, MemoryMetricsHasData) {
    bha::MemoryMetrics metrics;
    EXPECT_FALSE(metrics.has_data());

    metrics.max_stack_bytes = 512;
    EXPECT_TRUE(metrics.has_data());
}
