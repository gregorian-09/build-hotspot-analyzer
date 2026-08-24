#include <algorithm>
#include <gtest/gtest.h>

#include "bha/analyzers/build_session_analyzer.hpp"

namespace bha::analyzers::test {
    namespace {

        Timestamp at_seconds(const int seconds) {
            return Timestamp(std::chrono::system_clock::duration(std::chrono::seconds(seconds)));
        }

        BuildCommandEvent command(
            const std::string& id,
            const int start_seconds,
            const int duration_seconds
        ) {
            BuildCommandEvent event;
            event.id = id;
            event.role = BuildStepRole::Compile;
            event.start_time = at_seconds(start_seconds);
            event.duration = std::chrono::seconds(duration_seconds);
            event.timing_provenance.evidence = EvidenceKind::Observed;
            event.timing_provenance.producer = "cmake";
            event.timing_provenance.timing_domain = TimingDomain::WallClock;
            event.timing_provenance.timing_aggregation = TimingAggregation::Exclusive;
            return event;
        }

    }  // namespace

    TEST(BuildSessionAnalyzerTest, ReportsExactOverlapAndUnavailableCriticalPath) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        trace.build_session->commands = {
            command("compile-a", 0, 2),
            command("compile-b", 0, 1)
        };

        BuildSessionAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().build_session;
        EXPECT_EQ(analysis.total_commands, 2u);
        EXPECT_EQ(analysis.timed_commands, 2u);
        EXPECT_EQ(analysis.wall_clock_time, std::chrono::seconds(2));
        EXPECT_EQ(analysis.serial_time, std::chrono::seconds(3));
        EXPECT_EQ(analysis.peak_parallelism, 2u);
        EXPECT_DOUBLE_EQ(analysis.average_parallelism, 1.5);
        EXPECT_TRUE(analysis.critical_path.empty());

