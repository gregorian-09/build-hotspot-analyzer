#include "bha/parsers/sccache_stats_parser.hpp"

#include <gtest/gtest.h>

namespace bha::parsers {

    TEST(SccacheStatsParserTest, ParsesProducerCountersAndLanguageOutcomes) {
        constexpr std::string_view content = R"json({
            "version": "0.14.0",
            "stats": {
                "compile_requests": 12,
                "requests_executed": 10,
                "requests_not_compile": 2,
                "requests_unsupported_compiler": 1,
                "requests_not_cacheable": 1,
                "compilations": 10,
                "cache_hits": {
                    "counts": {"C/C++": 7}
                },
                "cache_misses": {
                    "counts": {"C/C++": 3}
                },
                "cache_errors": {
                    "counts": {"C/C++": 1}
                },
                "cache_timeouts": 2,
                "cache_read_errors": 3,
                "non_cacheable_compilations": 1,
                "forced_recaches": 2,
                "cache_write_errors": 1,
                "cache_writes": 8,
                "compile_fails": 1
            }
        })json";

        const SccacheStatsParser parser;
        const auto result = parser.parse_content(content, "sccache-stats.json");

        ASSERT_TRUE(result.is_ok());
        const auto& stats = result.value();
        EXPECT_EQ(stats.producer, "sccache");
        EXPECT_EQ(stats.producer_version, "0.14.0");
        EXPECT_EQ(stats.compile_requests, 12u);
        EXPECT_EQ(stats.executed_requests, 10u);
        EXPECT_EQ(stats.non_compilation_requests, 2u);
        EXPECT_EQ(stats.unsupported_compiler_requests, 1u);
        EXPECT_EQ(stats.non_cacheable_requests, 1u);
        EXPECT_EQ(stats.compilations, 10u);
        EXPECT_EQ(stats.cache_hits, 7u);
        EXPECT_EQ(stats.cache_misses, 3u);
        EXPECT_EQ(stats.cache_errors, 1u);
        EXPECT_EQ(stats.cache_timeouts, 2u);
        EXPECT_EQ(stats.cache_read_errors, 3u);
        EXPECT_EQ(stats.non_cacheable_compilations, 1u);
        EXPECT_EQ(stats.forced_recaches, 2u);
        EXPECT_EQ(stats.cache_write_errors, 1u);
        EXPECT_EQ(stats.cache_writes, 8u);
        EXPECT_EQ(stats.compilation_failures, 1u);
        ASSERT_EQ(stats.metric_capabilities.size(), 1u);
        EXPECT_EQ(stats.metric_capabilities.front().metric, "cache.outcomes");
        EXPECT_EQ(stats.metric_capabilities.front().provenance.evidence, EvidenceKind::Observed);
    }

    TEST(SccacheStatsParserTest, RejectsMissingOrMalformedCounters) {
        constexpr std::string_view content = R"json({
            "version": "0.14.0",
            "stats": {
                "compile_requests": 1,
                "requests_executed": 1,
                "requests_not_compile": 0,
                "requests_unsupported_compiler": 0,
                "requests_not_cacheable": 0,
                "compilations": 1,
                "cache_hits": {"counts": {"C/C++": "not-a-count"}},
                "cache_misses": {"counts": {"C/C++": 0}},
                "cache_errors": {"counts": {"C/C++": 0}},
                "cache_timeouts": 0,
                "cache_read_errors": 0,
                "non_cacheable_compilations": 0,
                "forced_recaches": 0,
                "cache_write_errors": 0,
                "cache_writes": 0,
                "compile_fails": 0
            }
        })json";

        const SccacheStatsParser parser;
        const auto result = parser.parse_content(content);

        EXPECT_TRUE(result.is_err());
    }

    TEST(SccacheStatsParserTest, RejectsHumanReadableOutput) {
        const SccacheStatsParser parser;
        const auto result = parser.parse_content("Cache hits: 10\nCache misses: 2\n");

        EXPECT_TRUE(result.is_err());
    }

}  // namespace bha::parsers
