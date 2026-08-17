#include "bha/suggestions/header_split_suggester.hpp"
#include "bha/suggestions/forward_decl_semantic_index.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace bha::suggestions {
    namespace {

        std::vector<std::string> split_qualified_name(const std::string& name) {
            std::vector<std::string> parts;
            std::size_t begin = 0;
            while (begin < name.size()) {
                const std::size_t separator = name.find("::", begin);
                if (separator == std::string::npos) {
                    parts.push_back(name.substr(begin));
                    break;
                }
                parts.push_back(name.substr(begin, separator - begin));
                begin = separator + 2;
            }
            return parts;
        }

        std::string render_forward_header(
            const fs::path& header,
            const std::vector<ForwardDeclSemanticRecord>& records
        ) {
            std::ostringstream output;
            output << "#pragma once\n\n";
            for (const auto& record : records) {
                if (record.keyword.empty() || record.qualified_name.empty() ||
                    record.unsupported_scope || record.macro_generated) {
                    return {};
                }
                const auto parts = split_qualified_name(record.qualified_name);
                if (parts.empty()) {
                    return {};
                }
                for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
                    output << "namespace " << parts[index] << " {\n";
                }
                output << record.keyword << " " << parts.back() << ";\n";
                for (std::size_t index = 1; index < parts.size(); ++index) {
                    output << "}\n";
                }
            }
            (void)header;
            return output.str();
        }

        bool actionable(const ForwardDeclSemanticRecord& record) {
            return record.complete_definition && !record.macro_generated &&
                !record.unsupported_scope && !record.uses.empty() &&
                std::ranges::all_of(record.uses, [](const auto& use) {
                    return !use.requires_complete_type && !use.in_dependent_context;
                });
        }

        std::vector<fs::path> semantic_use_files(
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

        std::string companion_name(const fs::path& header) {
            return header.stem().string() + "_fwd" + header.extension().string();
        }

    }  // namespace

    Result<SuggestionResult, Error> HeaderSplitSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        const auto started = std::chrono::steady_clock::now();
        if (!context.project_index ||
            context.project_index->compile_commands_status() != CompilationDatabaseStatus::Loaded) {
            result.diagnostics.push_back({
                "header_split.semantic.index_required",
                "Header splitting requires a valid compile_commands.json"
            });
            result.generation_time = std::chrono::steady_clock::now() - started;
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        std::vector<const analyzers::DependencyAnalysisResult::HeaderInfo*> headers;
        for (const auto& header : context.analysis.dependencies.headers) {
            if (is_header_file_path(header.path) && !header.included_by.empty()) {
                headers.push_back(&header);
            }
        }
        std::ranges::sort(headers, [](const auto* left, const auto* right) {
            return left->total_parse_time > right->total_parse_time;
        });

        const auto commands = context.project_index->compile_commands();
        for (const auto* header_info : headers) {
            if (context.is_cancelled()) {
                break;
            }
            ++result.items_analyzed;
            const auto semantic = analyze_forward_declarations(
                *context.project_index,
                header_info->path,
                commands
            );
            if (!semantic.available || semantic.records.empty() ||
                std::ranges::any_of(semantic.records, [](const auto& record) {
                    return !actionable(record);
                })) {
                ++result.items_skipped;
                continue;
            }

            const auto use_files = semantic_use_files(
                semantic.records,
                header_info->path,
                *context.project_index
            );
            if (use_files.empty()) {
                ++result.items_skipped;
                continue;
            }
            const auto content = render_forward_header(header_info->path, semantic.records);
            if (content.empty()) {
                ++result.items_skipped;
                continue;
            }

            const fs::path resolved_header = context.project_index->resolve(header_info->path);
            const fs::path companion = resolved_header.parent_path() / companion_name(resolved_header);
            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("split-ast", resolved_header);
            suggestion.type = SuggestionType::HeaderSplit;
            suggestion.priority = Priority::High;
            suggestion.confidence = 1.0;
            suggestion.title = "Create AST-proven forward header for " +
                resolved_header.filename().string();
            suggestion.description =
                "Create a companion forward-declaration header from Clang AST declarations "
                "and replace only includer edges whose uses are incomplete-type-safe.";
            suggestion.rationale =
                "The split is limited to declarations and uses proven by exact compilation "
                "commands. Filename patterns and placeholder architectural groups are not used.";
            suggestion.estimated_savings = Duration::zero();
            suggestion.target_file.path = companion;
            suggestion.target_file.action = FileAction::Create;
            suggestion.target_file.note = "Create AST-derived forward-declaration companion header";
            suggestion.is_safe = true;
            suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
            suggestion.application_summary = "Apply only after compile-command validation";
            suggestion.implementation_steps = {
                "Create the AST-derived companion header",
                "Replace proven incomplete-type-safe full includes",
                "Validate every affected translation unit and rebuild affected targets",
                "Measure actual savings with a fresh trace"
            };
            suggestion.caveats = {
                "Savings are intentionally unestimated until a post-edit trace exists",
                "Macros, aliases, templates, dependent contexts, and complete-type uses are rejected",
                "Only the AST-proven forward-declaration split is automated"
            };

            TextEdit create;
            create.file = companion;
            create.start_line = 0;
            create.start_col = 0;
            create.end_line = 0;
            create.end_col = 0;
            create.new_text = content;
            suggestion.edits.push_back(std::move(create));
            suggestion.secondary_files.push_back({
                companion,
                0,
                0,
                0,
                0,
                FileAction::Create,
                "AST-derived companion header"
            });

            std::unordered_set<std::string> edited;
            for (const auto& file : use_files) {
                const auto include = find_include_for_header(file, resolved_header.filename().string());
                if (!include.has_value()) {
                    continue;
                }
                TextEdit replacement;
                replacement.file = file;
                replacement.start_line = include->line;
                replacement.start_col = include->col_start;
                replacement.end_line = include->line;
                replacement.end_col = include->col_end;
                replacement.new_text = "#include \"" + companion.filename().string() + "\"";
                suggestion.edits.push_back(std::move(replacement));
                if (edited.insert(file.generic_string()).second) {
                    suggestion.secondary_files.push_back({
                        file,
                        0,
                        0,
                        0,
                        0,
                        FileAction::Modify,
                        "AST-proven incomplete-type-safe include replacement"
                    });
                }
            }
            if (suggestion.edits.size() == 1) {
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

    void register_header_split_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<HeaderSplitSuggester>()
        );
    }
}  // namespace bha::suggestions
