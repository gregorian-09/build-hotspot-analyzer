// Template suggestions require semantic evidence from the Clang AST index.

#include "bha/suggestions/template_suggester.hpp"

#include "bha/suggestions/suggester.hpp"
#include "bha/suggestions/template_semantic_index.hpp"

#include <algorithm>
#include <filesystem>

namespace {

    std::optional<bha::TextEdit> make_extern_template_edit(
        const bha::suggestions::TemplateSemanticRecord& record,
        bha::ProjectIndex& project_index
    ) {
        if (record.declaration_kind != "class" ||
            record.has_explicit_instantiation_declaration ||
            !record.complete_definition || !record.has_external_linkage ||
            !record.has_single_explicit_definition || record.has_dependent_arguments ||
            record.has_unsupported_scope || record.declaration_file.empty() ||
            record.declaration_end_line == 0 || record.explicit_definition_files.empty()) {
            return std::nullopt;
        }

        const auto extension = record.declaration_file.extension().string();
        if (extension != ".h" && extension != ".hh" && extension != ".hpp" &&
            extension != ".hxx" && extension != ".inl" && extension != ".ipp") {
            return std::nullopt;
        }

        const auto& owner = record.explicit_definition_files.front();
        if (owner == record.declaration_file || !project_index.compile_command_for(owner).has_value()) {
            return std::nullopt;
        }
        for (const auto& use_file : record.use_files) {
            if (!project_index.compile_command_for(use_file).has_value()) {
                return std::nullopt;
            }
        }

        const auto declaration = project_index.read_file(record.declaration_file);
        if (!declaration.has_value()) {
            return std::nullopt;
        }
        const auto line_count = static_cast<std::size_t>(
            std::count(declaration->begin(), declaration->end(), '\n')
        ) + 1;
        if (record.declaration_end_line > line_count) {
            return std::nullopt;
        }

        bha::TextEdit edit;
        edit.file = record.declaration_file;
        edit.start_line = record.declaration_end_line;
        edit.start_col = 0;
        edit.end_line = edit.start_line;
        edit.end_col = 0;
        edit.new_text = record.canonical_extern_declaration + "\n";
        return edit;
    }

}  // namespace

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
            if (context.trace.template_evidence != TemplateEvidence::PerSpecializationTimingWithLocations) {
                result.diagnostics.push_back({
                    "template.evidence.insufficient",
                    "Template edits require per-specialization timing with source locations; aggregate or missing timing evidence was rejected."
                });
            } else if (!context.trace.template_semantic_validated) {
                result.diagnostics.push_back({
                    "template.semantic.unvalidated",
                    "Template edits require semantic validation from the Clang AST index."
                });
            } else {
                result.diagnostics.push_back({
                    "template.index.unavailable",
                    "Template edits require a project index backed by a compilation database."
                });
            }
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        TemplateSemanticIndex semantic_index(*context.project_index);
        semantic_index.build();
        if (semantic_index.status() != TemplateSemanticStatus::Parsed) {
            result.items_skipped = context.analysis.templates.templates.size();
            result.diagnostics.push_back({
                "template.semantic.index_failed",
                semantic_index.diagnostic()
            });
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
                !record->has_external_linkage || record->use_files.empty() || record->uses.empty() ||
                !record->has_single_explicit_definition || record->has_dependent_arguments ||
                record->has_dependent_use_context || record->has_unsupported_scope) {
                ++result.items_skipped;
                continue;
            }

            const auto edit = make_extern_template_edit(*record, *context.project_index);
            if (!edit.has_value()) {
                ++result.items_skipped;
                continue;
            }

            Suggestion suggestion;
            suggestion.type = SuggestionType::ExplicitTemplate;
            suggestion.priority = Priority::High;
            suggestion.confidence = 1.0;
            suggestion.title = "Extern template for " + record->specialization;
            suggestion.description =
                "Add the Clang-derived extern template declaration to the header while "
                "retaining the single explicit-instantiation definition.";
            suggestion.rationale =
                "The declaration, use sites, and unique explicit-instantiation owner were "
                "verified from the compilation database and Clang AST.";
            suggestion.estimated_savings = Duration::zero();
            suggestion.target_file.path = record->declaration_file;
            suggestion.target_file.line_start = record->declaration_end_line + 1;
            suggestion.target_file.line_end = record->declaration_end_line + 1;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Insert canonical AST-derived extern template declaration";
            suggestion.edits.push_back(*edit);
            suggestion.is_safe = true;
            suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
            suggestion.implementation_steps = {
                "Apply the canonical extern template declaration",
                "Rebuild all compile-command-backed translation units",
                "Verify the explicit-instantiation owner remains linked"
            };
            suggestion.caveats = {
                "Savings are intentionally unestimated until a post-edit trace is available",
                "The edit is limited to a unique explicit class-instantiation owner"
            };
            suggestion.verification = "Clang syntax validation and full rebuild validation are required";
            result.suggestions.push_back(std::move(suggestion));
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
