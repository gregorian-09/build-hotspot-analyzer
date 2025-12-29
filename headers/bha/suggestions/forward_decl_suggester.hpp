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
        [[nodiscard]] std::string_view name() const noexcept override {
            return "ForwardDeclSuggester";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Identifies opportunities for forward declarations to reduce includes";
        }

        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::ForwardDeclaration;
        }

        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const override;
    };

    void register_forward_decl_suggester();

}  // namespace bha::suggestions

#endif //BHA_FORWARD_DECL_SUGGESTER_HPP