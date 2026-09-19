#include "semantic_replay_flags.hpp"

#include <gtest/gtest.h>

namespace bha::suggestions {
    TEST(SemanticReplayFlagsTest, AddsConfiguredResourceDirectory) {
        std::vector<std::string> arguments{"-std=c++20"};
        append_semantic_replay_resource_dir(arguments, "/toolchain/lib/clang");
        EXPECT_EQ(arguments, (std::vector<std::string>{"-std=c++20", "-resource-dir", "/toolchain/lib/clang"}));
    }

    TEST(SemanticReplayFlagsTest, PreservesExplicitResourceDirectory) {
        std::vector<std::string> separate{"-resource-dir", "/project/clang"};
        append_semantic_replay_resource_dir(separate, "/toolchain/lib/clang");
        EXPECT_EQ(separate, (std::vector<std::string>{"-resource-dir", "/project/clang"}));

        std::vector<std::string> attached{"-resource-dir=/project/clang"};
        append_semantic_replay_resource_dir(attached, "/toolchain/lib/clang");
        EXPECT_EQ(attached, (std::vector<std::string>{"-resource-dir=/project/clang"}));
    }

    TEST(SemanticReplayFlagsTest, IgnoresEmptyResourceDirectory) {
        std::vector<std::string> arguments{"-std=c++20"};
        append_semantic_replay_resource_dir(arguments, "");
        EXPECT_EQ(arguments, (std::vector<std::string>{"-std=c++20"}));
    }
}
