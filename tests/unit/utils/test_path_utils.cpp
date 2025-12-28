//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/utils/path_utils.hpp"

#include <gtest/gtest.h>

namespace bha::path_utils
{
    namespace fs = std::filesystem;

    TEST(NormalizeTest, ResolveDots) {
        EXPECT_EQ(normalize("a/b/../c"), fs::path("a/c"));
        EXPECT_EQ(normalize("a/./b/c"), fs::path("a/b/c"));
        EXPECT_EQ(normalize("a/b/c/../../d"), fs::path("a/d"));
    }

    TEST(NormalizeTest, Empty) {
        EXPECT_EQ(normalize(""), fs::path("."));
    }

    TEST(NormalizeTest, LeadingDotDot) {
        EXPECT_EQ(normalize("../a/b"), fs::path("../a/b"));
    }

    TEST(IsUnderTest, Basic) {
        EXPECT_TRUE(is_under("/home/user/project/src/file.cpp", "/home/user/project"));
        EXPECT_TRUE(is_under("/home/user/project/src", "/home/user/project"));
        EXPECT_FALSE(is_under("/home/user/other/file.cpp", "/home/user/project"));
        EXPECT_FALSE(is_under("/home/user/project", "/home/user/project/src"));
    }

    TEST(ReplaceExtensionTest, Basic) {
        EXPECT_EQ(replace_extension("file.cpp", ".h"), fs::path("file.h"));
        EXPECT_EQ(replace_extension("file.cpp", "h"), fs::path("file.h"));
        EXPECT_EQ(replace_extension("path/to/file.cpp", ".hpp"), fs::path("path/to/file.hpp"));
    }

    TEST(StemTest, Basic) {
        EXPECT_EQ(stem("file.cpp"), "file");
        EXPECT_EQ(stem("path/to/file.cpp"), "file");
        EXPECT_EQ(stem("file"), "file");
    }

    TEST(JoinTest, Basic) {
        const std::vector<std::string> parts = {"path", "to", "file.cpp"};
        const auto result = join(parts);
        EXPECT_EQ(result, fs::path("path/to/file.cpp"));
    }

    TEST(SplitTest, Basic) {
        const auto parts = split(fs::path("path/to/file.cpp"));
        ASSERT_EQ(parts.size(), 3u);
        EXPECT_EQ(parts[0], "path");
        EXPECT_EQ(parts[1], "to");
        EXPECT_EQ(parts[2], "file.cpp");
    }

    TEST(ToForwardSlashesTest, Basic) {
        const auto result = to_forward_slashes(fs::path("path/to/file"));
        EXPECT_EQ(result, "path/to/file");
    }

    TEST(DepthTest, Basic) {
        EXPECT_EQ(depth(fs::path("a/b/c")), 3u);
        EXPECT_EQ(depth(fs::path("a")), 1u);
        EXPECT_EQ(depth(fs::path("")), 0u);
    }
}