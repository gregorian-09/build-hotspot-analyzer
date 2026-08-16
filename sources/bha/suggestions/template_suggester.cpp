// Template suggestions require semantic evidence from the Clang AST index.

#include "bha/suggestions/template_suggester.hpp"

#include "bha/suggestions/template_semantic_index.hpp"

namespace bha::suggestions {

    Result<SuggestionResult, Error> TemplateSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;

        // Aggregate timing and unvalidated trace records cannot identify a safe
        // explicit-instantiation edit. Reject them before invoking Clang.
        if (context.trace.template_evidence != TemplateEvidence::PerSpecializationTimingWithLocations ||
            !context.trace.template_semantic_validated ||
            context.project_index == nullptr) {
            result.items_skipped = context.analysis.templates.templates.size();
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        TemplateSemanticIndex semantic_index(*context.project_index);
        semantic_index.build();
        if (semantic_index.status() != TemplateSemanticStatus::Parsed) {
            result.items_skipped = context.analysis.templates.templates.size();
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        for (const auto& candidate : context.analysis.templates.templates) {
            if (context.is_cancelled()) {
                result.items_skipped += context.analysis.templates.templates.size() -
                    result.items_analyzed - result.items_skipped;
                break;
            }

            const auto* record = candidate.full_signature.empty()
                ? nullptr
                : semantic_index.find_exact(candidate.full_signature);
            if (record == nullptr || !record->complete_definition ||
                !record->has_external_linkage || record->use_files.empty() ||
                !record->has_single_explicit_definition) {
                ++result.items_skipped;
                continue;
            }

            // Validation is intentionally separate from edit generation. The
            // AST record is now the only accepted evidence for future edits.
            ++result.items_analyzed;
        }

        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_template_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<TemplateSuggester>()
        );
    }

}  // namespace bha::suggestions
