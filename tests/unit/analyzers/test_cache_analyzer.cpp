#include "bha/analyzers/cache_analyzer.hpp"

#include <gtest/gtest.h>

namespace bha::analyzers {

    TEST(CacheAnalyzerTest, ReportsObservedCountersAndDerivedHitRate) {
        BuildTrace trace;
        CacheStatistics stats;
        stats.producer = "sccache";
        stats.producer_version = "0.14.0";
        stats.compile_requests = 12;
        stats.executed_requests = 10;
        stats.compilations = 10;
        stats.cache_hits = 7;
        stats.cache_misses = 3;
        stats.cache_errors = 1;
        MetricCapability observed;
        observed.metric = "cache.outcomes";
        observed.provenance.evidence = EvidenceKind::Observed;
        observed.provenance.producer = "sccache";
        stats.metric_capabilities.push_back(observed);
        trace.cache_statistics = stats;

        const CacheAnalyzer analyzer;
        constexpr AnalysisOptions options;
        const auto result = analyzer.analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& cache = result.value().cache_distribution;
        EXPECT_EQ(cache.compile_requests, 12u);
        EXPECT_EQ(cache.executed_compilations, 10u);
        EXPECT_EQ(cache.cache_hits, 7u);
        EXPECT_EQ(cache.cache_misses, 3u);
        ASSERT_TRUE(cache.hit_rate_percent.has_value());
        EXPECT_DOUBLE_EQ(*cache.hit_rate_percent, 70.0);
        ASSERT_EQ(cache.metric_capabilities.size(), 2u);
        EXPECT_EQ(cache.metric_capabilities.front().metric, "cache.outcomes");
        EXPECT_EQ(cache.metric_capabilities.back().metric, "cache.hit_rate");
        EXPECT_EQ(cache.metric_capabilities.back().provenance.evidence, EvidenceKind::Derived);
    }

    TEST(CacheAnalyzerTest, MarksHitRateUnavailableWithoutOutcomes) {
        BuildTrace trace;
        CacheStatistics stats;
        stats.producer = "sccache";
        stats.producer_version = "0.14.0";
        stats.compile_requests = 1;
        stats.compilations = 1;
        trace.cache_statistics = stats;

        const CacheAnalyzer analyzer;
        constexpr AnalysisOptions options;
        const auto result = analyzer.analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& cache = result.value().cache_distribution;
        EXPECT_FALSE(cache.hit_rate_percent.has_value());
        ASSERT_EQ(cache.metric_capabilities.size(), 1u);
        EXPECT_EQ(cache.metric_capabilities.front().metric, "cache.hit_rate");
        EXPECT_EQ(cache.metric_capabilities.front().provenance.evidence, EvidenceKind::Unavailable);
    }

}  // namespace bha::analyzers
