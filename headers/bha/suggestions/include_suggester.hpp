//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_INCLUDE_SUGGESTER_HPP
#define BHA_INCLUDE_SUGGESTER_HPP

/**
 * @file include_suggester.hpp
 * @brief Include optimization suggestions.
 *
 * Emits removals only when Clang's misc-include-cleaner reports an unused
 * include for a translation unit in the active compilation database.
 */

#include "bha/suggestions/suggester.hpp"

namespace bha::suggestions {

    /**
     * Suggests include optimizations for faster builds.
     */
    class IncludeSuggester : public ISuggester {
    public:
        /// Stable suggester identifier.
        [[nodiscard]] std::string_view name() const noexcept override {
            return "IncludeSuggester";
        }

        /// Human-readable behavior summary for UI/CLI surfaces.
        [[nodiscard]] std::string_view description() const noexcept override {
            return "Identifies unused includes from Clang semantic diagnostics";
        }

        /// Primary suggestion type emitted by this suggester.
        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::IncludeRemoval;
        }

        /// Full set of suggestion types this suggester may emit.
        [[nodiscard]] std::vector<SuggestionType> supported_types() const override {
            return {SuggestionType::IncludeRemoval};
        }

        /**
         * @brief Generate compiler-diagnostic-backed include removal suggestions.
         *
         * @param context Analysis context containing traces, analyzer outputs, and options.
         * @return Suggestion generation result or structured error.
         */
        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const override;
    };

    /**
     * @brief Register `IncludeSuggester` with the global suggester registry.
     */
    void register_include_suggester();

}  // namespace bha::suggestions

#endif //BHA_INCLUDE_SUGGESTER_HPP
