#include <gtest/gtest.h>

#include "bha/analyzers/linker_analyzer.hpp"

#include <algorithm>

namespace bha::analyzers::test {
    namespace {

        BuildCommandEvent link_command(
            const std::string& id,
            const int start_seconds,
            const int duration_seconds
        ) {
            BuildCommandEvent event;
            event.id = id;
            event.role = BuildStepRole::Link;
            event.start_time = Timestamp(
                std::chrono::system_clock::duration(std::chrono::seconds(start_seconds))
            );
            event.duration = std::chrono::seconds(duration_seconds);
            event.timing_provenance.evidence = EvidenceKind::Observed;
            event.timing_provenance.producer = "cmake-instrumentation";
            event.timing_provenance.timing_domain = TimingDomain::WallClock;
            event.timing_provenance.timing_aggregation = TimingAggregation::Exclusive;
            return event;
        }

        const MetricCapability* find_capability(
            const LinkerAnalysisResult& analysis,
            const std::string_view metric
        ) {
            const auto it = std::ranges::find(
                analysis.metric_capabilities,
                metric,
                &MetricCapability::metric
            );
            return it == analysis.metric_capabilities.end() ? nullptr : &*it;
        }

    }  // namespace

    TEST(LinkerAnalyzerTest, ReportsExactTimingAndOutputBytes) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        auto event = link_command("link-app", 0, 4);
        event.outputs = {"app"};
        event.output_sizes = {4096};
        trace.build_session->commands = {event};

        LinkerAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().linker;
        EXPECT_EQ(analysis.invocations, 1u);
        EXPECT_EQ(analysis.timed_invocations, 1u);
        EXPECT_EQ(analysis.output_size_observations, 1u);
        EXPECT_EQ(analysis.wall_clock_time, std::chrono::seconds(4));
        EXPECT_EQ(analysis.output_bytes, 4096u);

        const auto* wall_time = find_capability(analysis, "link.wall_time");
        ASSERT_NE(wall_time, nullptr);
        EXPECT_EQ(wall_time->provenance.evidence, EvidenceKind::Derived);

        const auto* output_bytes = find_capability(analysis, "link.output_bytes");
        ASSERT_NE(output_bytes, nullptr);
        EXPECT_EQ(output_bytes->provenance.evidence, EvidenceKind::Derived);
        EXPECT_TRUE(find_capability(analysis, "link.input_bytes") != nullptr);
        EXPECT_TRUE(find_capability(analysis, "lto.wall_time") != nullptr);
    }

    TEST(LinkerAnalyzerTest, FailsClosedForMissingProducerFields) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        auto event = link_command("link-app", 0, 4);
        event.start_time.reset();
        event.outputs = {"app"};
        event.output_sizes.clear();
        trace.build_session->commands = {event};

        LinkerAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().linker;
        EXPECT_EQ(analysis.invocations, 1u);
        EXPECT_EQ(analysis.timed_invocations, 0u);
        EXPECT_EQ(analysis.output_size_observations, 0u);
        EXPECT_EQ(analysis.wall_clock_time, Duration::zero());
        EXPECT_EQ(analysis.output_bytes, 0u);

        const auto* wall_time = find_capability(analysis, "link.wall_time");
        ASSERT_NE(wall_time, nullptr);
        EXPECT_EQ(wall_time->provenance.evidence, EvidenceKind::Unavailable);

        const auto* output_bytes = find_capability(analysis, "link.output_bytes");
        ASSERT_NE(output_bytes, nullptr);
        EXPECT_EQ(output_bytes->provenance.evidence, EvidenceKind::Unavailable);
    }

    TEST(LinkerAnalyzerTest, IgnoresNonLinkCommands) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        auto compile = link_command("compile-main", 0, 4);
        compile.role = BuildStepRole::Compile;
        trace.build_session->commands = {compile};

        LinkerAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().linker.invocations, 0u);
        EXPECT_TRUE(result.value().linker.metric_capabilities.empty());
    }

    TEST(LinkerAnalyzerTest, UsesAttachedLinkerTraceForLtoEvidence) {
        BuildTrace trace;
        trace.linker_trace = LinkerTrace{};
        trace.linker_trace->execute_linker_time = std::chrono::milliseconds(20);
        trace.linker_trace->lto_time = std::chrono::milliseconds(12);

        LinkerAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().linker;
        EXPECT_EQ(analysis.invocations, 1u);
        ASSERT_TRUE(analysis.trace_wall_clock_time.has_value());
        EXPECT_EQ(*analysis.trace_wall_clock_time, std::chrono::milliseconds(20));
        ASSERT_TRUE(analysis.lto_time.has_value());
        EXPECT_EQ(*analysis.lto_time, std::chrono::milliseconds(12));

        const auto* lto = find_capability(analysis, "lto.wall_time");
        ASSERT_NE(lto, nullptr);
        EXPECT_EQ(lto->provenance.evidence, EvidenceKind::Observed);
        EXPECT_EQ(lto->provenance.producer, "lld");

        const auto* trace_wall_time = find_capability(analysis, "linker.trace.wall_time");
        ASSERT_NE(trace_wall_time, nullptr);
        EXPECT_EQ(trace_wall_time->provenance.evidence, EvidenceKind::Observed);
    }

    TEST(LinkerAnalyzerTest, FailsClosedWhenWallTimeAggregationOverflows) {
        BuildTrace trace;
        trace.build_session = BuildSession{};
        auto first = link_command("first", 0, 1);
        first.duration = Duration::max();
        auto second = link_command("second", 0, 1);
        second.duration = Duration(1);
        trace.build_session->commands = {first, second};

        LinkerAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().linker;
        EXPECT_EQ(analysis.timed_invocations, 2u);
        EXPECT_EQ(analysis.wall_clock_time, Duration::zero());
        const auto* wall_time = find_capability(analysis, "link.wall_time");
        ASSERT_NE(wall_time, nullptr);
        EXPECT_EQ(wall_time->provenance.evidence, EvidenceKind::Unavailable);
        EXPECT_FALSE(wall_time->provenance.limitation.empty());
    }

}  // namespace bha::analyzers::test
