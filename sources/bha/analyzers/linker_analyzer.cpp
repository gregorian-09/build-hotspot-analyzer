// Created by gregorian-rayne on 8/22/26.

#include "bha/analyzers/linker_analyzer.hpp"

#include <algorithm>
#include <limits>

namespace bha::analyzers {
    namespace {

        MetricCapability capability(
            const std::string_view metric,
            const EvidenceKind evidence,
            const std::string_view limitation = {}
        ) {
            MetricCapability result;
            result.metric = metric;
            result.provenance.evidence = evidence;
            result.provenance.producer = "LinkerAnalyzer";
            result.provenance.capture_mode = "build-session-events";
            result.provenance.scope = "link-command";
            result.provenance.timing_domain = TimingDomain::WallClock;
            result.provenance.timing_aggregation = TimingAggregation::Exclusive;
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

    }  // namespace

    Result<AnalysisResult, Error> LinkerAnalyzer::analyze(
        const BuildTrace& trace,
        [[maybe_unused]] const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        if (!trace.build_session.has_value()) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        auto& analysis = result.linker;
        bool output_size_overflow = false;
        for (const auto& event : trace.build_session->commands) {
            if (event.role != BuildStepRole::Link) {
                continue;
            }

            ++analysis.invocations;
            if (event.has_exact_timing()) {
                ++analysis.timed_invocations;
                analysis.wall_clock_time += event.duration;
            }

            if (event.outputs.empty() || event.outputs.size() != event.output_sizes.size()) {
                continue;
            }

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

            ++analysis.output_size_observations;
            if (event_output_bytes >
                std::numeric_limits<std::uintmax_t>::max() - analysis.output_bytes) {
                output_size_overflow = true;
                continue;
            }
            analysis.output_bytes += event_output_bytes;
        }

        if (analysis.invocations == 0) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        if (analysis.timed_invocations > 0) {
            const std::string limitation = analysis.timed_invocations == analysis.invocations
                ? std::string{}
                : "Some link commands lack exact producer timing";
            auto link_time = capability(
                "link.wall_time",
                EvidenceKind::Derived,
                limitation
            );
            link_time.provenance.timing_aggregation = TimingAggregation::Exclusive;
            add_capability(analysis, std::move(link_time));
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

        if (analysis.output_size_observations > 0 && !output_size_overflow) {
            const std::string limitation = analysis.output_size_observations == analysis.invocations
                ? std::string{}
                : "Some link commands lack aligned producer output-size arrays";
            add_capability(
                analysis,
                capability("link.output_bytes", EvidenceKind::Derived, limitation)
            );
        } else {
            const std::string limitation = output_size_overflow
                ? "Producer-reported output sizes overflowed the aggregate representation"
                : "No link command has aligned producer output-size arrays";
            add_capability(
                analysis,
                capability("link.output_bytes", EvidenceKind::Unavailable, limitation)
            );
        }

        add_capability(
            analysis,
            capability(
                "link.input_bytes",
                EvidenceKind::Unavailable,
                "CMake Instrumentation v1 reports link outputs, not input object or library sizes; command parsing is not used"
            )
        );
        add_capability(
            analysis,
            capability(
                "lto.wall_time",
                EvidenceKind::Unavailable,
                "No linker time-trace evidence is attached to this build session"
            )
        );

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_linker_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<LinkerAnalyzer>()
        );
    }

}  // namespace bha::analyzers
