//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_PIMPL_SUGGESTER_HPP
#define BHA_PIMPL_SUGGESTER_HPP

/**
 * @file pimpl_suggester.hpp
 * @brief Suggester for PIMPL (Pointer to Implementation) pattern refactoring.
 */

#include "bha/suggestions/suggester.hpp"

namespace bha::suggestions {

    /**
     * @class PIMPLSuggester
     * @brief Suggests classes that could benefit from the PIMPL idiom.
     *
     * Identifies source files with high compile-time impact due to their
     * inclusion dependencies and suggests applying the PIMPL pattern to
     * reduce compile-time coupling.
     */
    class PIMPLSuggester : public ISuggester {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "PIMPLSuggester";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Suggests PIMPL (Pointer to Implementation) pattern for classes "
                   "with high compile-time dependency impact";
        }

        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::PIMPLPattern;
        }

        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context) const override;
    };

    /**
     * @brief Registers the PIMPL suggester with the global registry.
     */
    void register_pimpl_pattern_suggester();

}  // namespace bha::suggestions

#endif //BHA_PIMPL_SUGGESTER_HPP