// Created by gregorian-rayne on 8/23/26.

#include "bha/analyzers/process_resource_analyzer.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <utility>

namespace bha::analyzers {

    Result<AnalysisResult, Error> ProcessResourceAnalyzer::analyze(
        const BuildTrace& trace,
        [[maybe_unused]] const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        if (!trace.process_resource_report.has_value()) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        const auto& report = *trace.process_resource_report;
        auto& analysis = result.process_resources;
        analysis.observations = report.observations.size();
        analysis.metric_capabilities = report.metric_capabilities;

        bool process_time_overflow = false;
        bool user_time_overflow = false;

        for (const auto& observation : report.observations) {
            if (!process_time_overflow) {
                const auto sum = utils::checked_add_duration(
                    analysis.total_process_time,
                    observation.total_time
                );
                if (sum.has_value()) {
                    analysis.total_process_time = *sum;
                } else {
                    process_time_overflow = true;
                    analysis.total_process_time = Duration::zero();
                }
            }
            if (!user_time_overflow) {
                const auto sum = utils::checked_add_duration(
                    analysis.total_user_time,
                    observation.user_time
                );
                if (sum.has_value()) {
                    analysis.total_user_time = *sum;
                } else {
                    user_time_overflow = true;
                    analysis.total_user_time = Duration::zero();
                }
            }
            if (observation.peak_memory_kib > analysis.peak_memory_kib) {
                analysis.peak_memory_kib = observation.peak_memory_kib;
            }
        }

        if (process_time_overflow || user_time_overflow) {
            for (auto& capability : analysis.metric_capabilities) {
                if (capability.metric == "process.resource_counters") {
                    capability.provenance.evidence = EvidenceKind::Unavailable;
                    capability.provenance.limitation =
                        "Producer resource rows were observed, but an aggregate duration exceeded the representation";
                }
            }
        }

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_process_resource_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<ProcessResourceAnalyzer>()
        );
    }

}  // namespace bha::analyzers
