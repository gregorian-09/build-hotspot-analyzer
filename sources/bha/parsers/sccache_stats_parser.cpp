// Created by gregorian-rayne on 8/22/26.

#include "bha/parsers/sccache_stats_parser.hpp"

#include "bha/utils/file_utils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace bha::parsers {
    namespace {

        using json = nlohmann::json;

        Result<std::uint64_t, Error> required_count(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || !object[name].is_number_unsigned()) {
                return Result<std::uint64_t, Error>::failure(
                    Error::parse_error(
                        std::string("sccache statistics is missing unsigned counter: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<std::uint64_t, Error>::success(object[name].get<std::uint64_t>());
        }

        Result<std::uint64_t, Error> sum_language_counts(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || !object[name].is_object() ||
                !object[name].contains("counts") || !object[name]["counts"].is_object()) {
                return Result<std::uint64_t, Error>::failure(
                    Error::parse_error(
                        std::string("sccache statistics is missing language counter map: ") + name,
                        source_hint.string()
                    )
                );
            }

            std::uint64_t total = 0;
            for (const auto& [language, count] : object[name]["counts"].items()) {
                (void)language;
                if (!count.is_number_unsigned()) {
                    return Result<std::uint64_t, Error>::failure(
                        Error::parse_error(
                            std::string("sccache statistics has a non-unsigned language counter: ") + name,
                            source_hint.string()
                        )
                    );
                }
                const auto value = count.get<std::uint64_t>();
                if (value > std::numeric_limits<std::uint64_t>::max() - total) {
                    return Result<std::uint64_t, Error>::failure(
                        Error::parse_error(
                            std::string("sccache statistics counter overflows: ") + name,
                            source_hint.string()
                        )
                    );
                }
                total += value;
            }
            return Result<std::uint64_t, Error>::success(total);
        }

    }  // namespace

    Result<CacheStatistics, Error> SccacheStatsParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        try {
            const auto root = json::parse(content);
            if (!root.is_object() || !root.contains("stats") || !root["stats"].is_object()) {
                return Result<CacheStatistics, Error>::failure(
                    Error::parse_error("Not a sccache JSON statistics document", source_hint.string())
                );
            }
            const auto& stats = root["stats"];
            CacheStatistics result;
            result.producer = "sccache";
            if (!root.contains("version") || !root["version"].is_string()) {
                return Result<CacheStatistics, Error>::failure(
                    Error::parse_error("sccache statistics is missing its version", source_hint.string())
                );
            }
            result.producer_version = root["version"].get<std::string>();

            const auto compile_requests = required_count(stats, "compile_requests", source_hint);
            const auto executed_requests = required_count(stats, "requests_executed", source_hint);
            const auto non_compilation_requests = required_count(stats, "requests_not_compile", source_hint);
            const auto unsupported_requests = required_count(
                stats,
                "requests_unsupported_compiler",
                source_hint
            );
            const auto non_cacheable_requests = required_count(
                stats,
                "requests_not_cacheable",
                source_hint
            );
            const auto compilations = required_count(stats, "compilations", source_hint);
            const auto cache_hits = sum_language_counts(stats, "cache_hits", source_hint);
            const auto cache_misses = sum_language_counts(stats, "cache_misses", source_hint);
            const auto cache_errors = sum_language_counts(stats, "cache_errors", source_hint);
            const auto cache_timeouts = required_count(stats, "cache_timeouts", source_hint);
            const auto cache_read_errors = required_count(stats, "cache_read_errors", source_hint);
            const auto non_cacheable_compilations = required_count(
                stats,
                "non_cacheable_compilations",
                source_hint
            );
            const auto forced_recaches = required_count(stats, "forced_recaches", source_hint);
            const auto cache_write_errors = required_count(stats, "cache_write_errors", source_hint);
            const auto cache_writes = required_count(stats, "cache_writes", source_hint);
            const auto compilation_failures = required_count(stats, "compile_fails", source_hint);

            const auto* first_error = [&]() -> const Error* {
                const Result<std::uint64_t, Error>* results[] = {
                    &compile_requests,
                    &executed_requests,
                    &non_compilation_requests,
                    &unsupported_requests,
                    &non_cacheable_requests,
                    &compilations,
                    &cache_hits,
                    &cache_misses,
                    &cache_errors,
                    &cache_timeouts,
                    &cache_read_errors,
                    &non_cacheable_compilations,
                    &forced_recaches,
                    &cache_write_errors,
                    &cache_writes,
                    &compilation_failures
                };
                for (const auto* parse_result : results) {
                    if (parse_result->is_err()) return &parse_result->error();
                }
                return nullptr;
            }();
            if (first_error != nullptr) {
                return Result<CacheStatistics, Error>::failure(*first_error);
            }

            result.compile_requests = compile_requests.value();
            result.executed_requests = executed_requests.value();
            result.non_compilation_requests = non_compilation_requests.value();
            result.unsupported_compiler_requests = unsupported_requests.value();
            result.non_cacheable_requests = non_cacheable_requests.value();
            result.compilations = compilations.value();
            result.cache_hits = cache_hits.value();
            result.cache_misses = cache_misses.value();
            result.cache_errors = cache_errors.value();
            result.cache_timeouts = cache_timeouts.value();
            result.cache_read_errors = cache_read_errors.value();
            result.non_cacheable_compilations = non_cacheable_compilations.value();
            result.forced_recaches = forced_recaches.value();
            result.cache_write_errors = cache_write_errors.value();
            result.cache_writes = cache_writes.value();
            result.compilation_failures = compilation_failures.value();

            MetricCapability capability;
            capability.metric = "cache.outcomes";
            capability.provenance.evidence = EvidenceKind::Observed;
            capability.provenance.producer = result.producer;
            capability.provenance.producer_version = result.producer_version;
            capability.provenance.capture_mode = "--show-stats --stats-format=json";
            capability.provenance.scope = "cache-server";
            result.metric_capabilities.push_back(std::move(capability));
            return Result<CacheStatistics, Error>::success(std::move(result));
        } catch (const json::exception& exception) {
            return Result<CacheStatistics, Error>::failure(
                Error::parse_error(
                    std::string("Failed to parse sccache JSON statistics: ") + exception.what(),
                    source_hint.string()
                )
            );
        }
    }

    Result<CacheStatistics, Error> SccacheStatsParser::parse_file(const fs::path& path) const {
        const auto content = utils::read_file(path);
        if (content.is_err()) {
            return Result<CacheStatistics, Error>::failure(content.error());
        }
        return parse_content(content.value(), path);
    }

    Result<void, Error> SccacheStatsParser::attach_to_trace(
        BuildTrace& trace,
        const fs::path& path
    ) const {
        auto result = parse_file(path);
        if (result.is_err()) {
            return Result<void, Error>::failure(result.error());
        }

        trace.cache_statistics = std::move(result.value());
        for (const auto& capability : trace.cache_statistics->metric_capabilities) {
            const auto existing = std::ranges::find(
                trace.metric_capabilities,
                capability.metric,
                &MetricCapability::metric
            );
            if (existing == trace.metric_capabilities.end()) {
                trace.metric_capabilities.push_back(capability);
            }
        }
        return Result<void, Error>::success();
    }

}  // namespace bha::parsers
