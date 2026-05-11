//
// Created by gregorian-rayne on 01/18/26.
//

#ifndef BHA_SUGGESTION_CONSOLIDATOR_HPP
#define BHA_SUGGESTION_CONSOLIDATOR_HPP

/**
 * @file consolidator.hpp
 * @brief Consolidates fragmented suggestions into comprehensive recommendations.
 *
 * The consolidator takes multiple related suggestions and merges them into
 * single, actionable recommendations that reference actual codebase files
 * and provide complete context for decision-making.
 */

#include "bha/types.hpp"
#include "bha/suggestions/suggester.hpp"

#include <vector>

namespace bha::suggestions
{
    /**
     * @brief Tunables controlling suggestion consolidation behavior.
     */
    struct ConsolidationOptions {
        /// Enables/disables consolidation stage.
        bool enable_consolidation = true;
        /// Soft cap for items represented in one merged suggestion body.
        std::size_t max_items_per_suggestion = 50;
        /// Whether external/system headers are retained in merged output.
        bool include_external_headers = false;
        /// Age threshold used for stability heuristics in consolidation.
        Duration stability_threshold = std::chrono::hours(24 * 30 * 6);
    };

    /**
     * Consolidates related suggestions into comprehensive recommendations.
     */
    class SuggestionConsolidator {
    public:
        /**
         * @brief Construct consolidator with caller-provided options.
         */
        explicit SuggestionConsolidator(const ConsolidationOptions& options = {})
            : options_(options) {}

        /**
         * Consolidates a list of suggestions.
         *
         * @param suggestions Raw suggestions from all suggesters.
         * @return Consolidated suggestions with merged information and reduced duplication.
         */
        [[nodiscard]] std::vector<Suggestion> consolidate(
            std::vector<Suggestion> suggestions
        ) const;

    private:
        /**
         * Consolidates PCH suggestions into a single recommendation.
         */
        [[nodiscard]] static std::optional<Suggestion> consolidate_pch(
            const std::vector<Suggestion>& suggestions
        );

        /**
         * Consolidates header split suggestions.
         */
        static std::optional<Suggestion> consolidate_header_split(
            const std::vector<Suggestion>& suggestions
        ) ;

        /**
         * Consolidates unity build suggestions.
         */
        [[nodiscard]] std::optional<Suggestion> consolidate_unity_build(
            const std::vector<Suggestion>& suggestions
        ) const;

        /**
         * Consolidates include removal suggestions.
         */
        static std::optional<Suggestion> consolidate_include_removal(
            const std::vector<Suggestion>& suggestions
        ) ;

        /**
         * Consolidates forward declaration suggestions.
         */
        static std::optional<Suggestion> consolidate_forward_decl(
            const std::vector<Suggestion>& suggestions
        ) ;

        /**
         * Consolidates template instantiation suggestions.
         */
        static std::optional<Suggestion> consolidate_template(
            const std::vector<Suggestion>& suggestions
        ) ;

        /**
         * Consolidates PIMPL pattern suggestions.
         */
        static std::optional<Suggestion> consolidate_pimpl(
            const std::vector<Suggestion>& suggestions
        );

        /**
         * Merges impact assessments from multiple suggestions.
         */
        [[nodiscard]] static Impact merge_impacts(
            const std::vector<Suggestion>& suggestions
        );

        /**
         * Generates consolidated implementation steps.
         */
        [[nodiscard]] static std::vector<std::string> merge_steps(
            const std::vector<Suggestion>& suggestions
        );

        /**
         * Merges TextEdits from multiple suggestions, handling conflicts.
         *
         * The merge keeps deterministic ordering and drops exact duplicates.
         */
        [[nodiscard]] static std::vector<TextEdit> merge_edits(
            const std::vector<Suggestion>& suggestions
        );

        ConsolidationOptions options_;
    };

}  // namespace bha::suggestions

#endif //BHA_SUGGESTION_CONSOLIDATOR_HPP
