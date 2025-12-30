//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_SYMBOL_ANALYZER_HPP
#define BHA_SYMBOL_ANALYZER_HPP

/**
 * @file symbol_analyzer.hpp
 * @brief Symbol definition and usage analysis.
 *
 * Analyzes symbol definitions across the codebase, tracking where
 * symbols are defined and used to identify:
 * - Unused symbols (defined but never referenced elsewhere)
 * - Frequently used symbols that should be optimized
 * - Symbol visibility issues
 */

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Analyzes symbol definitions and usage patterns.
     *
     * Identifies:
     * - Symbol definitions and their locations
     * - Symbol usage patterns across files
     * - Potentially unused symbols
     * - Inline function candidates
     */
    class SymbolAnalyzer : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "SymbolAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes symbol definitions and usage patterns across the codebase";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_symbol_analyzer();

}  // namespace bha::analyzers

#endif //BHA_SYMBOL_ANALYZER_HPP