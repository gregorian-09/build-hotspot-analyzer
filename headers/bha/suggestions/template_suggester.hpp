//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_TEMPLATE_SUGGESTER_HPP
#define BHA_TEMPLATE_SUGGESTER_HPP

/**
 * @file template_suggester.hpp
 * @brief Template optimization suggestions.
 *
 * Analyzes template instantiations to identify:
 * - Templates that should use explicit instantiation
 * - Templates with excessive instantiation counts
 * - Opportunities for extern template declarations
 */

#include "bha/suggestions/suggester.hpp"

namespace bha::suggestions {

    /**
     * Suggests template optimizations for faster builds.
     */
    class TemplateSuggester : public ISuggester {
    public:
        /// Stable suggester identifier.
        [[nodiscard]] std::string_view name() const noexcept override {
            return "TemplateSuggester";
        }

        /// Human-readable behavior summary for UI/CLI surfaces.
        [[nodiscard]] std::string_view description() const noexcept override {
            return "Identifies expensive templates for explicit instantiation";
        }

        /// Primary suggestion type emitted by this suggester.
        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::ExplicitTemplate;
        }

        /**
         * @brief Generate explicit-template-instantiation suggestions.
         *
         * @param context Analysis context containing traces, analyzer outputs, and options.
         * @return Suggestion generation result or structured error.
         */
        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const override;
    };

    /**
     * @brief Register `TemplateSuggester` with the global suggester registry.
     */
    void register_template_suggester();

}  // namespace bha::suggestions

#endif //BHA_TEMPLATE_SUGGESTER_HPP
