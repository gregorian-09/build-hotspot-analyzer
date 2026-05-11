//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_FORWARD_DECL_SUGGESTER_HPP
#define BHA_FORWARD_DECL_SUGGESTER_HPP

/**
 * @file forward_decl_suggester.hpp
 * @brief Forward declaration suggestions.
 *
 * Analyzes header dependencies to identify where forward declarations
 * can replace full includes, reducing compilation dependencies.
 *
 * Criteria for forward declaration opportunities:
 * - Type is used only by pointer or reference
 * - Header is expensive to parse
 * - Include chain would be broken
 */

#include "bha/suggestions/suggester.hpp"

namespace bha::suggestions {

    /**
     * Suggests opportunities for using forward declarations.
     */
    class ForwardDeclSuggester : public ISuggester {
    public:
        /// Stable suggester identifier.
        [[nodiscard]] std::string_view name() const noexcept override {
            return "ForwardDeclSuggester";
        }

        /// Human-readable behavior summary for UI/CLI surfaces.
        [[nodiscard]] std::string_view description() const noexcept override {
            return "Identifies opportunities for forward declarations to reduce includes";
        }

        /// Primary suggestion type emitted by this suggester.
        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::ForwardDeclaration;
        }

        /**
         * @brief Generate forward-declaration substitution suggestions.
         *
         * @param context Analysis context containing traces, analyzer outputs, and options.
         * @return Suggestion generation result or structured error.
         */
        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const override;
    };

    /**
     * @brief Register `ForwardDeclSuggester` with the global suggester registry.
     */
    void register_forward_decl_suggester();

}  // namespace bha::suggestions

#endif //BHA_FORWARD_DECL_SUGGESTER_HPP
