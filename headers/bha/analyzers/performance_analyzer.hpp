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
 * - Critical path identification
 * - Parallelization efficiency
 * - Build bottlenecks
 * - Statistical distribution of compile times
 */

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Analyzes overall build performance metrics.
     *
     * Identifies:
     * - The critical path (longest dependency chain)
     * - Parallelization efficiency
     * - Statistical distribution of compile times
     * - Slowest files contributing to build time
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