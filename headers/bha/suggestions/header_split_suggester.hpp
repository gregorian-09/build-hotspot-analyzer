//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_HEADER_SPLIT_SUGGESTER_HPP
#define BHA_HEADER_SPLIT_SUGGESTER_HPP

/**
 * @file header_split_suggester.hpp
 * @brief Header split suggestions.
 *
 * Identifies large, expensive headers that could benefit from being
 * split into smaller, more focused headers. This reduces compilation
 * dependencies when files only need a subset of the functionality.
 *
 * Detection criteria:
 * - Header has high parse time
 * - Header is included by many files
 * - Different includers use different subsets of symbols (if detectable)
 */

#include "bha/suggestions/suggester.hpp"

namespace bha::suggestions {

    /**
     * Suggests splitting large headers into smaller ones.
     */
    class HeaderSplitSuggester : public ISuggester {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "HeaderSplitSuggester";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Identifies large headers that could be split into smaller, focused headers";
        }

        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::HeaderSplit;
        }

        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const override;
    };

    void register_header_split_suggester();

}  // namespace bha::suggestions

#endif //BHA_HEADER_SPLIT_SUGGESTER_HPP