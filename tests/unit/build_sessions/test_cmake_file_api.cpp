#include <gtest/gtest.h>

#include "bha/build_sessions/cmake_file_api.hpp"
#include "bha/utils/file_utils.hpp"

#include <chrono>
#include <filesystem>

namespace bha::build_sessions::test {
    namespace {

        fs::path make_fixture_root() {
            return fs::temp_directory_path() / (
                "bha-cmake-file-api-fixture-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            );
        }

        void write_fixture(const fs::path& root, const bool multiple_configurations) {
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root, ec);

            constexpr std::string_view index = R"json({
  "cmake": {"version": {"string": "3.28.3"}},
  "objects": [
    {"kind": "codemodel", "version": {"major": 2, "minor": 8}, "jsonFile": "codemodel.json"}
  ]
})json";
            constexpr std::string_view target = R"json({
  "kind": "codemodel",
  "version": {"major": 2, "minor": 8},
  "name": "app",
  "id": "app::@123",
  "type": "EXECUTABLE",
  "paths": {
    "source": ".",
    "build": "app",
    "nameOnDisk": "app",
    "artifacts": [{"path": "app"}]
  },
  "link": {"language": "CXX", "lto": true},
  "dependencies": [{"id": "lib::@456"}],
  "precompileHeaders": [{"header": "/src/pch.h", "backtrace": 0}]
})json";
            const auto codemodel = multiple_configurations
                ? std::string(R"json({
  "kind": "codemodel",
  "version": {"major": 2, "minor": 8},
  "paths": {"source": "/src", "build": "/build"},
  "configurations": [
    {"name": "Debug", "targets": [{"name": "app", "id": "app::@123", "jsonFile": "target.json"}]},
    {"name": "Release", "targets": [{"name": "app", "id": "app::@123", "jsonFile": "target.json"}]}
  ]
})json")
                : std::string(R"json({
  "kind": "codemodel",
  "version": {"major": 2, "minor": 8},
  "paths": {"source": "/src", "build": "/build"},
  "configurations": [
    {"name": "Debug", "targets": [{"name": "app", "id": "app::@123", "jsonFile": "target.json"}]}
  ]
})json");

            ASSERT_TRUE(utils::write_file(root / "index.json", index).is_ok());
            ASSERT_TRUE(utils::write_file(root / "target.json", target).is_ok());
            ASSERT_TRUE(utils::write_file(root / "codemodel.json", codemodel).is_ok());
        }

    }  // namespace

    TEST(CMakeFileApiParserTest, FollowsReplyReferencesAndParsesTargetOwnership) {
        const fs::path root = make_fixture_root();
        write_fixture(root, false);

        CMakeFileApiParser parser;
        const auto result = parser.parse_reply_index(root / "index.json");

        ASSERT_TRUE(result.is_ok());
        const auto& graph = result.value();
        EXPECT_TRUE(graph.complete);
        EXPECT_EQ(graph.producer_version, "3.28.3");
        EXPECT_EQ(graph.configuration, "Debug");
        EXPECT_EQ(graph.source_root, fs::path("/src"));
        EXPECT_EQ(graph.build_root, fs::path("/build"));
        ASSERT_EQ(graph.targets.size(), 1u);
        const auto& target = graph.targets.front();
        EXPECT_EQ(target.id, "app::@123");
        EXPECT_EQ(target.name, "app");
        EXPECT_EQ(target.type, "EXECUTABLE");
        EXPECT_EQ(target.source_directory, fs::path("/src"));
        EXPECT_EQ(target.build_directory, fs::path("/build/app"));
        EXPECT_EQ(target.name_on_disk, fs::path("/build/app/app"));
        ASSERT_EQ(target.artifacts.size(), 1u);
        EXPECT_EQ(target.artifacts.front(), fs::path("/build/app"));
        EXPECT_TRUE(target.lto_enabled);
        ASSERT_EQ(target.dependencies.size(), 1u);
        EXPECT_EQ(target.dependencies.front(), "lib::@456");
        ASSERT_EQ(target.precompile_headers.size(), 1u);
        EXPECT_EQ(target.precompile_headers.front(), fs::path("/src/pch.h"));
        ASSERT_EQ(graph.metric_capabilities.size(), 1u);
        EXPECT_EQ(graph.metric_capabilities.front().provenance.evidence, EvidenceKind::Observed);

        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TEST(CMakeFileApiParserTest, RejectsAmbiguousConfigurationWithoutSelection) {
        const fs::path root = make_fixture_root();
        write_fixture(root, true);

        CMakeFileApiParser parser;
        const auto result = parser.parse_reply_index(root / "index.json");

        EXPECT_TRUE(result.is_err());

        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TEST(CMakeFileApiParserTest, SelectsRequestedConfiguration) {
        const fs::path root = make_fixture_root();
        write_fixture(root, true);

        CMakeFileApiParser parser;
        const auto result = parser.parse_reply_index(root / "index.json", "Release");

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().configuration, "Release");

        std::error_code ec;
        fs::remove_all(root, ec);
    }

}  // namespace bha::build_sessions::test
