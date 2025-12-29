//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_INCLUDE_SUGGESTER_HPP
#define BHA_INCLUDE_SUGGESTER_HPP

/**
 * @file include_suggester.hpp
 * @brief Include optimization suggestions.
 *
 * Analyzes include patterns to identify:
 * - Headers that can be removed (unused includes)
 * - Headers that should be included directly (IWYU)
 * - Headers that can be moved from .h to .cpp
 */

#include "bha/suggestions/suggester.hpp"

namespace bha::suggestions {

    /**
     * Suggests include optimizations for faster builds.
     */
    class IncludeSuggester : public ISuggester {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "IncludeSuggester";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Identifies include optimizations (removal, IWYU, movement)";
        }

        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::IncludeRemoval;
        }

        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const override;
    };

    void register_include_suggester();

}  // namespace bha::suggestions

#endif //BHA_INCLUDE_SUGGESTER_HPP