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

    TEST(CMakeInstrumentationParserTest, ParsesDynamicSystemInformation) {
        constexpr std::string_view content = R"json({
  "version": {"major": 1, "minor": 1},
  "role": "custom",
  "result": 0,
  "timeStart": 1737053448177,
  "duration": 31,
  "dynamicSystemInformation": {
    "beforeHostMemoryUsed": 1024.0,
    "afterHostMemoryUsed": 2048.0,
    "beforeCPULoadAverage": 1.5,
    "afterCPULoadAverage": null
  }
})json";

        CMakeInstrumentationParser parser;
        const auto result = parser.parse_content(content, "custom.json");

        ASSERT_TRUE(result.is_ok());
        const auto& event = result.value();
        ASSERT_TRUE(event.before_host_memory_used_kib.has_value());
        EXPECT_EQ(*event.before_host_memory_used_kib, 1024u);
        ASSERT_TRUE(event.after_host_memory_used_kib.has_value());
        EXPECT_EQ(*event.after_host_memory_used_kib, 2048u);
        ASSERT_TRUE(event.before_cpu_load_average.has_value());
        EXPECT_DOUBLE_EQ(*event.before_cpu_load_average, 1.5);
        EXPECT_FALSE(event.after_cpu_load_average.has_value());
    }

    TEST(CMakeInstrumentationParserTest, RejectsInvalidDynamicMemoryValue) {
        constexpr std::string_view content = R"json({
  "version": {"major": 1, "minor": 1},
  "role": "custom",
  "result": 0,
  "timeStart": 1737053448177,
  "duration": 31,
  "dynamicSystemInformation": {"beforeHostMemoryUsed": -1}
})json";

        CMakeInstrumentationParser parser;
        const auto result = parser.parse_content(content, "custom.json");

        EXPECT_TRUE(result.is_err());
    }

    TEST(CMakeInstrumentationParserTest, RejectsInvalidStaticSystemInformationValue) {
        const auto unique = std::to_string(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        const fs::path root = fs::temp_directory_path() / ("bha-cmake-static-invalid-" + unique);
        const fs::path index = root / "index.json";
        ASSERT_TRUE(fs::create_directories(root));
        constexpr std::string_view content = R"json({
  "version": {"major": 1, "minor": 1},
  "staticSystemInformation": {"numberOfLogicalCPU": "16"},
  "snippets": []
})json";
        ASSERT_TRUE(utils::write_file(index, content).is_ok());

        CMakeInstrumentationParser parser;
        const auto result = parser.parse_index_file(index);

        EXPECT_TRUE(result.is_err());
        std::error_code ec;
        fs::remove_all(root, ec);
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
  "staticSystemInformation": {
    "OSName": "Linux",
    "OSPlatform": "x86_64",
    "OSRelease": "Ubuntu",
    "OSVersion": "6.8",
    "is64Bits": true,
    "numberOfLogicalCPU": 16,
    "numberOfPhysicalCPU": 8,
    "totalPhysicalMemory": 32768,
    "totalVirtualMemory": 65536,
    "processorName": "Test CPU",
    "vendorString": "Test Vendor"
  },
  "snippets": ["compile.json"]
})json";
        ASSERT_TRUE(utils::write_file(index, index_content).is_ok());

        CMakeInstrumentationParser parser;
        const auto result = parser.parse_index_file(index);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().id, index.generic_string());
        EXPECT_EQ(result.value().instrumentation_hook, "postCMakeBuild");
        ASSERT_TRUE(result.value().host_system.has_value());
        EXPECT_EQ(*result.value().host_system->os_name, "Linux");
        EXPECT_EQ(*result.value().host_system->logical_cpu_count, 16u);
        EXPECT_EQ(*result.value().host_system->total_physical_memory_mib, 32768u);
        EXPECT_EQ(*result.value().host_system->is_64_bits, true);
        ASSERT_EQ(result.value().commands.size(), 1u);
        EXPECT_EQ(result.value().commands.front().source, fs::path("src/main.cpp"));
        ASSERT_EQ(result.value().metric_capabilities.size(), 1u);
        EXPECT_EQ(result.value().metric_capabilities.front().provenance.capture_mode, "api-v1-index");

        std::error_code ec;
        fs::remove_all(root, ec);
    }

}  // namespace bha::build_sessions::test
