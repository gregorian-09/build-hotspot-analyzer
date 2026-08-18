#include "bha/suggestions/include_suggester.hpp"
#include "bha/suggestions/forward_decl_semantic_index.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace bha::suggestions {
    namespace {
        struct IncludeDiagnostic {
            fs::path file;
            std::size_t line = 0;
            std::string header_name;
        };

        std::string shell_quote(const std::string& input) {
#ifdef _WIN32
            std::string quoted = "\"";
            for (const char character : input) {
                if (character == '"') {
                    quoted += "\\\"";
                } else {
                    quoted += character;
                }
            }
            quoted += '"';
            return quoted;
#else
            std::string quoted = "'";
            for (const char character : input) {
                if (character == '\'') {
                    quoted += "'\\''";
                } else {
                    quoted += character;
                }
            }
            quoted += '\'';
            return quoted;
#endif
        }

        std::string trim(std::string value) {
            const auto first = value.find_first_not_of(" \t\r");
            if (first == std::string::npos) {
                return {};
            }
            const auto last = value.find_last_not_of(" \t\r");
            return value.substr(first, last - first + 1);
        }

        std::vector<IncludeDirective> parse_include_directives(const fs::path& file) {
            std::vector<IncludeDirective> directives;
            std::ifstream input(file);
            if (!input) {
                return directives;
            }

            std::string line;
            std::size_t line_number = 0;
            while (std::getline(input, line)) {
                const std::string cleaned = trim(line);
                if (!cleaned.starts_with('#')) {
                    ++line_number;
                    continue;
                }

                std::size_t cursor = 1;
                while (cursor < cleaned.size() && std::isspace(static_cast<unsigned char>(cleaned[cursor]))) {
                    ++cursor;
                }
                constexpr std::string_view include = "include";
                if (cleaned.compare(cursor, include.size(), include) != 0) {
                    ++line_number;
                    continue;
                }
                cursor += include.size();
                while (cursor < cleaned.size() && std::isspace(static_cast<unsigned char>(cleaned[cursor]))) {
                    ++cursor;
                }
                if (cursor >= cleaned.size() || (cleaned[cursor] != '<' && cleaned[cursor] != '"')) {
                    ++line_number;
                    continue;
                }

                const char opener = cleaned[cursor++];
                const char closer = opener == '<' ? '>' : '"';
                const auto end = cleaned.find(closer, cursor);
                if (end == std::string::npos || end == cursor) {
                    ++line_number;
                    continue;
                }

                IncludeDirective directive;
                directive.line = line_number;
                directive.col_start = 0;
                directive.col_end = line.size();
                directive.header_name = cleaned.substr(cursor, end - cursor);
                directive.is_system = opener == '<';
                directives.push_back(std::move(directive));
                ++line_number;
            }
            return directives;
        }

        std::optional<std::size_t> parse_diagnostic_line(
            const std::string& output,
            const fs::path& source_file
        ) {
            const std::string prefix = source_file.string() + ":";
            if (!output.starts_with(prefix)) {
                return std::nullopt;
            }
            const auto line_end = output.find(':', prefix.size());
            if (line_end == std::string::npos || line_end == prefix.size()) {
                return std::nullopt;
            }
            try {
                const auto line = std::stoul(output.substr(prefix.size(), line_end - prefix.size()));
                return line == 0 ? std::nullopt : std::optional<std::size_t>{line - 1};
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }

        std::vector<IncludeDiagnostic> run_include_cleaner(
            const fs::path& build_dir,
            const CompilationUnit& command
        ) {
            std::vector<IncludeDiagnostic> diagnostics;
            const fs::path source_file = command.source_file.lexically_normal();
            if (!fs::exists(source_file)) {
                return diagnostics;
            }

            const char* configured_binary = std::getenv("BHA_CLANG_TIDY");
            const std::string binary = configured_binary != nullptr && *configured_binary != '\0'
                ? configured_binary
                : "clang-tidy";
            std::string tidy = shell_quote(binary);
#ifdef _WIN32
            const auto extension = fs::path(binary).extension().string();
            if (extension == ".cmd" || extension == ".bat") {
                tidy = "cmd /d /q /c call " + tidy;
            }
#endif

            const std::string command_line = tidy +
                " -checks=" + shell_quote("-*,misc-include-cleaner") +
                " -p " + shell_quote(build_dir.string()) +
                " " + shell_quote(source_file.string()) + " --quiet 2>&1";
            FILE* pipe = popen(command_line.c_str(), "r");
            if (pipe == nullptr) {
                return diagnostics;
            }

            std::string output;
            std::array<char, 4096> buffer{};
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                output += buffer.data();
            }
            pclose(pipe);

            constexpr std::string_view diagnostic_tag = "[misc-include-cleaner]";
            constexpr std::string_view header_prefix = "included header ";
            constexpr std::string_view unused_suffix = " is not used directly";
            std::istringstream lines(output);
            std::string line;
            const auto directives = parse_include_directives(source_file);
            while (std::getline(lines, line)) {
                if (line.find(diagnostic_tag) == std::string::npos ||
                    line.find(unused_suffix) == std::string::npos) {
                    continue;
                }
                const auto line_number = parse_diagnostic_line(line, source_file);
                if (!line_number.has_value()) {
                    continue;
                }
                const auto header_start = line.find(header_prefix);
                const auto header_end = line.find(unused_suffix, header_start + header_prefix.size());
                if (header_start == std::string::npos || header_end == std::string::npos) {
                    continue;
                }
                const auto directive = std::find_if(
                    directives.begin(),
                    directives.end(),
                    [&](const IncludeDirective& candidate) {
                        return candidate.line == *line_number;
                    }
                );
                if (directive == directives.end()) {
                    continue;
                }
                diagnostics.push_back({
                    source_file,
                    *line_number,
                    line.substr(header_start + header_prefix.size(), header_end - header_start - header_prefix.size())
                });
            }
            return diagnostics;
        }

        Suggestion make_removal_suggestion(const IncludeDiagnostic& diagnostic) {
            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("clang-include-cleaner", diagnostic.file, diagnostic.header_name);
            suggestion.type = SuggestionType::IncludeRemoval;
            suggestion.priority = Priority::Low;
            suggestion.confidence = 1.0;
            suggestion.title = "Remove unused include " + diagnostic.header_name;
            suggestion.description = "Clang misc-include-cleaner reported this include as unused in the active translation unit.";
            suggestion.rationale = "The edit is emitted only from a compiler-backed include-cleaner diagnostic; BHA does not infer unusedness from source text or timing thresholds.";
            suggestion.estimated_savings = Duration::zero();
            suggestion.estimated_savings_percent = 0.0;
            suggestion.is_safe = true;
            suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
            suggestion.target_file = {
                diagnostic.file,
                diagnostic.line + 1,
                diagnostic.line + 1,
                0,
                0,
                FileAction::Modify,
                "Remove include confirmed by clang misc-include-cleaner"
            };
            suggestion.impact.total_files_affected = 1;
            suggestion.impact.cumulative_savings = Duration::zero();
            suggestion.caveats = {
                "Evidence is specific to the compile command used for this translation unit",
                "Re-run the suggester for each supported configuration"
            };
            suggestion.verification = "Rebuild the affected configuration and rerun clang misc-include-cleaner";
            suggestion.implementation_steps = {
                "Apply the exact include removal reported by Clang",
                "Rebuild the affected translation unit",
                "Re-run misc-include-cleaner to confirm the diagnostic is gone"
            };

            TextEdit edit;
            edit.file = diagnostic.file;
            edit.start_line = diagnostic.line;
            edit.end_line = diagnostic.line + 1;
            edit.end_col = 0;
            suggestion.edits.push_back(std::move(edit));
            return suggestion;
        }
    }

    Result<SuggestionResult, Error> IncludeSuggester::suggest(const SuggestionContext& context) const {
        SuggestionResult result;
        const auto started = std::chrono::steady_clock::now();
        if (!context.project_index || context.project_index->compile_commands_status() != CompilationDatabaseStatus::Loaded) {
            result.generation_time = std::chrono::duration_cast<Duration>(
                std::chrono::steady_clock::now() - started
            );
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        const auto commands = context.project_index->compile_commands();
        std::unordered_set<std::string> seen_sources;
        for (const auto& command : commands) {
            if (context.is_cancelled() || !is_source_file_path(command.source_file)) {
                break;
            }
            const fs::path source = command.source_file.lexically_normal();
            if (!seen_sources.insert(source.generic_string()).second ||
                !context.should_analyze(source)) {
                continue;
            }
            const fs::path build_dir = context.options.compile_commands_path.has_value()
                ? (context.options.compile_commands_path->filename() == "compile_commands.json"
                    ? context.options.compile_commands_path->parent_path()
                    : *context.options.compile_commands_path)
                : command.working_directory;
            for (const auto& diagnostic : run_include_cleaner(build_dir, command)) {
                std::string validation_diagnostic;
                if (validate_include_removal(
                        *context.project_index,
                        command,
                        diagnostic.file,
                        diagnostic.line,
                        diagnostic.header_name,
                        validation_diagnostic
                    )) {
                    result.suggestions.push_back(make_removal_suggestion(diagnostic));
                }
            }
        }

        std::ranges::sort(result.suggestions, [](const Suggestion& left, const Suggestion& right) {
            return left.id < right.id;
        });
        result.items_analyzed = seen_sources.size();
        result.generation_time = std::chrono::duration_cast<Duration>(
            std::chrono::steady_clock::now() - started
        );
        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_include_suggester() {
        SuggesterRegistry::instance().register_suggester(std::make_unique<IncludeSuggester>());
    }
}  // namespace bha::suggestions
