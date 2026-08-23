//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_PERFORMANCE_ANALYZER_HPP
#define BHA_PERFORMANCE_ANALYZER_HPP

/**
 * @file performance_analyzer.hpp
 * @brief Overall build performance analysis.
 *
 * Analyzes build performance metrics including:
 * - Producer-backed parallelization efficiency
 * - Statistical distribution of compile times
 */

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Analyzes overall build performance metrics.
     *
     * Reports:
     * - Parallelization efficiency when the trace has an exact build duration
     * - Statistical distribution of compile times
     * - Slowest files contributing to build time
     *
     * Critical-path analysis belongs to BuildSessionAnalyzer because only the
     * producer build session can supply command dependencies and timestamps.
     */
    class PerformanceAnalyzer : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "PerformanceAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes overall build performance and identifies bottlenecks";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_performance_analyzer();

}  // namespace bha::analyzers

#endif //BHA_PERFORMANCE_ANALYZER_HPP
