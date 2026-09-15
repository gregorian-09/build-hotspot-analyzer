#include "bha/suggestions/include_suggester.hpp"
#include "bha/suggestions/forward_decl_semantic_index.hpp"

#if BHA_HAVE_CLANG_TOOLING
#include <clang/Tooling/DiagnosticsYaml.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/YAMLTraits.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace bha::suggestions {
    namespace {
        struct IncludeDiagnostic {
            fs::path file;
            std::size_t line = 0;
            std::string header_name;
        };

        std::string trim(const std::string& value) {
            const auto first = value.find_first_not_of(" \t\r");
            if (first == std::string::npos) {
                return {};
            }
            const auto last = value.find_last_not_of(" \t\r");
            return value.substr(first, last - first + 1);
        }

        struct ParsedIncludeDirective {
            IncludeDirective directive;
            std::size_t byte_offset = 0;
        };

        std::vector<ParsedIncludeDirective> parse_include_directives(const fs::path& file) {
            std::vector<ParsedIncludeDirective> directives;
            std::ifstream input(file, std::ios::binary);
            if (!input) {
                return directives;
            }

            std::string line;
            std::size_t line_number = 0;
            std::size_t byte_offset = 0;
            while (std::getline(input, line)) {
                const std::string cleaned = trim(line);
                if (!cleaned.starts_with('#')) {
                    ++line_number;
                    byte_offset += line.size() + 1;
                    continue;
                }

                std::size_t cursor = 1;
                while (cursor < cleaned.size() && std::isspace(static_cast<unsigned char>(cleaned[cursor]))) {
                    ++cursor;
                }
                constexpr std::string_view include = "include";
                if (cleaned.compare(cursor, include.size(), include) != 0) {
                    ++line_number;
                    byte_offset += line.size() + 1;
                    continue;
                }
                cursor += include.size();
                while (cursor < cleaned.size() && std::isspace(static_cast<unsigned char>(cleaned[cursor]))) {
                    ++cursor;
                }
                if (cursor >= cleaned.size() || (cleaned[cursor] != '<' && cleaned[cursor] != '"')) {
                    ++line_number;
                    byte_offset += line.size() + 1;
                    continue;
                }

                const char opener = cleaned[cursor++];
                const char closer = opener == '<' ? '>' : '"';
                const auto end = cleaned.find(closer, cursor);
                if (end == std::string::npos || end == cursor) {
                    ++line_number;
                    byte_offset += line.size() + 1;
                    continue;
                }

                IncludeDirective directive;
                directive.line = line_number;
                directive.col_start = 0;
                directive.col_end = line.size();
                directive.header_name = cleaned.substr(cursor, end - cursor);
                directive.is_system = opener == '<';
                directives.push_back({std::move(directive), byte_offset});
                ++line_number;
                byte_offset += line.size() + 1;
            }
            return directives;
        }

        std::string diagnostic_path_key(std::string value) {
            std::ranges::replace(value, '\\', '/');
#ifdef _WIN32
            std::ranges::transform(
                value,
                value.begin(),
                [](const unsigned char character) { return static_cast<char>(std::tolower(character)); }
            );
#endif
            return value;
        }

        bool paths_refer_to_same_file(
            const fs::path& left,
            const fs::path& right,
            const fs::path& working_directory,
            const fs::path& build_directory
        ) {
            const auto key = [](const fs::path& path) {
                std::error_code error;
                const auto canonical = fs::weakly_canonical(path, error);
                return diagnostic_path_key((error ? path : canonical).generic_string());
            };

            const auto left_key = key(left);
            const auto right_key = key(right);
            if (left_key == right_key) {
                return true;
            }
            if (!right.is_relative()) {
                return false;
            }
            return left_key == key(working_directory / right) ||
                   left_key == key(build_directory / right);
        }

        std::vector<IncludeDiagnostic> run_include_cleaner(
            const fs::path& build_dir,
            const CompilationUnit& command
        ) {
            std::vector<IncludeDiagnostic> diagnostics;
#if !BHA_HAVE_CLANG_TOOLING
            (void)build_dir;
            (void)command;
            return diagnostics;
#else
            const fs::path source_file = command.source_file.lexically_normal();
            if (!fs::exists(source_file)) {
                return diagnostics;
            }

            const char* configured_binary = std::getenv("BHA_CLANG_TIDY");
            const std::string binary = configured_binary != nullptr && *configured_binary != '\0'
                ? configured_binary
                : "clang-tidy";

            std::error_code temp_error;
            const fs::path fixes_directory = fs::temp_directory_path(temp_error);
            if (temp_error) {
                return diagnostics;
            }
            const auto unique_id = std::chrono::steady_clock::now().time_since_epoch().count();
            const fs::path fixes_file = fixes_directory /
                ("bha-include-cleaner-" + std::to_string(unique_id) + ".yaml");
            const fs::path output_file = fixes_directory /
                ("bha-include-cleaner-" + std::to_string(unique_id) + ".out");
            const fs::path error_file = fixes_directory /
                ("bha-include-cleaner-" + std::to_string(unique_id) + ".err");
            const auto remove_temporary_files = [&] {
                std::error_code cleanup_error;
                fs::remove(fixes_file, cleanup_error);
                fs::remove(output_file, cleanup_error);
                fs::remove(error_file, cleanup_error);
            };
            remove_temporary_files();

            std::vector<std::string> argument_storage;
            argument_storage.reserve(12);
            std::string program;
            const auto add_argument = [&](std::string argument) {
                argument_storage.push_back(std::move(argument));
            };

#ifdef _WIN32
            std::string extension = fs::path(binary).extension().string();
            std::ranges::transform(
                extension,
                extension.begin(),
                [](const unsigned char character) { return static_cast<char>(std::tolower(character)); }
            );
            if (extension == ".cmd" || extension == ".bat") {
                const auto command_shell = llvm::sys::findProgramByName("cmd.exe");
                if (!command_shell) {
                    remove_temporary_files();
                    return diagnostics;
                }
                program = *command_shell;
                add_argument(program);
                add_argument("/d");
                add_argument("/q");
                add_argument("/c");
                add_argument("call");
                add_argument(binary);
            } else {
                const auto tidy_program = llvm::sys::findProgramByName(binary);
                if (!tidy_program) {
                    remove_temporary_files();
                    return diagnostics;
                }
                program = *tidy_program;
                add_argument(program);
            }
#else
            const auto tidy_program = llvm::sys::findProgramByName(binary);
            if (!tidy_program) {
                remove_temporary_files();
                return diagnostics;
            }
            program = *tidy_program;
            add_argument(program);
#endif

            add_argument("-checks=-*,misc-include-cleaner");
#ifdef _WIN32
            add_argument("--extra-arg-before=--driver-mode=cl");
#endif
            add_argument("-p");
            add_argument(build_dir.string());
            add_argument("--export-fixes=" + fixes_file.string());
            add_argument("--quiet");
            add_argument(source_file.string());

            std::vector<llvm::StringRef> arguments;
            arguments.reserve(argument_storage.size());
            for (const auto& argument : argument_storage) {
                arguments.emplace_back(argument);
            }
            const std::string output_path = output_file.string();
            const std::string error_path = error_file.string();
            const std::array<std::optional<llvm::StringRef>, 3> redirects = {
                std::nullopt,
                llvm::StringRef(output_path),
                llvm::StringRef(error_path)
            };
            std::string execution_error;
            bool execution_failed = false;
            const int exit_code = llvm::sys::ExecuteAndWait(
                program,
                arguments,
                std::nullopt,
                redirects,
                0,
                0,
                &execution_error,
                &execution_failed
            );
            if (exit_code != 0 || execution_failed) {
                remove_temporary_files();
                return diagnostics;
            }

            std::ifstream fixes_input(fixes_file, std::ios::binary);
            std::string fixes(
                (std::istreambuf_iterator<char>(fixes_input)),
                std::istreambuf_iterator<char>()
            );
            const bool fixes_read = static_cast<bool>(fixes_input);
            fixes_input.close();
            if (!fixes_read || fixes.empty()) {
                remove_temporary_files();
                return diagnostics;
            }

            clang::tooling::TranslationUnitDiagnostics exported;
            llvm::yaml::Input yaml_input(fixes);
            yaml_input >> exported;
            if (yaml_input.error()) {
                remove_temporary_files();
                return diagnostics;
            }

            const auto directives = parse_include_directives(source_file);
            std::unordered_set<std::size_t> seen_offsets;
            for (const auto& diagnostic : exported.Diagnostics) {
                if (diagnostic.DiagnosticName != "misc-include-cleaner") {
                    continue;
                }
                for (const auto& [file_path, replacements] : diagnostic.Message.Fix) {
                    (void)file_path;
                    for (const auto& replacement : replacements) {
                        if (replacement.getLength() == 0 ||
                            !replacement.getReplacementText().empty() ||
                            !paths_refer_to_same_file(
                                source_file,
                                fs::path(replacement.getFilePath().str()),
                                command.working_directory,
                                build_dir
                            ) ||
                            !seen_offsets.insert(replacement.getOffset()).second) {
                            continue;
                        }

                        const auto directive = std::find_if(
                            directives.begin(),
                            directives.end(),
                            [&](const ParsedIncludeDirective& candidate) {
                                return candidate.byte_offset == replacement.getOffset();
                            }
                        );
                        if (directive == directives.end()) {
                            continue;
                        }
                        diagnostics.push_back({
                            source_file,
                            directive->directive.line,
                            directive->directive.header_name
                        });
                    }
                }
            }
            remove_temporary_files();
            return diagnostics;
#endif
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
