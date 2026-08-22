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
     * Identifies project-owned classes with private data members from a
     * compile-command-backed Clang AST. The current output is advisory only;
     * structural PIMPL edits are not emitted until a complete AST replacement
     * backend exists.
     */
    class PIMPLSuggester : public ISuggester {
    public:
        /// Stable suggester identifier.
        [[nodiscard]] std::string_view name() const noexcept override {
            return "PIMPLSuggester";
        }

        /// Human-readable behavior summary for UI/CLI surfaces.
        [[nodiscard]] std::string_view description() const noexcept override {
            return "Suggests PIMPL (Pointer to Implementation) pattern for classes "
                   "with high compile-time dependency impact";
        }

        /// Primary suggestion type emitted by this suggester.
        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::PIMPLPattern;
        }

        /**
         * @brief Generate AST-backed advisory PIMPL candidates.
         *
         * @param context Analysis context containing traces, analyzer outputs, and options.
         * @return Suggestion generation result or structured error.
         */
        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context) const override;
    };

    /**
     * @brief Registers the PIMPL suggester with the global registry.
     */
    void register_pimpl_pattern_suggester();

}  // namespace bha::suggestions

#endif //BHA_PIMPL_SUGGESTER_HPP
