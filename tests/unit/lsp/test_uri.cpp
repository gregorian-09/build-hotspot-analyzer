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
        const fs::path original("/tmp/project name/include#header.hpp");
        const std::string encoded = path_to_uri(original);

        EXPECT_EQ(encoded, "file:///tmp/project%20name/include%23header.hpp");
        EXPECT_EQ(uri_to_path(encoded), original);
    }
}
