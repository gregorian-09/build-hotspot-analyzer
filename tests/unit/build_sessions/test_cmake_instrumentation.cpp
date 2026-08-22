#include <gtest/gtest.h>

#include "bha/build_sessions/cmake_instrumentation.hpp"
#include "bha/utils/file_utils.hpp"

#include <chrono>
#include <filesystem>

namespace bha::build_sessions::test {

    TEST(CMakeInstrumentationParserTest, ParsesObservedCompileSnippet) {
        constexpr std::string_view content = R"json({
  "version": {"major": 1, "minor": 1},
  "role": "compile",
  "result": 1,
  "command": "clang++ -c src/main.cpp",
  "workingDir": "/build",
  "target": "library",
  "language": "CXX",
  "outputs": ["CMakeFiles/library.dir/main.cpp.o"],
  "outputSizes": [2048],
  "source": "src/main.cpp",
  "config": "Debug",
  "timeStart": 1737053448177,
  "duration": 31
})json";

        CMakeInstrumentationParser parser;
        const auto result = parser.parse_content(content, "compile.json");

        ASSERT_TRUE(result.is_ok());
        const auto& event = result.value();
        EXPECT_EQ(event.role, BuildStepRole::Compile);
        EXPECT_EQ(event.target, "library");
        EXPECT_EQ(event.language, "CXX");
        EXPECT_EQ(event.source, fs::path("src/main.cpp"));
        EXPECT_EQ(event.command, "clang++ -c src/main.cpp");
        EXPECT_EQ(event.working_directory, fs::path("/build"));
        ASSERT_EQ(event.outputs.size(), 1u);
        EXPECT_EQ(event.outputs.front(), fs::path("CMakeFiles/library.dir/main.cpp.o"));
        ASSERT_EQ(event.output_sizes.size(), 1u);
        EXPECT_EQ(event.output_sizes.front(), 2048u);
        ASSERT_TRUE(event.result.has_value());
        EXPECT_EQ(*event.result, 1);
        EXPECT_EQ(event.configuration, "Debug");
        EXPECT_TRUE(event.has_exact_timing());
        EXPECT_EQ(event.duration, std::chrono::milliseconds(31));
        EXPECT_EQ(event.timing_provenance.evidence, EvidenceKind::Observed);
        EXPECT_EQ(event.timing_provenance.timing_domain, TimingDomain::WallClock);
    }

    TEST(CMakeInstrumentationParserTest, RejectsMissingTiming) {
        constexpr std::string_view content = R"json({
  "version": {"major": 1, "minor": 1},
  "role":"link",
  "target":"app"
})json";

        CMakeInstrumentationParser parser;
        const auto result = parser.parse_content(content, "link.json");

        EXPECT_TRUE(result.is_err());
    }

    TEST(CMakeInstrumentationParserTest, RejectsUnsupportedDataVersion) {
        constexpr std::string_view content = R"json({
  "version": {"major": 2, "minor": 0},
  "role": "link",
  "result": 0,
  "timeStart": 1737053448177,
  "duration": 31
})json";

        CMakeInstrumentationParser parser;
        const auto result = parser.parse_content(content, "link.json");

        EXPECT_TRUE(result.is_err());
    }

    TEST(CMakeInstrumentationParserTest, AcceptsNullResultForWholeBuild) {
        constexpr std::string_view content = R"json({
  "version": {"major": 1, "minor": 1},
  "role": "build",
  "result": null,
  "timeStart": 1737053448177,
  "duration": 31
})json";

        CMakeInstrumentationParser parser;
        const auto result = parser.parse_content(content, "build.json");

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().role, BuildStepRole::Build);
        EXPECT_FALSE(result.value().result.has_value());
    }

    TEST(CMakeInstrumentationParserTest, UsesIndexAsSessionBoundary) {
        const auto unique = std::to_string(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        const fs::path root = fs::temp_directory_path() / ("bha-cmake-index-" + unique);
        const fs::path data = root / "data";
        const fs::path index = root / "index.json";
        ASSERT_TRUE(fs::create_directories(data));

        constexpr std::string_view snippet = R"json({
  "version": {"major": 1, "minor": 1},
  "role": "compile",
  "result": 0,
  "timeStart": 1737053448177,
  "duration": 31,
  "source": "src/main.cpp"
})json";
        ASSERT_TRUE(utils::write_file(data / "compile.json", snippet).is_ok());
        const auto index_content = std::string(R"json({
  "version": {"major": 1, "minor": 1},
  "dataDir": ")json") + data.string() + R"json(",
  "hook": "postCMakeBuild",
  "snippets": ["compile.json"]
})json";
        ASSERT_TRUE(utils::write_file(index, index_content).is_ok());

        CMakeInstrumentationParser parser;
        const auto result = parser.parse_index_file(index);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().id, index.generic_string());
        EXPECT_EQ(result.value().instrumentation_hook, "postCMakeBuild");
        ASSERT_EQ(result.value().commands.size(), 1u);
        EXPECT_EQ(result.value().commands.front().source, fs::path("src/main.cpp"));
        ASSERT_EQ(result.value().metric_capabilities.size(), 1u);
        EXPECT_EQ(result.value().metric_capabilities.front().provenance.capture_mode, "api-v1-index");

        std::error_code ec;
        fs::remove_all(root, ec);
    }

}  // namespace bha::build_sessions::test
