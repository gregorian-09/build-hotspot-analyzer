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
         * @brief Generate PIMPL-pattern suggestions.
         *
         * @param context Analysis context containing traces, analyzer outputs, and options.
         * @return Suggestion generation result or structured error.
         */
        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context) const override;
    };

    /**
     * @brief Generates concrete PIMPL text edits for the strict, compile-validated subset.
     *
     * This is used by the external refactor tool path so it can reuse the same
     * AST-backed class extraction and strict edit generator as the suggester.
     *
     * @param compile_commands_path Compile database used for semantic validation.
     * @param source_file Source file owning the target class implementation.
     * @param header_file Header file declaring the target class.
     * @param class_name Exact class name to refactor.
     * @return Ordered edit list when refactor is safe, otherwise detailed error.
     */
    [[nodiscard]] Result<std::vector<TextEdit>, Error> generate_pimpl_refactor_edits(
        const fs::path& compile_commands_path,
        const fs::path& source_file,
        const fs::path& header_file,
        std::string_view class_name
    );

    /**
     * @brief Registers the PIMPL suggester with the global registry.
     */
    void register_pimpl_pattern_suggester();

}  // namespace bha::suggestions

#endif //BHA_PIMPL_SUGGESTER_HPP
