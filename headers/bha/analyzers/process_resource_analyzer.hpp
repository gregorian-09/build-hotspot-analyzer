// Created by gregorian-rayne on 8/23/26.

#ifndef BHA_PROCESS_RESOURCE_ANALYZER_HPP
#define BHA_PROCESS_RESOURCE_ANALYZER_HPP

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Aggregates exact process counters from a compiler resource report.
     */
    class ProcessResourceAnalyzer : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "ProcessResourceAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Aggregates producer-reported compiler process resources";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_process_resource_analyzer();

}  // namespace bha::analyzers

#endif // BHA_PROCESS_RESOURCE_ANALYZER_HPP
