// Created by gregorian-rayne on 8/22/26.

#include "bha/analyzers/build_target_analyzer.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace bha::analyzers {
    namespace {

        MetricCapability capability(
            const std::string_view metric,
            const EvidenceKind evidence,
            const std::string_view limitation = {},
            const TimingDomain timing_domain = TimingDomain::WallClock,
            const TimingAggregation timing_aggregation = TimingAggregation::Exclusive
        ) {
            MetricCapability result;
            result.metric = metric;
            result.provenance.evidence = evidence;
            result.provenance.producer = "BuildTargetAnalyzer";
            result.provenance.capture_mode = "cmake-file-api-v1+instrumentation-v1";
            result.provenance.scope = "target";
            result.provenance.timing_domain = timing_domain;
            result.provenance.timing_aggregation = timing_aggregation;
            result.provenance.limitation = limitation;
            return result;
        }

        void add_capability(
            BuildTargetAnalysisResult& analysis,
            MetricCapability value
        ) {
            const auto existing = std::ranges::find(
                analysis.metric_capabilities,
                value.metric,
                &MetricCapability::metric
            );
            if (existing == analysis.metric_capabilities.end()) {
                analysis.metric_capabilities.push_back(std::move(value));
            }
        }

        bool has_exact_output_sizes(const BuildCommandEvent& event) {
            return !event.outputs.empty() && event.outputs.size() == event.output_sizes.size();
        }

        enum class OutputSizeResult {
            Unavailable,
            Observed,
            Overflow
        };

        bool checked_increment(std::size_t& value) {
            const auto sum = utils::checked_add(value, std::size_t{1});
            if (!sum.has_value()) {
                return false;
            }
            value = *sum;
            return true;
        }

        OutputSizeResult add_output_sizes(
            BuildTargetAnalysisResult::TargetInfo& target,
            const BuildCommandEvent& event
        ) {
            if (!has_exact_output_sizes(event)) {
                return OutputSizeResult::Unavailable;
            }

            std::uintmax_t bytes = 0;
            for (const auto size : event.output_sizes) {
                if (size > std::numeric_limits<std::uintmax_t>::max() - bytes) {
                    return OutputSizeResult::Overflow;
                }
                bytes += size;
            }
            if (bytes > std::numeric_limits<std::uintmax_t>::max() - target.output_bytes) {
                return OutputSizeResult::Overflow;
            }
            if (!checked_increment(target.output_size_observations)) {
                return OutputSizeResult::Overflow;
            }
            target.output_bytes += bytes;
            return OutputSizeResult::Observed;
        }

    }  // namespace

    Result<AnalysisResult, Error> BuildTargetAnalyzer::analyze(
        const BuildTrace& trace,
        [[maybe_unused]] const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        if (!trace.target_graph.has_value() || !trace.target_graph->complete ||
            !trace.build_session.has_value()) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        auto& analysis = result.targets;
        analysis.targets.reserve(trace.target_graph->targets.size());
        std::unordered_map<std::string, std::vector<std::size_t>> target_indexes;
        std::unordered_set<std::size_t> compile_time_overflow_targets;
        std::unordered_set<std::size_t> link_time_overflow_targets;
        std::unordered_set<std::size_t> output_size_overflow_targets;
        bool has_nonzero_output_size = false;
        for (const auto& target : trace.target_graph->targets) {
            BuildTargetAnalysisResult::TargetInfo info;
            info.id = target.id;
            info.name = target.name;
            info.type = target.type;
            info.dependencies = target.dependencies;
            info.precompile_headers = target.precompile_headers;
            if (!info.precompile_headers.empty()) {
                if (!checked_increment(analysis.pch_targets)) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Precompiled-header target count overflowed")
                    );
                }
                const auto pch_header_count = utils::checked_add(
                    analysis.pch_headers,
                    info.precompile_headers.size()
                );
                if (!pch_header_count.has_value()) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Precompiled-header count overflowed")
                    );
                }
                analysis.pch_headers = *pch_header_count;
            }
            target_indexes[info.name].push_back(analysis.targets.size());
            analysis.targets.push_back(std::move(info));
        }

        for (const auto& event : trace.build_session->commands) {
            if (event.role != BuildStepRole::Compile && event.role != BuildStepRole::Link) {
                continue;
            }
            if (event.target.empty()) {
                continue;
            }
            if (!checked_increment(analysis.target_commands)) {
                return Result<AnalysisResult, Error>::failure(
                    Error::analysis_error("Target command count overflowed")
                );
            }

            const auto target_it = target_indexes.find(event.target);
            if (target_it == target_indexes.end() || target_it->second.size() != 1) {
                if (!checked_increment(analysis.unmatched_commands)) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Unmatched target command count overflowed")
                    );
                }
                continue;
            }

            if (!checked_increment(analysis.matched_commands)) {
                return Result<AnalysisResult, Error>::failure(
                    Error::analysis_error("Matched target command count overflowed")
                );
            }
            has_nonzero_output_size = has_nonzero_output_size || (
                has_exact_output_sizes(event) &&
                std::ranges::any_of(
                    event.output_sizes,
                    [](const std::uintmax_t size) { return size > 0; }
                )
            );
            const auto target_index = target_it->second.front();
            auto& target = analysis.targets[target_index];
            if (event.role == BuildStepRole::Compile) {
                if (!checked_increment(target.compile_commands)) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Target compile command count overflowed")
                    );
                }
                if (event.has_exact_timing()) {
                    if (!checked_increment(target.timed_compile_commands)) {
                        return Result<AnalysisResult, Error>::failure(
                            Error::analysis_error("Timed target compile command count overflowed")
                        );
                    }
                    if (!compile_time_overflow_targets.contains(target_index)) {
                        const auto sum = utils::checked_add_duration(
                            target.compile_wall_clock_time,
                            event.duration
                        );
                        if (sum.has_value()) {
                            target.compile_wall_clock_time = *sum;
                        } else {
                            compile_time_overflow_targets.insert(target_index);
                            target.compile_wall_clock_time = Duration::zero();
                        }
                    }
                }
            } else {
                if (!checked_increment(target.link_commands)) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Target link command count overflowed")
                    );
                }
                if (event.has_exact_timing()) {
                    if (!checked_increment(target.timed_link_commands)) {
                        return Result<AnalysisResult, Error>::failure(
                            Error::analysis_error("Timed target link command count overflowed")
                        );
                    }
                    if (!link_time_overflow_targets.contains(target_index)) {
                        const auto sum = utils::checked_add_duration(
                            target.link_wall_clock_time,
                            event.duration
                        );
                        if (sum.has_value()) {
                            target.link_wall_clock_time = *sum;
                        } else {
                            link_time_overflow_targets.insert(target_index);
                            target.link_wall_clock_time = Duration::zero();
                        }
                    }
                }
            }
            if (!output_size_overflow_targets.contains(target_index)) {
                if (add_output_sizes(target, event) == OutputSizeResult::Overflow) {
                    output_size_overflow_targets.insert(target_index);
                    target.output_bytes = 0;
                    target.output_size_observations = 0;
                }
            }
        }

        const std::string ownership_limitation = analysis.unmatched_commands == 0
            ? std::string{}
            : "Some producer target names did not match exactly one File API target; no fallback inference was used";
        add_capability(
            analysis,
            capability(
                "build.target.command_ownership",
                analysis.matched_commands > 0
                    ? EvidenceKind::Derived
                    : EvidenceKind::Unavailable,
                analysis.target_commands == 0
                    ? "No compile or link event included the producer target field"
                    : ownership_limitation
            )
        );

        add_capability(
            analysis,
            capability(
                "build.target.pch_declarations",
                analysis.pch_headers > 0
                    ? EvidenceKind::Derived
                    : EvidenceKind::Unavailable,
                analysis.pch_headers > 0
                    ? std::string{}
                    : "The selected CMake codemodel declares no precompiled headers"
            )
        );

        std::size_t compile_commands = 0;
        std::size_t timed_compile_commands = 0;
        std::size_t link_commands = 0;
        std::size_t timed_link_commands = 0;
        std::size_t output_observations = 0;
        const auto add_count = [](std::size_t& total, const std::size_t value) {
            const auto sum = utils::checked_add(total, value);
            if (!sum.has_value()) {
                return false;
            }
            total = *sum;
            return true;
        };
        for (const auto& target : analysis.targets) {
            if (!add_count(compile_commands, target.compile_commands) ||
                !add_count(timed_compile_commands, target.timed_compile_commands) ||
                !add_count(link_commands, target.link_commands) ||
                !add_count(timed_link_commands, target.timed_link_commands) ||
                !add_count(output_observations, target.output_size_observations)) {
                return Result<AnalysisResult, Error>::failure(
                    Error::analysis_error("Target metric count aggregation overflowed")
                );
            }
        }

        if (!compile_time_overflow_targets.empty()) {
            add_capability(
                analysis,
                capability(
                    "build.target.compile_wall_time",
                    EvidenceKind::Unavailable,
                    "At least one target's exact compile durations overflowed the aggregate representation"
                )
            );
        } else if (timed_compile_commands > 0) {
            add_capability(
                analysis,
                capability(
                    "build.target.compile_wall_time",
                    EvidenceKind::Derived,
                    timed_compile_commands == compile_commands
                        ? std::string{}
                        : "Some matched compile events lack exact producer timing"
                )
            );
        } else {
            add_capability(
                analysis,
                capability(
                    "build.target.compile_wall_time",
                    EvidenceKind::Unavailable,
                    "No matched compile event has exact producer timing"
                )
            );
        }

        if (!link_time_overflow_targets.empty()) {
            add_capability(
                analysis,
                capability(
                    "build.target.link_wall_time",
                    EvidenceKind::Unavailable,
                    "At least one target's exact link durations overflowed the aggregate representation"
                )
            );
        } else if (timed_link_commands > 0) {
            add_capability(
                analysis,
                capability(
                    "build.target.link_wall_time",
                    EvidenceKind::Derived,
                    timed_link_commands == link_commands
                        ? std::string{}
                        : "Some matched link events lack exact producer timing"
                )
            );
        } else {
            add_capability(
                analysis,
                capability(
                    "build.target.link_wall_time",
                    EvidenceKind::Unavailable,
                    "No matched link event has exact producer timing"
                )
            );
        }

        add_capability(
            analysis,
            capability(
                "build.target.output_bytes",
                output_size_overflow_targets.empty() && output_observations > 0 &&
                    has_nonzero_output_size
                ? EvidenceKind::Derived
                : EvidenceKind::Unavailable,
                !output_size_overflow_targets.empty()
                    ? "Producer-reported output sizes overflowed the aggregate representation"
                    : output_observations > 0 && !has_nonzero_output_size
                        ? "The producer reported zero for every target output; output bytes are unavailable"
                    : "No matched compile or link event has aligned producer output-size arrays",
                TimingDomain::None,
                TimingAggregation::None
            )
        );

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_build_target_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<BuildTargetAnalyzer>()
        );
    }

}  // namespace bha::analyzers
