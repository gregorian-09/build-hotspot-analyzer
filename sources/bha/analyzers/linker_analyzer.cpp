// Created by gregorian-rayne on 8/22/26.

#include "bha/analyzers/linker_analyzer.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <algorithm>
#include <limits>

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
            result.provenance.producer = "LinkerAnalyzer";
            result.provenance.capture_mode = "build-session-events";
            result.provenance.scope = "link-command";
            result.provenance.timing_domain = timing_domain;
            result.provenance.timing_aggregation = timing_aggregation;
            result.provenance.limitation = limitation;
            return result;
        }

        void add_capability(
            LinkerAnalysisResult& analysis,
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

        bool has_capability(
            const LinkerAnalysisResult& analysis,
            const std::string_view metric
        ) {
            return std::ranges::find(
                analysis.metric_capabilities,
                metric,
                &MetricCapability::metric
            ) != analysis.metric_capabilities.end();
        }

        bool checked_increment(std::size_t& value) {
            const auto sum = utils::checked_add(value, std::size_t{1});
            if (!sum.has_value()) {
                return false;
            }
            value = *sum;
            return true;
        }

    }  // namespace

    Result<AnalysisResult, Error> LinkerAnalyzer::analyze(
        const BuildTrace& trace,
        [[maybe_unused]] const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        const bool has_linker_trace = trace.linker_trace.has_value();
        if (!trace.build_session.has_value() && !has_linker_trace) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        auto& analysis = result.linker;
        bool invalid_trace_wall_time = false;
        bool invalid_lto_time = false;
        if (has_linker_trace) {
            if (trace.linker_trace->execute_linker_time.has_value()) {
                if (*trace.linker_trace->execute_linker_time < Duration::zero()) {
                    invalid_trace_wall_time = true;
                } else {
                    analysis.trace_wall_clock_time = trace.linker_trace->execute_linker_time;
                }
            }
            if (trace.linker_trace->lto_time.has_value()) {
                if (*trace.linker_trace->lto_time < Duration::zero()) {
                    invalid_lto_time = true;
                } else {
                    analysis.lto_time = trace.linker_trace->lto_time;
                }
            }
            for (const auto& metric : trace.linker_trace->metric_capabilities) {
                if ((invalid_trace_wall_time && metric.metric == "linker.trace.wall_time") ||
                    (invalid_lto_time && metric.metric == "lto.wall_time")) {
                    continue;
                }
                add_capability(analysis, metric);
            }
        }

        bool output_size_overflow = false;
        bool has_output_size_observation = false;
        bool has_nonzero_output_size = false;
        bool wall_time_overflow = false;
        if (trace.build_session.has_value()) {
            for (const auto& event : trace.build_session->commands) {
                if (event.role != BuildStepRole::Link) {
                    continue;
                }

                if (!checked_increment(analysis.invocations)) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Link invocation count overflowed")
                    );
                }
                if (event.has_exact_timing()) {
                    if (!checked_increment(analysis.timed_invocations)) {
                        return Result<AnalysisResult, Error>::failure(
                            Error::analysis_error("Timed link invocation count overflowed")
                        );
                    }
                    if (!wall_time_overflow) {
                        const auto sum = utils::checked_add_duration(
                            analysis.wall_clock_time,
                            event.duration
                        );
                        if (sum.has_value()) {
                            analysis.wall_clock_time = *sum;
                        } else {
                            wall_time_overflow = true;
                            analysis.wall_clock_time = Duration::zero();
                        }
                    }
                }

                if (event.outputs.empty() || event.outputs.size() != event.output_sizes.size()) {
                    continue;
                }
                has_output_size_observation = true;
                has_nonzero_output_size = has_nonzero_output_size || std::ranges::any_of(
                    event.output_sizes,
                    [](const std::uintmax_t size) { return size > 0; }
                );

                std::uintmax_t event_output_bytes = 0;
                for (const auto size : event.output_sizes) {
                    if (size > std::numeric_limits<std::uintmax_t>::max() - event_output_bytes) {
                        output_size_overflow = true;
                        break;
                    }
                    event_output_bytes += size;
                }
                if (output_size_overflow) {
                    continue;
                }

                if (!checked_increment(analysis.output_size_observations)) {
                    output_size_overflow = true;
                    continue;
                }
                if (event_output_bytes >
                    std::numeric_limits<std::uintmax_t>::max() - analysis.output_bytes) {
                    output_size_overflow = true;
                    continue;
                }
                analysis.output_bytes += event_output_bytes;
            }
        }

        if (output_size_overflow) {
            analysis.output_size_observations = 0;
            analysis.output_bytes = 0;
        }

        if (analysis.invocations == 0 && has_linker_trace) {
            analysis.invocations = 1;
        }
        if (analysis.invocations == 0) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        if (wall_time_overflow) {
            add_capability(
                analysis,
                capability(
                    "link.wall_time",
                    EvidenceKind::Unavailable,
                    "Exact link command durations overflowed the aggregate representation"
                )
            );
        } else if (analysis.timed_invocations > 0) {
            const std::string limitation = analysis.timed_invocations == analysis.invocations
                ? std::string{}
                : "Some link commands lack exact producer timing";
            add_capability(
                analysis,
                capability("link.wall_time", EvidenceKind::Derived, limitation)
            );
        } else {
            add_capability(
                analysis,
                capability(
                    "link.wall_time",
                    EvidenceKind::Unavailable,
                    "No link command has exact producer timing"
                )
            );
        }

        if (analysis.output_size_observations > 0 && !output_size_overflow && has_nonzero_output_size) {
            const std::string limitation = analysis.output_size_observations == analysis.invocations
                ? std::string{}
                : "Some link commands lack aligned producer output-size arrays";
            add_capability(
                analysis,
                capability(
                    "link.output_bytes",
                    EvidenceKind::Derived,
                    limitation,
                    TimingDomain::None,
                    TimingAggregation::None
                )
            );
        } else {
            const std::string limitation = output_size_overflow
                ? "Producer-reported output sizes overflowed the aggregate representation"
                : has_output_size_observation && !has_nonzero_output_size
                    ? "The producer reported zero for every linked output; output bytes are unavailable"
                : "No link command has aligned producer output-size arrays";
            add_capability(
                analysis,
                capability(
                    "link.output_bytes",
                    EvidenceKind::Unavailable,
                    limitation,
                    TimingDomain::None,
                    TimingAggregation::None
                )
            );
        }

        add_capability(
            analysis,
            capability(
                "link.input_bytes",
                EvidenceKind::Unavailable,
                "CMake Instrumentation v1 reports link outputs, not input object or library sizes; command parsing is not used",
                TimingDomain::None,
                TimingAggregation::None
            )
        );

        if (!analysis.lto_time.has_value()) {
            add_capability(
                analysis,
                capability(
                    "lto.wall_time",
                    EvidenceKind::Unavailable,
                    invalid_lto_time
                        ? "Attached linker time-trace evidence contained a negative LTO duration"
                        : "No linker time-trace evidence is attached to this build session"
                )
            );
        }

        if (has_linker_trace && analysis.trace_wall_clock_time.has_value() &&
            !has_capability(analysis, "linker.trace.wall_time")) {
            auto trace_capability = capability(
                "linker.trace.wall_time",
                EvidenceKind::Observed,
                {},
                TimingDomain::WallClock,
                TimingAggregation::Inclusive
            );
            trace_capability.provenance.producer = "lld";
            trace_capability.provenance.capture_mode = "--time-trace";
            trace_capability.provenance.scope = "ExecuteLinker";
            add_capability(analysis, std::move(trace_capability));
        } else if (has_linker_trace && invalid_trace_wall_time &&
                   !has_capability(analysis, "linker.trace.wall_time")) {
            add_capability(
                analysis,
                capability(
                    "linker.trace.wall_time",
                    EvidenceKind::Unavailable,
                    "Attached linker time-trace evidence contained a negative ExecuteLinker duration",
                    TimingDomain::WallClock,
                    TimingAggregation::Inclusive
                )
            );
        }

        if (has_linker_trace && analysis.lto_time.has_value() &&
            !has_capability(analysis, "lto.wall_time")) {
            auto lto_capability = capability(
                "lto.wall_time",
                EvidenceKind::Observed,
                {},
                TimingDomain::WallClock,
                TimingAggregation::Inclusive
            );
            lto_capability.provenance.producer = "lld";
            lto_capability.provenance.capture_mode = "--time-trace";
            lto_capability.provenance.scope = "Total LTO";
            add_capability(analysis, std::move(lto_capability));
        }

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_linker_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<LinkerAnalyzer>()
        );
    }

}  // namespace bha::analyzers
