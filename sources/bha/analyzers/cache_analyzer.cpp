// Created by gregorian-rayne on 8/22/26.

#include "bha/analyzers/cache_analyzer.hpp"

#include <limits>
#include <utility>

namespace bha::analyzers {

    Result<AnalysisResult, Error> CacheAnalyzer::analyze(
        const BuildTrace& trace,
        [[maybe_unused]] const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        if (!trace.cache_statistics.has_value()) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        const auto& statistics = *trace.cache_statistics;
        auto& analysis = result.cache_distribution;
        analysis.compile_requests = statistics.compile_requests;
        analysis.executed_compilations = statistics.executed_requests;
        analysis.non_compilation_requests = statistics.non_compilation_requests;
        analysis.unsupported_compiler_requests = statistics.unsupported_compiler_requests;
        analysis.non_cacheable_requests = statistics.non_cacheable_requests;
        analysis.compilations = statistics.compilations;
        analysis.cache_hits = statistics.cache_hits;
        analysis.cache_misses = statistics.cache_misses;
        analysis.cache_errors = statistics.cache_errors;
        analysis.cache_timeouts = statistics.cache_timeouts;
        analysis.cache_read_errors = statistics.cache_read_errors;
        analysis.non_cacheable_compilations = statistics.non_cacheable_compilations;
        analysis.forced_recaches = statistics.forced_recaches;
        analysis.cache_write_errors = statistics.cache_write_errors;
        analysis.cache_writes = statistics.cache_writes;
        analysis.compilation_failures = statistics.compilation_failures;
        analysis.metric_capabilities = statistics.metric_capabilities;

        const bool outcome_count_overflows =
            statistics.cache_hits > std::numeric_limits<std::uint64_t>::max() - statistics.cache_misses;
        if (!outcome_count_overflows && statistics.cache_hits + statistics.cache_misses > 0) {
            analysis.hit_rate_percent = 100.0 * static_cast<double>(statistics.cache_hits) /
                static_cast<double>(statistics.cache_hits + statistics.cache_misses);
            MetricCapability hit_rate;
            hit_rate.metric = "cache.hit_rate";
            hit_rate.provenance.evidence = EvidenceKind::Derived;
            hit_rate.provenance.producer = "CacheAnalyzer";
            hit_rate.provenance.capture_mode = "sccache-json-counters";
            hit_rate.provenance.scope = "cache-server";
            hit_rate.provenance.limitation = "Derived from observed cache hit and miss counters";
            analysis.metric_capabilities.push_back(std::move(hit_rate));
        } else {
            MetricCapability hit_rate;
            hit_rate.metric = "cache.hit_rate";
            hit_rate.provenance.evidence = EvidenceKind::Unavailable;
            hit_rate.provenance.producer = "CacheAnalyzer";
            hit_rate.provenance.capture_mode = "sccache-json-counters";
            hit_rate.provenance.scope = "cache-server";
            hit_rate.provenance.limitation = outcome_count_overflows
                ? "Hit and miss counters overflow their reportable sum"
                : "Producer reported no cache hit or miss outcomes";
            analysis.metric_capabilities.push_back(std::move(hit_rate));
        }

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_cache_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<CacheAnalyzer>()
        );
    }

}  // namespace bha::analyzers
