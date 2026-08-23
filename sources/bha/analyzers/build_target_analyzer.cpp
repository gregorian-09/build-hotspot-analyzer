// Created by gregorian-rayne on 8/22/26.

#include "bha/analyzers/build_target_analyzer.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
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

        bool add_output_sizes(
            BuildTargetAnalysisResult::TargetInfo& target,
            const BuildCommandEvent& event
        ) {
            if (!has_exact_output_sizes(event)) {
                return false;
            }

            std::uintmax_t bytes = 0;
            for (const auto size : event.output_sizes) {
                if (size > std::numeric_limits<std::uintmax_t>::max() - bytes) {
                    return false;
                }
                bytes += size;
            }
            if (bytes > std::numeric_limits<std::uintmax_t>::max() - target.output_bytes) {
                return false;
            }
            ++target.output_size_observations;
            target.output_bytes += bytes;
            return true;
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
        for (const auto& target : trace.target_graph->targets) {
            BuildTargetAnalysisResult::TargetInfo info;
            info.id = target.id;
            info.name = target.name;
            info.type = target.type;
            info.dependencies = target.dependencies;
            info.precompile_headers = target.precompile_headers;
            if (!info.precompile_headers.empty()) {
                ++analysis.pch_targets;
                analysis.pch_headers += info.precompile_headers.size();
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
            ++analysis.target_commands;

            const auto target_it = target_indexes.find(event.target);
            if (target_it == target_indexes.end() || target_it->second.size() != 1) {
                ++analysis.unmatched_commands;
                continue;
            }

            ++analysis.matched_commands;
            auto& target = analysis.targets[target_it->second.front()];
            if (event.role == BuildStepRole::Compile) {
                ++target.compile_commands;
                if (event.has_exact_timing()) {
                    ++target.timed_compile_commands;
                    target.compile_wall_clock_time += event.duration;
                }
            } else {
                ++target.link_commands;
                if (event.has_exact_timing()) {
                    ++target.timed_link_commands;
                    target.link_wall_clock_time += event.duration;
                }
            }
            add_output_sizes(target, event);
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
        for (const auto& target : analysis.targets) {
            compile_commands += target.compile_commands;
            timed_compile_commands += target.timed_compile_commands;
            link_commands += target.link_commands;
            timed_link_commands += target.timed_link_commands;
            output_observations += target.output_size_observations;
        }

        if (timed_compile_commands > 0) {
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

        if (timed_link_commands > 0) {
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
                output_observations > 0 ? EvidenceKind::Derived : EvidenceKind::Unavailable,
                output_observations > 0
                    ? std::string{}
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
