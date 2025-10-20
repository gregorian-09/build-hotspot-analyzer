//
// Created by gregorian on 20/10/2025.
//

#ifndef PCH_ANALYZER_H
#define PCH_ANALYZER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace bha::analysis {

    /**
     * @struct PCHCandidate
     * Represents a potential candidate for inclusion in a precompiled header (PCH).
     *
     * This structure stores analysis data used to evaluate whether a header
     * should be part of a PCH file based on inclusion frequency and compile-time impact.
     */
    struct PCHCandidate {
        /** The path or name of the header file. */
        std::string header;

        /** The number of times this header was included across the project. */
        int inclusion_count;

        /** The average compile time (in milliseconds) attributed to this header. */
        double average_compile_time_ms;

        /** Estimated total compile-time savings (in milliseconds) if included in a PCH. */
        double potential_savings_ms;

        /** A composite score representing the benefit of including this header in a PCH. */
        double benefit_score;
    };

    /**
     * @class PCHAnalyzer
     * Performs analysis related to precompiled header (PCH) optimization.
     *
     * The PCHAnalyzer evaluates header inclusion data, compile-time impact,
     * and potential savings to suggest optimal headers for precompilation.
     * It can also assess the effectiveness of existing PCH configurations.
     */
    class PCHAnalyzer {
    public:
        /** Default constructor. */
        PCHAnalyzer() = default;

        /**
         * Identifies headers that are strong candidates for precompilation.
         *
         * Analyzes inclusion frequency and compile-time contributions to determine
         * which headers offer the most benefit when included in a PCH.
         *
         * @param trace The build trace containing compilation timing data.
         * @param graph The dependency graph describing header relationships.
         * @param top_n The maximum number of candidate headers to return.
         * @param min_inclusion_ratio Minimum inclusion ratio to consider a header.
         * @return A Result containing a list of top @ref PCHCandidate entries.
         */
        static core::Result<std::vector<PCHCandidate>> identify_pch_candidates(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            int top_n = 10,
            double min_inclusion_ratio = 0.5
        );

        /**
         * Evaluates the performance effectiveness of a given PCH configuration.
         *
         * Compares build data to measure compile-time improvements and missed optimization opportunities.
         *
         * @param trace The build trace used for timing analysis.
         * @param pch_file The path to the current PCH file.
         * @return A Result containing computed @ref core::PCHMetrics values.
         */
        static core::Result<core::PCHMetrics> analyze_pch_effectiveness(
            const core::BuildTrace& trace,
            const std::string& pch_file
        );

        /**
         * Suggests headers to add to the existing PCH to improve performance.
         *
         * Identifies headers that are frequently used but not yet precompiled.
         *
         * @param trace The build trace.
         * @param graph The dependency graph.
         * @param current_pch_file Path to the current PCH file.
         * @return A Result containing a list of suggested headers to add.
         */
        static core::Result<std::vector<std::string>> suggest_pch_additions(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            const std::string& current_pch_file
        );

        /**
         * Suggests headers that may be unnecessary in the current PCH.
         *
         * Finds headers included in the PCH that provide little to no performance benefit.
         *
         * @param trace The build trace.
         * @param graph The dependency graph.
         * @param current_pch_file Path to the current PCH file.
         * @return A Result containing a list of headers recommended for removal.
         */
        static core::Result<std::vector<std::string>> suggest_pch_removals(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            const std::string& current_pch_file
        );

        /**
         * Calculates the benefit score for a given header's inclusion in the PCH.
         *
         * Combines inclusion frequency, compile-time impact, and relative importance.
         *
         * @param header The header file being evaluated.
         * @param inclusion_count Number of times the header is included.
         * @param compile_time_ms Average compile time associated with the header.
         * @param total_files Total number of files in the project.
         * @return The calculated benefit score.
         */
        static double calculate_pch_benefit_score(
            const std::string& header,
            int inclusion_count,
            double compile_time_ms,
            int total_files
        );

        /**
         * Estimates total potential compile-time savings from using a given PCH configuration.
         *
         * Evaluates how much build time could be saved by precompiling the provided set of headers.
         *
         * @param pch_headers List of headers to include in the hypothetical PCH.
         * @param trace The build trace data.
         * @param graph The dependency graph.
         * @return A Result containing the estimated time savings (in milliseconds).
         */
        static core::Result<double> estimate_pch_savings(
            const std::vector<std::string>& pch_headers,
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph
        );

    private:
        /**
         * Counts how many times each header is included across all source files.
         *
         * @param graph The dependency graph containing include relationships.
         * @return A map of header paths to their inclusion counts.
         */
        [[nodiscard]] static std::unordered_map<std::string, int> count_header_inclusions(
            const core::DependencyGraph& graph
        ) ;

        /**
         * Estimates the compile-time contribution of each header.
         *
         * @param trace The build trace data.
         * @param graph The dependency graph for inclusion context.
         * @return A map of header paths to estimated compile times (in milliseconds).
         */
        [[nodiscard]] static std::unordered_map<std::string, double> estimate_header_compile_times(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph
        ) ;

        /**
         * Determines whether a header is a system or third-party header.
         *
         * System headers are typically excluded from PCH optimization analysis.
         *
         * @param header The header file path.
         * @return True if the header is a system header, false otherwise.
         */
        [[nodiscard]] static bool is_system_header(const std::string& header) ;
    };

} // namespace bha::analysis


#endif //PCH_ANALYZER_H
