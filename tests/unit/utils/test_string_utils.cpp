//
// Created by gregorian on 29/10/2025.
//

#include <gtest/gtest.h>
#include "bha/utils/string_utils.h"

using namespace bha::utils;

TEST(StringUtilsTest, SplitSingleChar) {
    const auto result = split("hello,world,test", ',');
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "hello");
    EXPECT_EQ(result[1], "world");
    EXPECT_EQ(result[2], "test");
}

TEST(StringUtilsTest, SplitEmptyString) {
    const auto result = split("", ',');
    EXPECT_FALSE(result.empty());
}

TEST(StringUtilsTest, SplitWithEmptyTokens) {
    const auto result = split("a,,b", ',');
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "");
    EXPECT_EQ(result[2], "b");
}

TEST(StringUtilsTest, SplitMultiChar) {
    const auto result = split("hello::world::test", "::");
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "hello");
    EXPECT_EQ(result[1], "world");
    EXPECT_EQ(result[2], "test");
}

TEST(StringUtilsTest, JoinStrings) {
    const std::vector<std::string> strings = {"hello", "world", "test"};
    const auto result = join(strings, ", ");
    EXPECT_EQ(result, "hello, world, test");
}

TEST(StringUtilsTest, JoinEmptyVector) {
    constexpr std::vector<std::string> strings;
    const auto result = join(strings, ", ");
    EXPECT_EQ(result, "");
}

TEST(StringUtilsTest, JoinSingleElement) {
    const std::vector<std::string> strings = {"hello"};
    const auto result = join(strings, ", ");
    EXPECT_EQ(result, "hello");
}

TEST(StringUtilsTest, TrimWhitespace) {
    EXPECT_EQ(trim("  hello  "), "hello");
    EXPECT_EQ(trim("hello"), "hello");
    EXPECT_EQ(trim("   "), "");
    EXPECT_EQ(trim(""), "");
}

TEST(StringUtilsTest, TrimLeft) {
    EXPECT_EQ(trim_left("  hello  "), "hello  ");
    EXPECT_EQ(trim_left("hello"), "hello");
}

TEST(StringUtilsTest, TrimRight) {
    EXPECT_EQ(trim_right("  hello  "), "  hello");
    EXPECT_EQ(trim_right("hello"), "hello");
}

TEST(StringUtilsTest, StartsWith) {
    EXPECT_TRUE(starts_with("hello world", "hello"));
    EXPECT_FALSE(starts_with("hello world", "world"));
    EXPECT_TRUE(starts_with("test", "test"));
    EXPECT_TRUE(starts_with("test", ""));
}

TEST(StringUtilsTest, EndsWith) {
    EXPECT_TRUE(ends_with("hello world", "world"));
    EXPECT_FALSE(ends_with("hello world", "hello"));
    EXPECT_TRUE(ends_with("test", "test"));
    EXPECT_TRUE(ends_with("test", ""));
}

TEST(StringUtilsTest, ContainsSubstring) {
    EXPECT_TRUE(contains("hello world", "world"));
    EXPECT_TRUE(contains("hello world", "lo wo"));
    EXPECT_FALSE(contains("hello world", "xyz"));
}

TEST(StringUtilsTest, ContainsChar) {
    EXPECT_TRUE(contains("hello", 'e'));
    EXPECT_FALSE(contains("hello", 'x'));
}

TEST(StringUtilsTest, ToLower) {
    EXPECT_EQ(to_lower("HELLO World"), "hello world");
    EXPECT_EQ(to_lower("abc123"), "abc123");
    EXPECT_EQ(to_lower(""), "");
}

TEST(StringUtilsTest, ToUpper) {
    EXPECT_EQ(to_upper("hello World"), "HELLO WORLD");
    EXPECT_EQ(to_upper("ABC123"), "ABC123");
    EXPECT_EQ(to_upper(""), "");
}

TEST(StringUtilsTest, ReplaceAll) {
    EXPECT_EQ(replace_all("hello world hello", "hello", "hi"), "hi world hi");
    EXPECT_EQ(replace_all("aaa", "aa", "b"), "ba");
    EXPECT_EQ(replace_all("test", "xyz", "abc"), "test");
}

TEST(StringUtilsTest, ReplaceFirst) {
    EXPECT_EQ(replace_first("hello world hello", "hello", "hi"), "hi world hello");
    EXPECT_EQ(replace_first("test", "xyz", "abc"), "test");
}

TEST(StringUtilsTest, Find) {
    EXPECT_EQ(find("hello world", "world"), 6);
    EXPECT_EQ(find("hello world", "xyz"), std::nullopt);
    EXPECT_EQ(find("hello", "hello"), 0);
}

TEST(StringUtilsTest, FindLast) {
    EXPECT_EQ(find_last("hello world hello", "hello"), 12);
    EXPECT_EQ(find_last("test", "xyz"), std::nullopt);
}

TEST(StringUtilsTest, IsEmptyOrWhitespace) {
    EXPECT_TRUE(is_empty_or_whitespace(""));
    EXPECT_TRUE(is_empty_or_whitespace("   "));
    EXPECT_TRUE(is_empty_or_whitespace("\t\n"));
    EXPECT_FALSE(is_empty_or_whitespace("hello"));
    EXPECT_FALSE(is_empty_or_whitespace(" hello "));
}

TEST(StringUtilsTest, RemovePrefix) {
    EXPECT_EQ(remove_prefix("hello world", "hello "), "world");
    EXPECT_EQ(remove_prefix("hello world", "test"), "hello world");
    EXPECT_EQ(remove_prefix("test", "test"), "");
}

TEST(StringUtilsTest, RemoveSuffix) {
    EXPECT_EQ(remove_suffix("hello world", " world"), "hello");
    EXPECT_EQ(remove_suffix("hello world", "test"), "hello world");
    EXPECT_EQ(remove_suffix("test", "test"), "");
}

TEST(StringUtilsTest, SplitLines) {
    const auto result = split_lines("line1\nline2\nline3");
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "line1");
    EXPECT_EQ(result[1], "line2");
    EXPECT_EQ(result[2], "line3");
}

TEST(StringUtilsTest, SplitLinesWithCRLF) {
    const auto result = split_lines("line1\r\nline2\r\nline3");
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "line1");
    EXPECT_EQ(result[1], "line2");
    EXPECT_EQ(result[2], "line3");
}

TEST(StringUtilsTest, SplitLinesEmpty) {
    const auto result = split_lines("");
    EXPECT_TRUE(result.empty());
}

TEST(StringUtilsTest, EqualsIgnoreCase) {
    EXPECT_TRUE(equals_ignore_case("Hello", "hello"));
    EXPECT_TRUE(equals_ignore_case("WORLD", "world"));
    EXPECT_TRUE(equals_ignore_case("Test123", "test123"));
    EXPECT_FALSE(equals_ignore_case("hello", "world"));
}