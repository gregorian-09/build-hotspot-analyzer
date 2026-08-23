// Created by gregorian-rayne on 8/23/26.

#include "bha/analyzers/process_resource_analyzer.hpp"

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

        for (const auto& observation : report.observations) {
            analysis.total_process_time += observation.total_time;
            analysis.total_user_time += observation.user_time;
            if (observation.peak_memory_kib > analysis.peak_memory_kib) {
                analysis.peak_memory_kib = observation.peak_memory_kib;
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
