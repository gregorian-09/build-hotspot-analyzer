#include "bha/analyzers/process_resource_analyzer.hpp"

#include <gtest/gtest.h>

namespace bha::analyzers::test {

    TEST(ProcessResourceAnalyzerTest, AggregatesExactProducerCounters) {
        BuildTrace trace;
        trace.process_resource_report = ProcessResourceReport{};
        trace.process_resource_report->observations = {
            {"clang", "one.o", std::chrono::milliseconds(92), std::chrono::milliseconds(84), 87536},
            {"ld", "app", std::chrono::milliseconds(9), std::chrono::milliseconds(8), 53568}
        };
        trace.process_resource_report->metric_capabilities.push_back({
            "process.resource_counters",
            MetricProvenance{
                EvidenceKind::Observed,
                "clang",
                "",
                "-fproc-stat-report=FILE",
                "build",
                TimingDomain::WallClock,
                TimingAggregation::Exclusive,
                "Rows identify tool invocations and output paths"
            }
        });

        ProcessResourceAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& resources = result.value().process_resources;
        EXPECT_EQ(resources.observations, 2u);
        EXPECT_EQ(resources.total_process_time, std::chrono::milliseconds(101));
        EXPECT_EQ(resources.total_user_time, std::chrono::milliseconds(92));
        EXPECT_EQ(resources.peak_memory_kib, 87536u);
        ASSERT_EQ(resources.metric_capabilities.size(), 1u);
        EXPECT_EQ(resources.metric_capabilities.front().metric, "process.resource_counters");
    }

    TEST(ProcessResourceAnalyzerTest, IsUnavailableWithoutProducerReport) {
        ProcessResourceAnalyzer analyzer;
        const auto result = analyzer.analyze(BuildTrace{}, {});

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().process_resources.observations, 0u);
        EXPECT_TRUE(result.value().process_resources.metric_capabilities.empty());
    }

}  // namespace bha::analyzers::test
