// Template suggestions require semantic evidence from the Clang AST index.

#include "bha/suggestions/template_suggester.hpp"

namespace bha::suggestions {

    Result<SuggestionResult, Error> TemplateSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;

        // Timing data is useful for reporting, but it cannot prove that an
        // extern/explicit-instantiation edit is valid. Until the semantic index
        // is connected, fail closed instead of falling back to text heuristics.
        result.items_skipped = context.analysis.templates.templates.size();
        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_template_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<TemplateSuggester>()
        );
    }

}  // namespace bha::suggestions
