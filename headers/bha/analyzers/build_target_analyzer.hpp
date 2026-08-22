// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_BUILD_TARGET_ANALYZER_HPP
#define BHA_BUILD_TARGET_ANALYZER_HPP

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Joins exact CMake instrumentation target names to File API target
     * objects and derives target-scoped command metrics.
     */
    class BuildTargetAnalyzer final : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "BuildTargetAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes exact target ownership and target-scoped build metrics";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_build_target_analyzer();

}  // namespace bha::analyzers

#endif // BHA_BUILD_TARGET_ANALYZER_HPP
