//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/utils/string_utils.hpp"

#include <gtest/gtest.h>

namespace bha::string_utils
{
    TEST(TrimTest, TrimLeft) {
        EXPECT_EQ(trim_left("  hello"), "hello");
        EXPECT_EQ(trim_left("\t\nhello"), "hello");
        EXPECT_EQ(trim_left("hello"), "hello");
        EXPECT_EQ(trim_left(""), "");
    }

    TEST(TrimTest, TrimRight) {
        EXPECT_EQ(trim_right("hello  "), "hello");
        EXPECT_EQ(trim_right("hello\t\n"), "hello");
        EXPECT_EQ(trim_right("hello"), "hello");
        EXPECT_EQ(trim_right(""), "");
    }

    TEST(TrimTest, Trim) {
        EXPECT_EQ(trim("  hello  "), "hello");
        EXPECT_EQ(trim("\t\nhello\t\n"), "hello");
        EXPECT_EQ(trim("hello"), "hello");
        EXPECT_EQ(trim("   "), "");
    }

    TEST(SplitTest, SplitByChar) {
        const auto parts = split("a,b,c", ',');
        ASSERT_EQ(parts.size(), 3u);
        EXPECT_EQ(parts[0], "a");
        EXPECT_EQ(parts[1], "b");
        EXPECT_EQ(parts[2], "c");
    }

    TEST(SplitTest, SplitEmptyParts) {
        const auto parts = split("a,,c", ',');
        ASSERT_EQ(parts.size(), 3u);
        EXPECT_EQ(parts[0], "a");
        EXPECT_EQ(parts[1], "");
        EXPECT_EQ(parts[2], "c");
    }

    TEST(SplitTest, SplitNoDelimiter) {
        const auto parts = split("hello", ',');
        ASSERT_EQ(parts.size(), 1u);
        EXPECT_EQ(parts[0], "hello");
    }

    TEST(SplitTest, SplitByString) {
        const auto parts = split("a::b::c", "::");
        ASSERT_EQ(parts.size(), 3u);
        EXPECT_EQ(parts[0], "a");
        EXPECT_EQ(parts[1], "b");
        EXPECT_EQ(parts[2], "c");
    }

    TEST(JoinTest, JoinStrings) {
        const std::vector<std::string> parts = {"a", "b", "c"};
        EXPECT_EQ(join(parts, ","), "a,b,c");
        EXPECT_EQ(join(parts, "::"), "a::b::c");
    }

    TEST(JoinTest, JoinEmpty) {
        constexpr std::vector<std::string> empty;
        EXPECT_EQ(join(empty, ","), "");
    }

    TEST(JoinTest, JoinSingle) {
        const std::vector<std::string> single = {"only"};
        EXPECT_EQ(join(single, ","), "only");
    }

    TEST(StartsWithTest, Basic) {
        EXPECT_TRUE(starts_with("hello world", "hello"));
        EXPECT_TRUE(starts_with("hello", "hello"));
        EXPECT_FALSE(starts_with("hello", "world"));
        EXPECT_TRUE(starts_with("hello", ""));
        EXPECT_FALSE(starts_with("", "hello"));
    }

    TEST(EndsWithTest, Basic) {
        EXPECT_TRUE(ends_with("hello world", "world"));
        EXPECT_TRUE(ends_with("hello", "hello"));
        EXPECT_FALSE(ends_with("hello", "world"));
        EXPECT_TRUE(ends_with("hello", ""));
        EXPECT_FALSE(ends_with("", "hello"));
    }

    TEST(ContainsTest, Basic) {
        EXPECT_TRUE(contains("hello world", "lo wo"));
        EXPECT_TRUE(contains("hello", "hello"));
        EXPECT_FALSE(contains("hello", "xyz"));
        EXPECT_TRUE(contains("hello", ""));
    }

    TEST(CaseConversionTest, ToLower) {
        EXPECT_EQ(to_lower("HELLO"), "hello");
        EXPECT_EQ(to_lower("Hello World"), "hello world");
        EXPECT_EQ(to_lower("hello"), "hello");
        EXPECT_EQ(to_lower(""), "");
    }

    TEST(CaseConversionTest, ToUpper) {
        EXPECT_EQ(to_upper("hello"), "HELLO");
        EXPECT_EQ(to_upper("Hello World"), "HELLO WORLD");
        EXPECT_EQ(to_upper("HELLO"), "HELLO");
        EXPECT_EQ(to_upper(""), "");
    }

    TEST(ReplaceAllTest, Basic) {
        EXPECT_EQ(replace_all("hello world", "world", "universe"), "hello universe");
        EXPECT_EQ(replace_all("aaa", "a", "b"), "bbb");
        EXPECT_EQ(replace_all("hello", "xyz", "abc"), "hello");
        EXPECT_EQ(replace_all("", "a", "b"), "");
    }

    TEST(ReplaceAllTest, EmptyFrom) {
        EXPECT_EQ(replace_all("hello", "", "x"), "hello");
    }

    TEST(FormatDurationTest, Various) {
        EXPECT_EQ(format_duration(500), "500ns");
        EXPECT_EQ(format_duration(5000), "5.00us");
        EXPECT_EQ(format_duration(5000000), "5.00ms");
        EXPECT_EQ(format_duration(5000000000), "5.00s");
        EXPECT_EQ(format_duration(300000000000), "5.00min");
        EXPECT_EQ(format_duration(18000000000000), "5.00h");
    }

    TEST(FormatBytesTest, Various) {
        EXPECT_EQ(format_bytes(500), "500 B");
        EXPECT_EQ(format_bytes(5 * 1024), "5.00 KB");
        EXPECT_EQ(format_bytes(5 * 1024 * 1024), "5.00 MB");
        EXPECT_EQ(format_bytes(5ULL * 1024 * 1024 * 1024), "5.00 GB");
    }
}