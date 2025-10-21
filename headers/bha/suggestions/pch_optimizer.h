//
// Created by gregorian on 20/10/2025.
//

#ifndef PCH_OPTIMIZER_H
#define PCH_OPTIMIZER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <string>

namespace bha::suggestions {

    /**
     * @struct PCHOptimizationResult
     * Represents the outcome of a precompiled header (PCH) optimization analysis.
     *
     * This structure holds information about headers that should be added or removed
     * from a PCH file, the recommended PCH contents, and the estimated build performance
     * impact of applying the optimization.
     */
    struct PCHOptimizationResult {
        /** Headers that should be added to the PCH for optimal benefit. */
        std::vector<std::string> headers_to_add;

        /** Headers that should be removed from the PCH to reduce overhead. */
        std::vector<std::string> headers_to_remove;

        /** The suggested combined PCH content (header include directives). */
        std::string suggested_pch_content;

        /** Estimated total compile-time savings (in milliseconds) after optimization. */
        double estimated_time_savings_ms;

        /** Confidence score (0.0–1.0) indicating reliability of the optimization recommendation. */
        double confidence;
    };

    /**
     * @class PCHOptimizer
     * Provides analysis and recommendations for optimizing precompiled headers (PCH).
     *
     * The PCHOptimizer identifies headers that should be added or removed from precompiled
     * header sets to maximize compilation efficiency. It uses build trace data, dependency
     * graphs, and header inclusion frequency to estimate potential performance improvements.
     */
    class PCHOptimizer {
    public:
        /** Default constructor. */
        PCHOptimizer() = default;

        /**
         * Performs full PCH optimization analysis.
         *
         * Combines header inclusion data and compilation timing information to determine
         * the most beneficial headers to include or exclude in the PCH. Also generates
         * a suggested PCH file content with estimated build-time savings.
         *
         * @param trace The build trace containing per-file compile times.
         * @param graph The dependency graph describing file inclusion relationships.
         * @param current_pch_headers The current list of headers in the PCH file.
         * @return A Result containing a @ref PCHOptimizationResult with recommendations.
         */
        static core::Result<PCHOptimizationResult> optimize_pch(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            const std::vector<std::string>& current_pch_headers
        );

        /**
         * Suggests headers that could be beneficially added to the PCH.
         *
         * Headers are recommended based on their inclusion frequency across translation units
         * and their contribution to compile time.
         *
         * @param trace The build trace for compile-time context.
         * @param graph The dependency graph showing inclusion relationships.
         * @param top_n Maximum number of headers to suggest.
         * @param min_inclusion_ratio Minimum inclusion ratio threshold for consideration.
         * @return A Result containing a list of suggested header file paths.
         */
        static core::Result<std::vector<std::string>> suggest_headers_to_add(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            int top_n = 10,
            double min_inclusion_ratio = 0.5
        );

        /**
         * Suggests headers that should be removed from the PCH.
         *
         * Identifies headers that are rarely included or provide minimal compile-time benefit.
         *
         * @param trace The build trace providing timing information.
         * @param graph The dependency graph describing header relationships.
         * @param current_pch_headers The current list of headers in the PCH.
         * @return A Result containing a list of headers recommended for removal.
         */
        static core::Result<std::vector<std::string>> suggest_headers_to_remove(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            const std::vector<std::string>& current_pch_headers
        );

        /**
         * Generates human-readable suggestions related to PCH configuration.
         *
         * Produces a set of @ref core::Suggestion objects describing how to modify
         * the PCH to improve build performance.
         *
         * @param trace The build trace used for timing and inclusion analysis.
         * @param graph The dependency graph of the project.
         * @param current_pch_headers The headers currently part of the PCH.
         * @return A Result containing a list of high-level PCH optimization suggestions.
         */
        static core::Result<std::vector<core::Suggestion>> generate_pch_suggestions(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            const std::vector<std::string>& current_pch_headers
        );

        /**
         * Generates a PCH header file content string.
         *
         * Constructs a textual representation of the PCH header file
         * with proper `#include` directives for all specified headers.
         *
         * @param headers The list of headers to include in the PCH.
         * @return A Result containing the generated PCH header content as a string.
         */
        static core::Result<std::string> generate_pch_header_file(
            const std::vector<std::string>& headers
        );

        /**
         * Estimates the performance benefit of a proposed PCH optimization.
         *
         * Evaluates the compile-time improvement from adding and removing specific headers
         * based on inclusion frequency and compile cost metrics.
         *
         * @param headers_to_add Headers being proposed for addition to the PCH.
         * @param headers_to_remove Headers being proposed for removal from the PCH.
         * @param trace The build trace data.
         * @param graph The dependency graph of the project.
         * @return Estimated build-time savings in milliseconds.
         */
        static double estimate_pch_optimization_benefit(
            const std::vector<std::string>& headers_to_add,
            const std::vector<std::string>& headers_to_remove,
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph
        );

    private:
        /**
         * Calculates the relative importance of a header file for PCH inclusion.
         *
         * Combines inclusion count, compile time, and file coverage to score header importance.
         *
         * @param inclusion_count The number of translation units including the header.
         * @param compile_time_ms The average compile time contribution of the header.
         * @param total_files The total number of translation units analyzed.
         * @return A computed importance score.
         */
        [[nodiscard]] static double calculate_header_importance(
            int inclusion_count,
            double compile_time_ms,
            int total_files
        ) ;

        /**
         * Determines whether a header is a system header.
         *
         * System headers (e.g., `<iostream>`) are typically excluded from PCH optimization.
         *
         * @param header The header path to check.
         * @return True if the header is a system header, false otherwise.
         */
        [[nodiscard]] static bool is_system_header(const std::string& header) ;

        /**
         * Determines whether a header should remain in the PCH.
         *
         * Uses inclusion frequency and compile-time contribution heuristics.
         *
         * @param inclusion_ratio The proportion of translation units including this header.
         * @param compile_time_ms The header's average compile-time cost.
         * @return True if the header should remain in the PCH, false otherwise.
         */
        [[nodiscard]] static bool should_remain_in_pch(
            double inclusion_ratio,
            double compile_time_ms
        ) ;
    };

} // namespace bha::suggestions

#endif //PCH_OPTIMIZER_H
