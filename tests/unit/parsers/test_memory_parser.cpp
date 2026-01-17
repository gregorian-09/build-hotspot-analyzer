#include "bha/parsers/memory_parser.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

TEST(MemoryParserTest, ParseGccMemReport) {
    const std::string gcc_output = R"(
Memory still allocated at end of compilation:
103456 kB tree nodes
45678 kB garbage collection overhead
12345 kB tree node sizes
TOTAL: 161479 kB
)";

    auto result = bha::parsers::parse_gcc_mem_report(gcc_output);
    ASSERT_TRUE(result.is_ok());

    const auto& metrics = result.value();
    EXPECT_EQ(metrics.parsing_bytes, 103456 * 1024);
    EXPECT_EQ(metrics.ggc_memory, 45678 * 1024);
    EXPECT_EQ(metrics.peak_memory_bytes, 161479 * 1024);
}

TEST(MemoryParserTest, ParseGccMemReportEmpty) {
    const std::string empty_output;

    auto result = bha::parsers::parse_gcc_mem_report(empty_output);
    ASSERT_TRUE(result.is_ok());

    const auto& metrics = result.value();
    EXPECT_FALSE(metrics.has_data());
}

TEST(MemoryParserTest, ParseGccStackUsage) {
    fs::path temp_file = fs::temp_directory_path() / "test.su";
    std::ofstream file(temp_file);
    file << "main.cpp:42:10:foo\t256\tstatic\n";
    file << "main.cpp:58:5:bar\t512\tdynamic\n";
    file << "main.cpp:100:8:baz\t128\tstatic\n";
    file.close();

    auto result = bha::parsers::parse_gcc_stack_usage(temp_file);
    ASSERT_TRUE(result.is_ok());

    const auto& metrics = result.value();
    EXPECT_EQ(metrics.max_stack_bytes, 512);

    fs::remove(temp_file);
}

TEST(MemoryParserTest, ParseGccStackUsageNonexistent) {
    const fs::path fake_file = "/nonexistent/file.su";

    const auto result = bha::parsers::parse_gcc_stack_usage(fake_file);
    ASSERT_FALSE(result.is_ok());
}

TEST(MemoryParserTest, ParseMsvcMapFile) {
    fs::path temp_file = fs::temp_directory_path() / "test.map";
    std::ofstream file(temp_file);
    file << " Preferred load address is 00400000\n";
    file << "\n";
    file << " Start         Length     Name                   Class\n";
    file << " 0001:00000000 00012345H .text                   CODE\n";
    file << " 0002:00000000 00004567H .rdata                  DATA\n";
    file << " Summary\n";
    file << "\n";
    file << "        10000 bytes\n";
    file.close();

    auto result = bha::parsers::parse_msvc_map_file(temp_file);
    ASSERT_TRUE(result.is_ok());

    const auto& metrics = result.value();
    EXPECT_GT(metrics.peak_memory_bytes, 0);

    fs::remove(temp_file);
}

TEST(MemoryParserTest, ParseMemoryFileStackUsage) {
    fs::path temp_file = fs::temp_directory_path() / "test.su";
    std::ofstream file(temp_file);
    file << "foo.cpp:10:5:func\t1024\tstatic\n";
    file.close();

    auto result = bha::parsers::parse_memory_file(temp_file);
    ASSERT_TRUE(result.is_ok());

    const auto& metrics = result.value();
    EXPECT_EQ(metrics.max_stack_bytes, 1024);

    fs::remove(temp_file);
}

TEST(MemoryParserTest, ParseMemoryFileUnknownExtension) {
    const fs::path fake_file = "/tmp/test.unknown";

    auto result = bha::parsers::parse_memory_file(fake_file);
    ASSERT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code(), bha::ErrorCode::InvalidArgument);
}

TEST(MemoryParserTest, MemoryMetricsHasData) {
    bha::MemoryMetrics metrics;
    EXPECT_FALSE(metrics.has_data());

    metrics.peak_memory_bytes = 1024;
    EXPECT_TRUE(metrics.has_data());

    metrics.peak_memory_bytes = 0;
    metrics.max_stack_bytes = 512;
    EXPECT_TRUE(metrics.has_data());
}
