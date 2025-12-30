//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_DEPENDENCY_ANALYZER_HPP
#define BHA_DEPENDENCY_ANALYZER_HPP

/**
 * @file dependency_analyzer.hpp
 * @brief Include dependency analysis.
 *
 * Analyzes header inclusion patterns to identify:
 * - Most expensive headers
 * - Headers included many times
 * - Circular dependencies
 * - Deep include chains
 */

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Analyzes include dependencies across compilation units.
     */
    class DependencyAnalyzer : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "DependencyAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes header dependencies and identifies expensive includes";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_dependency_analyzer();

}  // namespace bha::analyzers

#endif //BHA_DEPENDENCY_ANALYZER_HPP