//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_SYMBOL_ANALYZER_HPP
#define BHA_SYMBOL_ANALYZER_HPP

/**
 * @file symbol_analyzer.hpp
 * @brief Symbol definition and usage analysis.
 *
 * Analyzes producer-provided symbol definitions and exact symbol-use records.
 * It does not classify names, infer linkage, or treat header inclusion as a
 * symbol reference. Those conclusions require semantic AST or object-file
 * evidence that is not present in the normalized trace model.
 */

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Analyzes symbol definitions and usage patterns.
     *
     * Reports:
     * - Exact producer-provided symbol names and definition locations
     * - Exact symbol uses attached to include records
     * - Observed template event counts
     * - Producer-defined symbols with no observed uses
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
