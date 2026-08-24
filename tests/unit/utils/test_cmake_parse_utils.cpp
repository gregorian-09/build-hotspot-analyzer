#include "bha/utils/cmake_parse_utils.hpp"

#include <gtest/gtest.h>

namespace bha::utils {

    TEST(CMakeParseUtilsTest, IgnoresEscapedParenthesesInUnquotedArguments) {
        EXPECT_EQ(count_paren_delta_outside_quotes("(\\) literal"), 1);
    }

    TEST(CMakeParseUtilsTest, IgnoresParenthesesAfterEscapedQuote) {
        EXPECT_EQ(count_paren_delta_outside_quotes("(\"a \\\" )\""), 1);
    }

    TEST(CMakeParseUtilsTest, IgnoresParenthesesInsideBracketArguments) {
        EXPECT_EQ(count_paren_delta_outside_quotes(R"(( [=[literal ( # )]=] ))"), 0);
    }

    TEST(CMakeParseUtilsTest, PreservesEscapedQuotedAndUnquotedCharacters) {
        const auto tokens = tokenize_cmake_args(R"(DEFINE "a \"b\" c" foo\ bar foo\;bar 'single')");

        ASSERT_EQ(tokens.size(), 5u);
        EXPECT_EQ(tokens[0], "DEFINE");
        EXPECT_EQ(tokens[1], "a \"b\" c");
        EXPECT_EQ(tokens[2], "foo bar");
        EXPECT_EQ(tokens[3], "foo;bar");
        EXPECT_EQ(tokens[4], "'single'");
    }

    TEST(CMakeParseUtilsTest, KeepsBracketArgumentsAsSingleTokens) {
        const auto tokens = tokenize_cmake_args(R"(PROPERTY [=[line
# )]=] tail)");

        ASSERT_EQ(tokens.size(), 3u);
        EXPECT_EQ(tokens[0], "PROPERTY");
        EXPECT_EQ(tokens[1], "line\n# )");
        EXPECT_EQ(tokens[2], "tail");
    }

}  // namespace bha::utils
