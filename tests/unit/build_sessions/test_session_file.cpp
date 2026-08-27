#include <gtest/gtest.h>

#include "bha/build_sessions/session_file.hpp"
#include "bha/utils/file_utils.hpp"

#include <chrono>
#include <filesystem>

namespace bha::build_sessions::test {

    TEST(BuildSessionFileParserTest, RoundTripsObservedCommandTiming) {
        BuildSession session;
        session.id = "build-1";
        session.build_system = BuildSystemType::MSBuild;
        session.build_system_version = "17.0";
        session.configuration = "Release";
        session.platform = "x64";
        session.instrumentation_hook = "bha-adapter-wall-clock";
        session.host_system = BuildHostSystemInfo{};
        session.host_system->os_name = "Windows";
        session.host_system->logical_cpu_count = 16;

        MetricCapability capability;
        capability.metric = "build.command.wall_time";
        capability.provenance.evidence = EvidenceKind::Observed;
        capability.provenance.producer = "MSBuild";
        capability.provenance.timing_domain = TimingDomain::WallClock;
        capability.provenance.timing_aggregation = TimingAggregation::Exclusive;
        session.metric_capabilities.push_back(capability);

        BuildCommandEvent command;
        command.id = "build";
        command.role = BuildStepRole::Build;
        command.command = "MSBuild project.sln";
        command.working_directory = "C:/src/project";
        command.start_time = Timestamp(std::chrono::milliseconds(1000));
        command.duration = std::chrono::milliseconds(2500);
        command.result = 0;
        command.trace_file = "compile/use_box.json";
        command.outputs = {"build/use_box.obj"};
        command.output_sizes = {4096};
        command.test_name = "compile-test";
        command.before_host_memory_used_kib = 1000;
        command.after_host_memory_used_kib = 1200;
        command.before_cpu_load_average = 1.25;
        command.after_cpu_load_average = 2.5;
        command.standard_output = "stdout";
        command.standard_error = "stderr";
        command.dependency_ids = {"compile-0"};
        command.timing_provenance.evidence = EvidenceKind::Observed;
        command.timing_provenance.producer = "MSBuild";
        command.timing_provenance.capture_mode = "structured-log";
        command.timing_provenance.scope = "compile";
        command.timing_provenance.timing_domain = TimingDomain::WallClock;
        command.timing_provenance.timing_aggregation = TimingAggregation::Exclusive;
        session.commands.push_back(command);

        const fs::path path = fs::temp_directory_path() / "bha-build-session-test.json";
        BuildSessionFileParser parser;
        const auto document = parser.write_file(session, path);
        ASSERT_TRUE(document.is_ok()) << document.error().message();

        const auto content = utils::read_file(path);
        ASSERT_TRUE(content.is_ok()) << content.error().message();
        const auto parsed = parser.parse_content(content.value(), path);

        ASSERT_TRUE(parsed.is_ok()) << parsed.error().message();
        ASSERT_EQ(parsed.value().commands.size(), 1u);
        EXPECT_EQ(parsed.value().build_system, BuildSystemType::MSBuild);
        EXPECT_EQ(parsed.value().build_system_version, "17.0");
        ASSERT_TRUE(parsed.value().host_system.has_value());
        EXPECT_EQ(parsed.value().host_system->logical_cpu_count, 16u);
        EXPECT_EQ(parsed.value().commands.front().duration, std::chrono::milliseconds(2500));
        EXPECT_EQ(parsed.value().commands.front().trace_file, fs::path("compile/use_box.json"));
        EXPECT_EQ(parsed.value().commands.front().output_sizes.front(), 4096u);
        EXPECT_EQ(parsed.value().commands.front().after_cpu_load_average, 2.5);
        EXPECT_EQ(parsed.value().commands.front().timing_provenance.producer, "MSBuild");
        EXPECT_EQ(parsed.value().commands.front().timing_provenance.evidence, EvidenceKind::Observed);
        EXPECT_EQ(parsed.value().metric_capabilities.front().provenance.timing_domain, TimingDomain::WallClock);

        std::error_code ec;
        fs::remove(path, ec);
    }

    TEST(BuildSessionFileParserTest, RejectsMalformedSchema) {
        BuildSessionFileParser parser;
        const auto result = parser.parse_content(R"json({"schema":"other","version":1})json");
        EXPECT_TRUE(result.is_err());
    }

}  // namespace bha::build_sessions::test
