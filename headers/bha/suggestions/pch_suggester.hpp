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
        [[nodiscard]] std::string_view name() const noexcept override {
            return "PCHSuggester";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Identifies headers suitable for precompiled header optimization";
        }

        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::PCHOptimization;
        }

        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const override;
    };

    void register_pch_suggester();

}  // namespace bha::suggestions

#endif //BHA_PCH_SUGGESTER_HPP