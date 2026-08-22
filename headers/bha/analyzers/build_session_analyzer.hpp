// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_BUILD_SESSION_ANALYZER_HPP
#define BHA_BUILD_SESSION_ANALYZER_HPP

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Derives scheduler metrics from exact build-system command events.
     * Missing timestamps or dependency edges remain unavailable.
     */
    class BuildSessionAnalyzer : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "BuildSessionAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes exact build command timing and scheduler overlap";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_build_session_analyzer();

}  // namespace bha::analyzers

#endif // BHA_BUILD_SESSION_ANALYZER_HPP
