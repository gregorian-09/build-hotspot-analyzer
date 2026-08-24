#include <gtest/gtest.h>

#include "bha/analyzers/build_target_analyzer.hpp"

#include <algorithm>

namespace bha::analyzers::test {
    namespace {

        BuildCommandEvent command(
            const BuildStepRole role,
            const std::string& target,
            const int start_seconds,
            const int duration_seconds
        ) {
            BuildCommandEvent event;
            event.id = target + "-command";
            event.role = role;
            event.target = target;
            event.start_time = Timestamp(
                std::chrono::system_clock::duration(std::chrono::seconds(start_seconds))
            );
            event.duration = std::chrono::seconds(duration_seconds);
            event.outputs = {target + ".out"};
            event.output_sizes = {1024};
            event.timing_provenance.evidence = EvidenceKind::Observed;
            event.timing_provenance.producer = "cmake-instrumentation";
            event.timing_provenance.timing_domain = TimingDomain::WallClock;
            event.timing_provenance.timing_aggregation = TimingAggregation::Exclusive;
            return event;
        }

        const MetricCapability* find_capability(
            const BuildTargetAnalysisResult& analysis,
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

    TEST(BuildTargetAnalyzerTest, JoinsOnlyExactProducerTargetNames) {
        BuildTrace trace;
        trace.target_graph = BuildTargetGraph{};
        trace.target_graph->complete = true;
        trace.target_graph->targets = {
            BuildTarget{"app-id", "app", "EXECUTABLE", {}, {}, {}, {}, "CXX", false, {"lib-id"}, {"/src/pch.h"}},
            BuildTarget{"lib-id", "lib", "STATIC_LIBRARY", {}, {}, {}, {}, "CXX", false, {}, {}}
        };
        trace.build_session = BuildSession{};
        trace.build_session->commands = {
            command(BuildStepRole::Compile, "app", 0, 2),
            command(BuildStepRole::Link, "app", 2, 3),
            command(BuildStepRole::Compile, "unknown", 0, 1)
        };

        BuildTargetAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().targets;
        ASSERT_EQ(analysis.targets.size(), 2u);
        EXPECT_EQ(analysis.target_commands, 3u);
        EXPECT_EQ(analysis.matched_commands, 2u);
        EXPECT_EQ(analysis.unmatched_commands, 1u);
        EXPECT_EQ(analysis.targets[0].compile_commands, 1u);
        EXPECT_EQ(analysis.targets[0].timed_compile_commands, 1u);
        EXPECT_EQ(analysis.targets[0].compile_wall_clock_time, std::chrono::seconds(2));
        EXPECT_EQ(analysis.targets[0].link_commands, 1u);
        EXPECT_EQ(analysis.targets[0].link_wall_clock_time, std::chrono::seconds(3));
        EXPECT_EQ(analysis.targets[0].output_bytes, 2048u);
        EXPECT_EQ(analysis.targets[0].dependencies, std::vector<std::string>{"lib-id"});
        ASSERT_EQ(analysis.pch_targets, 1u);
        ASSERT_EQ(analysis.pch_headers, 1u);
        EXPECT_EQ(analysis.targets[0].precompile_headers, std::vector<fs::path>{"/src/pch.h"});

        const auto* ownership = find_capability(analysis, "build.target.command_ownership");
        ASSERT_NE(ownership, nullptr);
        EXPECT_EQ(ownership->provenance.evidence, EvidenceKind::Derived);
        EXPECT_FALSE(ownership->provenance.limitation.empty());
        const auto* pch = find_capability(analysis, "build.target.pch_declarations");
        ASSERT_NE(pch, nullptr);
        EXPECT_EQ(pch->provenance.evidence, EvidenceKind::Derived);
    }

    TEST(BuildTargetAnalyzerTest, FailsClosedWithoutProducerTargetFields) {
        BuildTrace trace;
        trace.target_graph = BuildTargetGraph{};
        trace.target_graph->complete = true;
        trace.target_graph->targets = {
            BuildTarget{"app-id", "app", "EXECUTABLE", {}, {}, {}, {}, "CXX", false, {}, {}}
        };
        trace.build_session = BuildSession{};
        auto event = command(BuildStepRole::Compile, "app", 0, 1);
        event.target.clear();
        trace.build_session->commands = {event};

        BuildTargetAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().targets;
        EXPECT_EQ(analysis.target_commands, 0u);
        EXPECT_EQ(analysis.matched_commands, 0u);
        const auto* ownership = find_capability(analysis, "build.target.command_ownership");
        ASSERT_NE(ownership, nullptr);
        EXPECT_EQ(ownership->provenance.evidence, EvidenceKind::Unavailable);
    }

    TEST(BuildTargetAnalyzerTest, RejectsAmbiguousTargetNames) {
        BuildTrace trace;
        trace.target_graph = BuildTargetGraph{};
        trace.target_graph->complete = true;
        trace.target_graph->targets = {
            BuildTarget{"app-id-1", "app", "EXECUTABLE", {}, {}, {}, {}, "CXX", false, {}, {}},
            BuildTarget{"app-id-2", "app", "EXECUTABLE", {}, {}, {}, {}, "CXX", false, {}, {}}
        };
        trace.build_session = BuildSession{};
        trace.build_session->commands = {command(BuildStepRole::Compile, "app", 0, 1)};

        BuildTargetAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().targets;
        EXPECT_EQ(analysis.matched_commands, 0u);
        EXPECT_EQ(analysis.unmatched_commands, 1u);
        const auto* ownership = find_capability(analysis, "build.target.command_ownership");
        ASSERT_NE(ownership, nullptr);
        EXPECT_EQ(ownership->provenance.evidence, EvidenceKind::Unavailable);
    }

    TEST(BuildTargetAnalyzerTest, FailsClosedWhenTargetDurationAggregationOverflows) {
        BuildTrace trace;
        trace.target_graph = BuildTargetGraph{};
        trace.target_graph->complete = true;
        trace.target_graph->targets = {
            BuildTarget{"app-id", "app", "EXECUTABLE", {}, {}, {}, {}, "CXX", false, {}, {}}
        };
        trace.build_session = BuildSession{};
        auto first = command(BuildStepRole::Compile, "app", 0, 1);
        first.duration = Duration::max();
        auto second = command(BuildStepRole::Compile, "app", 0, 1);
        second.duration = Duration(1);
        trace.build_session->commands = {first, second};

        BuildTargetAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().targets;
        ASSERT_EQ(analysis.targets.size(), 1u);
        EXPECT_EQ(analysis.targets.front().timed_compile_commands, 2u);
        EXPECT_EQ(analysis.targets.front().compile_wall_clock_time, Duration::zero());
        const auto* compile_time = find_capability(analysis, "build.target.compile_wall_time");
        ASSERT_NE(compile_time, nullptr);
        EXPECT_EQ(compile_time->provenance.evidence, EvidenceKind::Unavailable);
        EXPECT_FALSE(compile_time->provenance.limitation.empty());
    }

}  // namespace bha::analyzers::test
