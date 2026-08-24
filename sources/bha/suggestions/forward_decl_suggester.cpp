#include "bha/suggestions/forward_decl_suggester.hpp"
#include "bha/suggestions/forward_decl_semantic_index.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace bha::suggestions {
    namespace {

        std::string render_forward_declaration(const ForwardDeclSemanticRecord& record) {
            if (record.keyword.empty() || record.unqualified_name.empty() || record.unsupported_scope ||
                record.macro_generated) {
                return {};
            }

            std::ostringstream output;
            for (const auto& namespace_context : record.namespaces) {
                if (namespace_context.inline_namespace) {
                    output << "inline ";
                }
                output << "namespace " << namespace_context.name << " { ";
            }
            output << record.keyword << " " << record.unqualified_name << ";";
            for (std::size_t index = 0; index < record.namespaces.size(); ++index) {
                output << " }";
            }
            return output.str();
        }

        std::string render_forward_declarations(
            const std::vector<ForwardDeclSemanticRecord>& records
        ) {
            std::vector<std::string> declarations;
            declarations.reserve(records.size());
            for (const auto& record : records) {
                const auto declaration = render_forward_declaration(record);
                if (!declaration.empty()) {
                    declarations.push_back(declaration);
                }
            }
            std::ranges::sort(declarations);
            std::ostringstream output;
            for (const auto& declaration : declarations) {
                output << declaration << '\n';
            }
            return output.str();
        }

        bool record_is_actionable(const ForwardDeclSemanticRecord& record) {
            return record.complete_definition && !record.macro_generated &&
                !record.template_declaration &&
                !record.unsupported_scope && !record.declaration_shape_conflict &&
                !record.uses.empty() &&
                std::ranges::all_of(record.uses, [](const auto& use) {
                    return !use.requires_complete_type && !use.in_dependent_context &&
                        !use.through_alias && !use.through_template && !use.macro_expanded;
                });
        }

        std::vector<CompilationUnit> commands_for_header(
            ProjectIndex& project_index,
            const analyzers::DependencyAnalysisResult::HeaderInfo& header
        ) {
            const auto all_commands = project_index.compile_commands();
            if (header.included_by.empty()) {
                return all_commands;
            }

            std::unordered_set<std::string> includers;
            for (const auto& path : header.included_by) {
                includers.insert(project_index.resolve(path).generic_string());
            }
            std::vector<CompilationUnit> commands;
            for (const auto& command : all_commands) {
                if (includers.contains(project_index.resolve(command.source_file).generic_string())) {
                    commands.push_back(command);
                }
            }
            return commands.empty() ? all_commands : commands;
        }

        std::vector<fs::path> use_files(
            const std::vector<ForwardDeclSemanticRecord>& records,
            const fs::path& header,
            ProjectIndex& project_index
        ) {
            std::vector<fs::path> files;
            std::unordered_set<std::string> seen;
            for (const auto& record : records) {
                for (const auto& use : record.uses) {
                    const fs::path file = project_index.resolve(use.source_file);
                    if (file == project_index.resolve(header) || !fs::exists(file)) {
                        continue;
                    }
                    if (seen.insert(file.generic_string()).second) {
                        files.push_back(file);
                    }
                }
            }
            std::ranges::sort(files);
            return files;
        }

    }  // namespace

    Result<SuggestionResult, Error> ForwardDeclSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        const auto started = std::chrono::steady_clock::now();
        if (!context.project_index ||
            context.project_index->compile_commands_status() != CompilationDatabaseStatus::Loaded) {
            result.diagnostics.push_back({
                "forward_decl.semantic.index_required",
                "Forward-declaration suggestions require a valid compile_commands.json"
            });
            result.generation_time = std::chrono::steady_clock::now() - started;
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        std::vector<const analyzers::DependencyAnalysisResult::HeaderInfo*> headers;
        for (const auto& header : context.analysis.dependencies.headers) {
            if (is_header_file_path(header.path) && !header.included_by.empty() &&
                is_project_owned_path(context, header.path)) {
                headers.push_back(&header);
            }
        }
        std::ranges::sort(headers, [](const auto* left, const auto* right) {
            return left->total_parse_time > right->total_parse_time;
        });

        const std::size_t limit = context.options.max_suggestions == 0
            ? headers.size()
            : std::min(headers.size(), context.options.max_suggestions);
        for (std::size_t index = 0; index < limit; ++index) {
            if (context.is_cancelled()) {
                break;
            }
            const auto& header = *headers[index];
            ++result.items_analyzed;
            const auto commands = commands_for_header(*context.project_index, header);
            if (commands.empty()) {
                ++result.items_skipped;
                continue;
            }

            const auto semantic = analyze_forward_declarations(
                *context.project_index,
                header.path,
                commands,
                context.forward_decl_semantic_cache.get()
            );
            if (!semantic.available) {
                ++result.items_skipped;
                continue;
            }
            if (std::ranges::any_of(semantic.records, [](const auto& record) {
                    return !record_is_actionable(record);
                })) {
                ++result.items_skipped;
                continue;
            }
            const auto files = use_files(semantic.records, header.path, *context.project_index);
            if (files.empty()) {
                ++result.items_skipped;
                continue;
            }
            const auto declaration_text = render_forward_declarations(semantic.records);
            if (declaration_text.empty()) {
                ++result.items_skipped;
                continue;
            }

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("fwd-ast", header.path);
            suggestion.type = SuggestionType::ForwardDeclaration;
            suggestion.priority = Priority::High;
            suggestion.confidence = 1.0;
            suggestion.title = "Replace " + header.path.filename().string() +
                " with AST-proven forward declarations";
            suggestion.description =
                "Clang AST evidence proves that all observed uses of the declarations "
                "are pointer/reference-only and do not require complete types.";
            suggestion.rationale =
                "The candidate is derived from exact compile commands and canonical AST "
                "declaration/use bindings. No text heuristic is used to establish safety.";
            suggestion.estimated_savings = Duration::zero();
            suggestion.target_file.path = files.front();
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Replace the full include with AST-derived declarations";
            suggestion.is_safe = true;
            suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
            suggestion.application_summary = "Apply only after compile-command validation";
            suggestion.implementation_steps = {
                "Replace the full include with the AST-derived forward declarations",
                "Validate every affected translation unit with its original compile command",
                "Rebuild the affected targets and compare a fresh trace"
            };
            suggestion.caveats = {
                "Savings are intentionally unestimated until a post-edit trace is available",
                "Unsupported macros, aliases, templates, dependent contexts, and complete-type uses are rejected"
            };

            const std::unordered_set<std::string> use_file_keys = [&] {
                std::unordered_set<std::string> keys;
                for (const auto& file : files) {
                    keys.insert(file.generic_string());
                }
                return keys;
            }();
            std::unordered_set<std::string> edited_ranges;
            std::vector<ForwardDeclSemanticInclude> selected_includes;
            for (const auto& include : semantic.includes) {
                const fs::path file = context.project_index->resolve(include.including_file);
                if (!use_file_keys.contains(file.generic_string())) {
                    continue;
                }
                const auto edit_key = file.generic_string() + ":" +
                    std::to_string(include.offset) + ":" + std::to_string(include.length);
                if (!edited_ranges.insert(edit_key).second) {
                    continue;
                }
                selected_includes.push_back(include);
                TextEdit edit;
                edit.file = file;
                edit.start_line = include.line;
                edit.start_col = include.col_start;
                edit.end_line = include.line;
                edit.end_col = include.col_end;
                edit.new_text = format_separated_block(declaration_text);
                edit.byte_offset = include.offset;
                edit.byte_length = include.length;
                suggestion.edits.push_back(std::move(edit));
                FileTarget target;
                target.path = file;
                target.action = FileAction::Modify;
                target.note = "AST-proven incomplete-type-safe use";
                suggestion.secondary_files.push_back(std::move(target));
            }
            if (suggestion.edits.empty()) {
                ++result.items_skipped;
                continue;
            }
            std::string validation_diagnostic;
            if (!validate_forward_decl_replacements(
                    *context.project_index,
                    commands,
                    selected_includes,
                    format_separated_block(declaration_text),
                    validation_diagnostic
                )) {
                ++result.items_skipped;
                continue;
            }
            suggestion.impact.total_files_affected = suggestion.secondary_files.size();
            result.suggestions.push_back(std::move(suggestion));
        }

        std::ranges::sort(result.suggestions, [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
        result.generation_time = std::chrono::steady_clock::now() - started;
        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_forward_decl_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<ForwardDeclSuggester>()
        );
    }
}  // namespace bha::suggestions
