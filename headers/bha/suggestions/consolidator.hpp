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
#include <unordered_map>

namespace bha::suggestions
{
    /**
     * Consolidation options.
     */
    struct ConsolidationOptions {
        bool enable_consolidation = true;
        std::size_t max_items_per_suggestion = 50;
        bool include_external_headers = false;
        Duration stability_threshold = std::chrono::hours(24 * 30 * 6);
    };

    /**
     * Consolidates related suggestions into comprehensive recommendations.
     */
    class SuggestionConsolidator {
    public:
        explicit SuggestionConsolidator(ConsolidationOptions options = {})
            : options_(std::move(options)) {}

        /**
         * Consolidates a list of suggestions.
         *
         * @param suggestions Raw suggestions from all suggesters.
         * @return Consolidated suggestions with merged information.
         */
        [[nodiscard]] std::vector<Suggestion> consolidate(
            std::vector<Suggestion> suggestions
        ) const;

    private:
        /**
         * Consolidates PCH suggestions into a single recommendation.
         */
        [[nodiscard]] std::optional<Suggestion> consolidate_pch(
            const std::vector<Suggestion>& suggestions
        ) const;

        /**
         * Consolidates header split suggestions.
         */
        [[nodiscard]] std::optional<Suggestion> consolidate_header_split(
            const std::vector<Suggestion>& suggestions
        ) const;

        /**
         * Consolidates unity build suggestions.
         */
        [[nodiscard]] std::optional<Suggestion> consolidate_unity_build(
            const std::vector<Suggestion>& suggestions
        ) const;

        /**
         * Consolidates include removal suggestions.
         */
        [[nodiscard]] std::optional<Suggestion> consolidate_include_removal(
            const std::vector<Suggestion>& suggestions
        ) const;

        /**
         * Consolidates forward declaration suggestions.
         */
        [[nodiscard]] std::optional<Suggestion> consolidate_forward_decl(
            const std::vector<Suggestion>& suggestions
        ) const;

        /**
         * Consolidates template instantiation suggestions.
         */
        [[nodiscard]] std::optional<Suggestion> consolidate_template(
            const std::vector<Suggestion>& suggestions
        ) const;

        /**
         * Merges impact assessments from multiple suggestions.
         */
        [[nodiscard]] Impact merge_impacts(
            const std::vector<Suggestion>& suggestions
        ) const;

        /**
         * Generates consolidated implementation steps.
         */
        [[nodiscard]] std::vector<std::string> merge_steps(
            const std::vector<Suggestion>& suggestions
        ) const;

        ConsolidationOptions options_;
    };

}  // namespace bha::suggestions

#endif //BHA_SUGGESTION_CONSOLIDATOR_HPP