        const auto critical = std::ranges::find(
            analysis.metric_capabilities,
            "build.scheduler.critical_path",
            &MetricCapability::metric
        );
        ASSERT_NE(critical, analysis.metric_capabilities.end());
        EXPECT_EQ(critical->provenance.evidence, EvidenceKind::Unavailable);
    }

    TEST(BuildSessionAnalyzerTest, ZeroDurationCommandsDoNotUnderflowParallelism) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        trace.build_session->commands = {
            command("zero-duration", 0, 0),
            command("compile-a", 0, 1)
        };

        BuildSessionAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().build_session;
        EXPECT_EQ(analysis.total_commands, 2u);
        EXPECT_EQ(analysis.timed_commands, 2u);
        EXPECT_EQ(analysis.wall_clock_time, std::chrono::seconds(1));
        EXPECT_EQ(analysis.serial_time, std::chrono::seconds(1));
        EXPECT_EQ(analysis.peak_parallelism, 1u);
        EXPECT_DOUBLE_EQ(analysis.average_parallelism, 1.0);
    }

    TEST(BuildSessionAnalyzerTest, ComputesCriticalPathFromCompleteDependencies) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        trace.build_session->dependency_graph_complete = true;

        auto first = command("compile-a", 0, 2);
        auto second = command("compile-b", 2, 3);
        second.dependency_ids = {"compile-a"};
        auto independent = command("compile-c", 0, 1);
        trace.build_session->commands = {first, second, independent};

        BuildSessionAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().build_session;
        EXPECT_EQ(analysis.critical_path_time, std::chrono::seconds(5));
        ASSERT_EQ(analysis.critical_path.size(), 2u);
        EXPECT_EQ(analysis.critical_path[0], "compile-a");
        EXPECT_EQ(analysis.critical_path[1], "compile-b");

        const auto critical = std::ranges::find(
            analysis.metric_capabilities,
            "build.scheduler.critical_path",
            &MetricCapability::metric
        );
        ASSERT_NE(critical, analysis.metric_capabilities.end());
        EXPECT_EQ(critical->provenance.evidence, EvidenceKind::Derived);
    }

    TEST(BuildSessionAnalyzerTest, RejectsIncompleteCommandTiming) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        trace.build_session->commands = {command("compile-a", 0, 1), BuildCommandEvent{}};

        BuildSessionAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().build_session;
        EXPECT_EQ(analysis.total_commands, 2u);
        EXPECT_EQ(analysis.timed_commands, 1u);

        const auto parallelism = std::ranges::find(
            analysis.metric_capabilities,
            "build.scheduler.parallelism",
            &MetricCapability::metric
        );
        ASSERT_NE(parallelism, analysis.metric_capabilities.end());
        EXPECT_EQ(parallelism->provenance.evidence, EvidenceKind::Unavailable);
    }

    TEST(BuildSessionAnalyzerTest, ReportsProducerDefinedRoleMetrics) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        trace.build_session->host_system = BuildHostSystemInfo{};
        trace.build_session->host_system->os_name = "Linux";
        trace.build_session->host_system->logical_cpu_count = 16;
        trace.build_session->host_system->total_physical_memory_mib = 32768;

        auto configure = command("configure", 0, 2);
        configure.role = BuildStepRole::Configure;
        configure.result = 0;
        configure.before_host_memory_used_kib = 1024;
        configure.after_host_memory_used_kib = 2048;
        configure.before_cpu_load_average = 1.0;
        configure.after_cpu_load_average = 2.0;

        auto custom = command("custom", 2, 3);
        custom.role = BuildStepRole::Custom;
        custom.result = 7;
        custom.before_host_memory_used_kib = 4096;
        custom.after_host_memory_used_kib = 3072;
        custom.before_cpu_load_average = 3.0;
        custom.standard_output = "abc";
        custom.standard_error = "warning";

        auto test = command("test", 5, 4);
        test.role = BuildStepRole::Test;
        test.result = 0;
        test.after_cpu_load_average = 4.0;

        auto install = command("install", 9, 1);
        install.role = BuildStepRole::Install;

        trace.build_session->commands = {test, install, custom, configure};

        BuildSessionAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& steps = result.value().build_session.step_metrics;
        ASSERT_EQ(steps.size(), 4u);
        EXPECT_EQ(steps[0].role, BuildStepRole::Configure);
        EXPECT_EQ(steps[1].role, BuildStepRole::Custom);
        EXPECT_EQ(steps[2].role, BuildStepRole::Test);
        EXPECT_EQ(steps[3].role, BuildStepRole::Install);

        EXPECT_EQ(steps[0].total_commands, 1u);
        EXPECT_EQ(steps[0].wall_clock_time, std::chrono::seconds(2));
        EXPECT_EQ(steps[0].result_observations, 1u);
        EXPECT_EQ(steps[0].successful_commands, 1u);
        EXPECT_EQ(steps[0].failed_commands, 0u);

        EXPECT_EQ(steps[1].wall_clock_time, std::chrono::seconds(3));
        EXPECT_EQ(steps[1].failed_commands, 1u);
        EXPECT_EQ(steps[1].output_observations, 1u);
        ASSERT_TRUE(steps[1].stdout_bytes.has_value());
        EXPECT_EQ(*steps[1].stdout_bytes, 3u);
        ASSERT_TRUE(steps[1].stderr_bytes.has_value());
        EXPECT_EQ(*steps[1].stderr_bytes, 7u);
        EXPECT_EQ(steps[2].successful_commands, 1u);
        EXPECT_EQ(steps[3].result_observations, 0u);

        const auto& host = result.value().build_session.host_telemetry;
        EXPECT_EQ(host.memory_samples, 4u);
        ASSERT_TRUE(host.peak_memory_used_kib.has_value());
        EXPECT_EQ(*host.peak_memory_used_kib, 4096u);
        EXPECT_EQ(host.cpu_load_samples, 4u);
        ASSERT_TRUE(host.peak_before_cpu_load_average.has_value());
        EXPECT_DOUBLE_EQ(*host.peak_before_cpu_load_average, 3.0);
        ASSERT_TRUE(host.peak_after_cpu_load_average.has_value());
        EXPECT_DOUBLE_EQ(*host.peak_after_cpu_load_average, 4.0);
        ASSERT_TRUE(result.value().build_session.host_system.has_value());
        EXPECT_EQ(*result.value().build_session.host_system->os_name, "Linux");
        EXPECT_EQ(*result.value().build_session.host_system->logical_cpu_count, 16u);
        const auto host_capability = std::ranges::find_if(
            result.value().build_session.metric_capabilities,
            [](const MetricCapability& capability) {
                return capability.metric == "build.host.system";
            }
        );
        ASSERT_NE(host_capability, result.value().build_session.metric_capabilities.end());
        EXPECT_EQ(host_capability->provenance.evidence, EvidenceKind::Observed);

        const auto custom_result = std::ranges::find_if(
            result.value().build_session.metric_capabilities,
            [](const MetricCapability& capability) {
                return capability.metric == "build.step.result" &&
                    capability.provenance.scope == "role:custom";
            }
        );
        ASSERT_NE(custom_result, result.value().build_session.metric_capabilities.end());
        EXPECT_EQ(custom_result->provenance.evidence, EvidenceKind::Observed);

        const auto output_bytes = std::ranges::find_if(
            result.value().build_session.metric_capabilities,
            [](const MetricCapability& capability) {
                return capability.metric == "build.step.output_bytes" &&
                    capability.provenance.scope == "role:custom";
            }
        );
        ASSERT_NE(output_bytes, result.value().build_session.metric_capabilities.end());
        EXPECT_EQ(output_bytes->provenance.evidence, EvidenceKind::Derived);

        const auto install_result = std::ranges::find_if(
            result.value().build_session.metric_capabilities,
            [](const MetricCapability& capability) {
                return capability.metric == "build.step.result" &&
                    capability.provenance.scope == "role:install";
            }
        );
        ASSERT_NE(install_result, result.value().build_session.metric_capabilities.end());
        EXPECT_EQ(install_result->provenance.evidence, EvidenceKind::Unavailable);
    }

    TEST(BuildSessionAnalyzerTest, ReportsProducerCompileTraceReferences) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        auto compile = command("compile", 0, 1);
        compile.trace_file = "compile-trace/main.json";
        trace.build_session->commands = {compile};

        BuildSessionAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().build_session.compile_trace_references, 1u);
        const auto capability = std::ranges::find(
            result.value().build_session.metric_capabilities,
            "build.compile_trace.reference",
            &MetricCapability::metric
        );
        ASSERT_NE(capability, result.value().build_session.metric_capabilities.end());
        EXPECT_EQ(capability->provenance.evidence, EvidenceKind::Observed);
        EXPECT_EQ(capability->provenance.capture_mode, "api-v1.1-snippet");
    }

}  // namespace bha::analyzers::test
