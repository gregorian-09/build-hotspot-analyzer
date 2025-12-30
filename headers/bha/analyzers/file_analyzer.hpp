//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_FILE_ANALYZER_HPP
#define BHA_FILE_ANALYZER_HPP

/**
 * @file file_analyzer.hpp
 * @brief Per-file compilation analysis.
 *
 * Analyzes individual file compilation times and identifies
 * the slowest files that contribute most to build time.
 */

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Analyzes per-file compilation metrics.
     *
     * Identifies:
     * - Slowest compiling files
     * - Files with high template instantiation time
     * - Files with excessive include processing
     * - Time distribution across files
     */
    class FileAnalyzer : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "FileAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes per-file compilation times and identifies slowest files";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_file_analyzer();

}  // namespace bha::analyzers

#endif //BHA_FILE_ANALYZER_HPP