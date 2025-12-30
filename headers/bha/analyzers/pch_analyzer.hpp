//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_PCH_ANALYZER_HPP
#define BHA_PCH_ANALYZER_HPP

/**
 * @file pch_analyzer.hpp
 * @brief Precompiled header effectiveness analysis.
 *
 * Analyzes headers to identify PCH candidates based on:
 * - Inclusion frequency across compilation units
 * - Parse time impact
 * - Header stability (rarely changing headers are better PCH candidates)
 */

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Result of PCH analysis.
     */
    struct PCHAnalysisResult {
        struct PCHCandidate {
            fs::path header;
            Duration total_parse_time = Duration::zero();
            std::size_t inclusion_count = 0;
            std::size_t including_files = 0;
            double pch_score = 0.0;  // Higher is better candidate
            Duration estimated_savings = Duration::zero();
        };

        std::vector<PCHCandidate> candidates;
        Duration current_total_parse_time = Duration::zero();
        Duration potential_savings = Duration::zero();
        std::size_t total_headers_analyzed = 0;
    };

    /**
     * Analyzes headers for PCH optimization opportunities.
     *
     * Identifies:
     * - Headers frequently included across multiple files
     * - Headers with high cumulative parse time
     * - Optimal PCH candidates based on inclusion patterns
     */
    class PCHAnalyzer : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "PCHAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes headers to identify precompiled header candidates";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;

        /**
         * Performs dedicated PCH analysis.
         */
        [[nodiscard]] static Result<PCHAnalysisResult, Error> analyze_pch(
            const BuildTrace& trace,
            const AnalysisOptions& options
        );
    };

    void register_pch_analyzer();

}  // namespace bha::analyzers

#endif //BHA_PCH_ANALYZER_HPP