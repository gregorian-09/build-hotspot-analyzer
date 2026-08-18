#include "bha/suggestions/pch_suggester.hpp"

#include "bha/suggestions/suggester.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace bha::suggestions {
    namespace {

        fs::path resolved(ProjectIndex& project_index, const fs::path& path) {
            return project_index.resolve(path).lexically_normal();
        }

        std::string command_environment_key(const CompilationUnit& command) {
            std::ostringstream key;
            for (std::size_t index = 1; index < command.command_line.size(); ++index) {
                const auto& argument = command.command_line[index];
                if (argument == "-c" || argument == "/c") {
                    continue;
                }
                if (argument == "-o" || argument == "/Fo" || argument == "/Fe") {
                    ++index;
                    continue;
                }
                if (argument.starts_with("-o") || argument.starts_with("/Fo")) {
                    continue;
                }
                const fs::path path_argument(argument);
                if (path_argument == command.source_file ||
                    path_argument.filename() == command.source_file.filename()) {
                    continue;
                }
                key << argument << '\0';
            }
            return key.str();
        }

        bool has_matching_compile_environment(
            ProjectIndex& project_index,
            const analyzers::DependencyAnalysisResult::HeaderInfo& header
        ) {
            if (header.included_by.size() < 2) {
                return false;
            }

            std::unordered_set<std::string> includers;
            for (const auto& file : header.included_by) {
                includers.insert(resolved(project_index, file).generic_string());
            }

            std::optional<std::string> environment;
            std::size_t matched = 0;
            for (const auto& command : project_index.compile_commands()) {
                if (!includers.contains(resolved(project_index, command.source_file).generic_string())) {
                    continue;
                }
                const auto current = command_environment_key(command);
                if (!environment.has_value()) {
                    environment = current;
                } else if (*environment != current) {
                    return false;
                }
                ++matched;
            }
            return matched == includers.size();
        }

        Suggestion make_advisory(
            const analyzers::DependencyAnalysisResult::HeaderInfo& header
        ) {
            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("pch-evidence", header.path);
            suggestion.type = SuggestionType::PCHOptimization;
            suggestion.priority = Priority::Medium;
            suggestion.confidence = 1.0;
            suggestion.title = "Evaluate a precompiled header for " + header.path.filename().string();

            std::ostringstream description;
            description << "Clang/GCC/MSVC-compatible compile-command evidence shows that "
                        << header.path.generic_string() << " is included by "
                        << header.included_by.size() << " translation units. The observed "
                        << "includers share one compiler environment."
                        << "\n\nA PCH may reduce repeated preprocessing, but BHA will not claim a saving or edit a"
                        << " build file until a build-system-specific configuration and fresh trace are available.";
            suggestion.description = description.str();
            suggestion.rationale =
                "The candidate is derived only from the dependency graph and exact compilation "
                "database environments. No modification-frequency, filename, build-file, or "
                "fixed-percentage heuristic is used.";
            suggestion.estimated_savings = Duration::zero();
            suggestion.target_file.path = header.path;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Candidate header identified from repeated exact compilation environments";
            suggestion.impact.total_files_affected = header.included_by.size();
            suggestion.caveats = {
                "PCH compatibility must be verified for every compiler, language mode, macro set, and target",
                "Only one PCH can be used per compilation and it must be generated with a compatible command",
                "No automatic build-system edit is emitted",
                "Savings remain unknown until a post-configuration build trace is collected"
            };
            suggestion.implementation_steps = {
                "Choose a build-system-native PCH configuration for the affected target",
                "Generate and consume the PCH with the exact compatible compile environments",
                "Run syntax and full rebuild validation",
                "Compare a fresh trace against the baseline"
            };
            suggestion.verification =
                "Validate the generated PCH with each affected compile command and measure a fresh trace";
            suggestion.is_safe = false;
            suggestion.application_mode = SuggestionApplicationMode::Advisory;
            return suggestion;
        }

    }  // namespace

    Result<SuggestionResult, Error> PCHSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        const auto started = std::chrono::steady_clock::now();

        if (!context.project_index ||
            context.project_index->compile_commands_status() != CompilationDatabaseStatus::Loaded) {
            result.diagnostics.push_back({
                "pch.evidence.compile_database_required",
                "PCH candidates require a valid compilation database"
            });
            result.generation_time = std::chrono::steady_clock::now() - started;
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        for (const auto& header : context.analysis.dependencies.headers) {
            if (context.is_cancelled()) {
                break;
            }
            ++result.items_analyzed;
            if (header.path.empty() || !is_header_file_path(header.path) ||
                header.inclusion_count < 2 || header.included_by.size() < 2 ||
                header.total_parse_time <= Duration::zero() ||
                !fs::exists(context.project_index->resolve(header.path)) ||
                !has_matching_compile_environment(*context.project_index, header)) {
                ++result.items_skipped;
                continue;
            }

            result.suggestions.push_back(make_advisory(header));
        }

        std::ranges::sort(result.suggestions, [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
        result.generation_time = std::chrono::steady_clock::now() - started;
        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_pch_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<PCHSuggester>()
        );
    }

}  // namespace bha::suggestions
