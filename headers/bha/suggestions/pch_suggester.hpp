//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_PCH_SUGGESTER_HPP
#define BHA_PCH_SUGGESTER_HPP

/**
 * @file pch_suggester.hpp
 * @brief Precompiled header suggestions.
 *
 * Identifies headers that are expensive to parse and included frequently,
 * making them good candidates for precompiled headers.
 *
 * Criteria for PCH candidates:
 * - Header is included in multiple translation units
 * - Total parse time exceeds threshold
 * - Header is stable (not frequently modified)
 */

#include "bha/suggestions/suggester.hpp"

namespace bha::suggestions {

    /**
     * Suggests headers for precompiled header optimization.
     */
    class PCHSuggester : public ISuggester {
    public:
        /// Stable suggester identifier.
        [[nodiscard]] std::string_view name() const noexcept override {
            return "PCHSuggester";
        }

        /// Human-readable behavior summary for UI/CLI surfaces.
        [[nodiscard]] std::string_view description() const noexcept override {
            return "Identifies headers suitable for precompiled header optimization";
        }

        /// Primary suggestion type emitted by this suggester.
        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::PCHOptimization;
        }

        /**
         * @brief Generate precompiled-header optimization suggestions.
         *
         * @param context Analysis context containing traces, analyzer outputs, and options.
         * @return Suggestion generation result or structured error.
         */
        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const override;
    };

    /**
     * @brief Register `PCHSuggester` with the global suggester registry.
     */
    void register_pch_suggester();

}  // namespace bha::suggestions

#endif //BHA_PCH_SUGGESTER_HPP
