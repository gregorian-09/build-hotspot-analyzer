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

}  // namespace bha::analyzers::test
