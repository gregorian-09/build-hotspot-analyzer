//
// Created by gregorian on 20/10/2025.
//

#ifndef SUGGESTION_ENGINE_H
#define SUGGESTION_ENGINE_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include "bha/suggestions/header_splitter.h"
#include <vector>

namespace bha::suggestions {

    /**
     * @class SuggestionEngine
     * Central engine for generating build optimization suggestions.
     *
     * The SuggestionEngine aggregates multiple suggestion modules (forward declaration,
     * header splitting, PCH optimization, and PIMPL usage) to provide a unified list of
     * optimization opportunities for a given build trace.
     */
    class SuggestionEngine {
    public:
        /**
         * @struct Options
         * Configuration options for controlling suggestion generation.
         *
         * Defines which suggestion categories are enabled, as well as thresholds
         * for confidence, performance impact, and filtering limits.
         */
        struct Options {
            bool enable_forward_declarations = true; ///< Enable forward declaration suggestions.
            bool enable_header_splits = true;        ///< Enable header file splitting suggestions.
            bool enable_pch_suggestions = true;      ///< Enable precompiled header (PCH) suggestions.
            bool enable_pimpl = false;               ///< Enable PIMPL (pointer to implementation) pattern suggestions.

            double min_confidence = 0.5;             ///< Minimum confidence threshold to include suggestions.
            double min_time_savings_ms = 50.0;       ///< Minimum estimated compile time savings (in ms).

            int max_suggestions = 20;                ///< Maximum number of suggestions to return.

            int header_split_fanout_threshold = 20;  ///< Minimum dependent count to consider a header split.
            size_t header_split_min_symbols = 4;     ///< Minimum number of symbols required to analyze a split.
        };

        /** Constructs a SuggestionEngine with default configuration. */
        SuggestionEngine();

        /** Default destructor. */
        ~SuggestionEngine() = default;

        /**
         * Generates all available optimization suggestions.
         *
         * Combines results from forward declaration, header splitting, PCH, and PIMPL
         * modules into a single ranked list of suggestions.
         *
         * @param trace The build trace to analyze.
         * @param options Configuration for controlling which suggestions to generate.
         * @return A Result containing a vector of suggestions.
         */
        core::Result<std::vector<core::Suggestion>> generate_all_suggestions(
            const core::BuildTrace& trace,
            const Options& options
        );

        /**
         * Suggests forward declaration opportunities.
         *
         * Identifies where class includes can be replaced with forward declarations
         * to reduce rebuild time and coupling.
         *
         * @param trace The build trace to analyze.
         * @return A Result containing a vector of forward declaration suggestions.
         */
        static core::Result<std::vector<core::Suggestion>> suggest_forward_declarations(
            const core::BuildTrace& trace
        );

        /**
         * Suggests header file splits based on symbol usage clustering.
         *
         * Analyzes large or high-fanout headers and recommends splitting them
         * into smaller, more modular units.
         *
         * @param graph The dependency graph.
         * @param options Suggestion generation options.
         * @return A Result containing a vector of header split suggestions.
         */
        core::Result<std::vector<core::Suggestion>> suggest_header_splits(
            const core::DependencyGraph& graph,
            const Options& options
        );

        /**
         * Suggests optimizations for precompiled headers (PCH).
         *
         * Identifies headers that should be added to or removed from the PCH
         * to improve compilation performance.
         *
         * @param trace The build trace.
         * @param graph The dependency graph.
         * @return A Result containing a vector of PCH optimization suggestions.
         */
        static core::Result<std::vector<core::Suggestion>> suggest_pch_optimization(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph
        );

        /**
         * Suggests classes that can benefit from using the PIMPL pattern.
         *
         * Analyzes classes for private members and includes that indicate high coupling
         * and large rebuild surfaces, suggesting PIMPL usage when beneficial.
         *
         * @param trace The build trace to analyze.
         * @return A Result containing a vector of PIMPL pattern suggestions.
         */
        static core::Result<std::vector<core::Suggestion>> suggest_pimpl_patterns(
            const core::BuildTrace& trace
        );

        /**
         * Filters and ranks the generated suggestions.
         *
         * Applies thresholds for minimum confidence and minimum time savings,
         * then ranks suggestions by expected benefit and truncates to a max count.
         *
         * @param suggestions List of generated suggestions to filter.
         * @param min_confidence Minimum confidence required.
         * @param min_savings Minimum time savings required.
         * @param max_count Maximum number of suggestions to keep.
         * @return A single top-ranked suggestion.
         */
        static core::Result<core::Suggestion> filter_and_rank(
            std::vector<core::Suggestion>& suggestions,
            double min_confidence,
            double min_savings,
            int max_count
        );

    private:
        std::unique_ptr<HeaderSplitter> header_splitter_; ///< Internal utility for analyzing and splitting headers.

        /**
         * Determines if a suggestion should be included based on thresholds.
         *
         * @param suggestion The suggestion to evaluate.
         * @param min_confidence The minimum confidence threshold.
         * @param min_savings The minimum compile time savings threshold.
         * @return `true` if the suggestion passes filters, `false` otherwise.
         */
        static bool should_include_suggestion(
            const core::Suggestion& suggestion,
            double min_confidence,
            double min_savings
        );

        /**
         * Converts a header split analysis result into a standard Suggestion object.
         *
         * @param split_suggestion The header split analysis result.
         * @return A formatted Suggestion ready for inclusion in reports.
         */
        static core::Suggestion header_split_to_suggestion(
            const HeaderSplitSuggestion& split_suggestion
        );
    };
}

#endif //SUGGESTION_ENGINE_H
