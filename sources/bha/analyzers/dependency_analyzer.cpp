//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/dependency_analyzer.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <algorithm>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace bha::analyzers
{
    namespace {

        struct HeaderStats {
            fs::path path;
            Duration total_parse_time = Duration::zero();
            Duration self_parse_time = Duration::zero();
            bool self_time_available = true;
            std::size_t inclusion_count = 0;
            std::unordered_set<std::string> including_files;
        };

        std::string path_key(const fs::path& p) {
            return p.lexically_normal().string();
        }

    }  // namespace

    Result<AnalysisResult, Error> DependencyAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        const auto start_time = std::chrono::steady_clock::now();

        std::unordered_map<std::string, HeaderStats> header_map;
        std::size_t max_depth = 0;
        std::size_t total_includes = 0;
        Duration total_include_time = Duration::zero();

        for (const auto& unit : trace.units) {
            const std::string source_key = path_key(unit.source_file);

            for (const auto& include : unit.includes) {
                if (include.header.empty()) {
                    continue;
                }
                if (include.parse_time < Duration::zero() ||
                    (include.self_parse_time.has_value() &&
                        *include.self_parse_time < Duration::zero())) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Header parse timing cannot be negative")
                    );
                }
                const std::string header_key = path_key(include.header);

                auto& stats = header_map[header_key];
                if (stats.path.empty()) {
                    stats.path = include.header;
                }

                if (const auto sum = utils::checked_add_duration(
                        stats.total_parse_time,
                        include.parse_time
                    ); sum.has_value()) {
                    stats.total_parse_time = *sum;
                } else {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error(
                            "Header parse timing exceeded the supported aggregate duration range"
                        )
                    );
                }
                const auto inclusion_count = utils::checked_add(
                    stats.inclusion_count,
                    std::size_t{1}
                );
                if (!inclusion_count.has_value()) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Header inclusion count overflowed")
                    );
                }
                stats.inclusion_count = *inclusion_count;
                if (!source_key.empty()) {
                    stats.including_files.insert(source_key);
                }
                if (include.self_parse_time.has_value()) {
                    if (stats.self_time_available) {
                        if (const auto sum = utils::checked_add_duration(
                                stats.self_parse_time,
                                *include.self_parse_time
                            ); sum.has_value()) {
                            stats.self_parse_time = *sum;
                        } else {
                            return Result<AnalysisResult, Error>::failure(
                                Error::analysis_error(
                                    "Header self-parse timing exceeded the supported aggregate duration range"
                                )
                            );
                        }
                    }
                } else {
                    stats.self_time_available = false;
                }

                if (const auto sum = utils::checked_add_duration(
                        total_include_time,
                        include.parse_time
                    ); sum.has_value()) {
                    total_include_time = *sum;
                } else {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error(
                            "Total include timing exceeded the supported aggregate duration range"
                        )
                    );
                }
                const auto include_count = utils::checked_add(
                    total_includes,
                    std::size_t{1}
                );
                if (!include_count.has_value()) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Total include count overflowed")
                    );
                }
                total_includes = *include_count;
                max_depth = std::max(max_depth, include.depth);
            }
        }

        result.dependencies.headers.reserve(header_map.size());

        bool has_self_time = false;
        bool all_self_time_available = true;
        for (auto& stats : header_map | std::views::values) {
            DependencyAnalysisResult::HeaderInfo info;
            info.path = stats.path;
            info.total_parse_time = stats.total_parse_time;
            info.inclusion_count = stats.inclusion_count;
            info.including_files = stats.including_files.size();
            if (stats.self_time_available) {
                info.self_parse_time = stats.self_parse_time;
                has_self_time = true;
            } else {
                all_self_time_available = false;
            }

            info.included_by.reserve(stats.including_files.size());
            for (const auto& file : stats.including_files) {
                info.included_by.emplace_back(file);
            }
            std::ranges::sort(info.included_by, [](const auto& a, const auto& b) {
                return a.generic_string() < b.generic_string();
            });

            result.dependencies.headers.push_back(std::move(info));
        }

        std::ranges::sort(result.dependencies.headers,
                          [](const auto& a, const auto& b) {
                              if (a.total_parse_time != b.total_parse_time) {
                                  return a.total_parse_time > b.total_parse_time;
                              }
                              return a.path.generic_string() < b.path.generic_string();
                          });

        result.dependencies.total_includes = total_includes;
        result.dependencies.unique_headers = header_map.size();
        result.dependencies.max_include_depth = max_depth;
        result.dependencies.total_include_time = total_include_time;

        if (!result.dependencies.headers.empty()) {
            MetricCapability capability;
            capability.metric = "frontend.header.consumer_fanout";
            capability.provenance.evidence = EvidenceKind::Derived;
            capability.provenance.producer = "DependencyAnalyzer";
            capability.provenance.capture_mode = "-ftime-trace Source events";
            capability.provenance.scope = "build";
            capability.provenance.timing_domain = TimingDomain::None;
            capability.provenance.timing_aggregation = TimingAggregation::None;
            capability.provenance.limitation =
                "Fanout counts translation units with an observed Source event; direct include edges and untraced consumers are unavailable";
            result.dependencies.metric_capabilities.push_back(std::move(capability));
        }

        if (has_self_time) {
            MetricCapability capability;
            capability.metric = "frontend.source_self_time";
            capability.provenance.evidence = EvidenceKind::Derived;
            capability.provenance.producer = "clang";
            capability.provenance.capture_mode = "-ftime-trace";
            capability.provenance.scope = "build";
            capability.provenance.timing_domain = TimingDomain::WallClock;
            capability.provenance.timing_aggregation = TimingAggregation::Exclusive;
            if (!all_self_time_available) {
                capability.provenance.limitation =
                    "Headers with incomplete Source interval identity retain unavailable self-time";
            }
            result.dependencies.metric_capabilities.push_back(std::move(capability));
        }

        const auto end_time = std::chrono::steady_clock::now();
        result.analysis_time = std::chrono::system_clock::now();
        result.analysis_duration = std::chrono::duration_cast<Duration>(end_time - start_time);

        (void)options;

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_dependency_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<DependencyAnalyzer>()
        );
    }
}  // namespace bha::analyzers
