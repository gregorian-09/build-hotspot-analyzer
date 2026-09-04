#include "bha/lsp/uri.hpp"

#include <filesystem>
#include <gtest/gtest.h>

namespace bha::lsp::uri {
    namespace fs = std::filesystem;

    TEST(UriTest, DecodesPercentEncodedWorkspacePath) {
        EXPECT_EQ(
            uri_to_path("file:///tmp/project%20name/include%23header.hpp"),
            fs::path("/tmp/project name/include#header.hpp")
        );
    }

    TEST(UriTest, RoundTripsAbsolutePathWithReservedCharacters) {
        const fs::path original = fs::absolute("bha-uri-project name/include#header.hpp").lexically_normal();
        const std::string encoded = path_to_uri(original);

        EXPECT_TRUE(encoded.starts_with("file://"));
        EXPECT_NE(encoded.find("bha-uri-project%20name/include%23header.hpp"), std::string::npos);
        EXPECT_EQ(uri_to_path(encoded), original);
    }
}
