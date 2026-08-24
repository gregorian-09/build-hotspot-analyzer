#include "bha/utils/include_parse_utils.hpp"

#include <gtest/gtest.h>

namespace bha::utils {

    TEST(IncludeParseUtilsTest, AcceptsTrailingComments) {
        const auto line_comment = parse_include_directive_line("#include \"header.hpp\" // comment");
        const auto block_comment = parse_include_directive_line("#include <header.hpp> /* comment */");

        ASSERT_TRUE(line_comment.has_value());
        ASSERT_TRUE(block_comment.has_value());
        EXPECT_EQ(line_comment->header_name, "header.hpp");
        EXPECT_TRUE(block_comment->is_system);
    }

    TEST(IncludeParseUtilsTest, AcceptsCommentsBetweenDirectiveTokens) {
        const auto result = parse_include_directive_line("#/* directive */include/* header */\"header.hpp\"");

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->header_name, "header.hpp");
    }

    TEST(IncludeParseUtilsTest, RejectsTrailingPreprocessingTokens) {
        EXPECT_FALSE(parse_include_directive_line("#include \"header.hpp\" trailing").has_value());
        EXPECT_FALSE(parse_include_directive_line("#include <header.hpp> /* unterminated").has_value());
    }

}  // namespace bha::utils
