#include "bha/suggestions/unity_build_suggester.hpp"

#include "bha/utils/cmake_classification_utils.hpp"
#include "bha/utils/cmake_parse_utils.hpp"
#include "bha/utils/cmake_target_parse_utils.hpp"
#include "bha/utils/path_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#endif

namespace bha::suggestions {
    namespace {
        struct CMakeCommand {
            std::string name;
            std::vector<std::string> arguments;
            std::size_t start_line = 0;
            std::size_t end_line = 0;
        };

        struct CMakeTarget {
            fs::path cmake_file;
            std::string name;
            std::vector<fs::path> sources;
            std::size_t end_line = 0;
        };

        struct CompileEvidence {
            fs::path source;
            CompilationUnit command;
            Duration compile_time = Duration::zero();
        };

        struct TargetState {
            bool unity_enabled = false;
            bool unity_state_unknown = false;
        };

        std::string lowercase(std::string_view value) {
            return utils::to_lower_ascii(value);
        }

        std::string path_key(const fs::path& path) {
            return path.lexically_normal().generic_string();
        }

        bool is_cxx_source(const fs::path& path) {
            const std::string extension = lowercase(path.extension().string());
            return extension == ".cc" || extension == ".cpp" || extension == ".cxx" ||
                   extension == ".c++";
        }

        std::string remove_cmake_comment(std::string_view line) {
            bool in_quote = false;
            char quote = '\0';
            for (std::size_t index = 0; index < line.size(); ++index) {
                const char character = line[index];
                if (in_quote) {
                    if (character == quote) {
                        in_quote = false;
                    }
                    continue;
                }
                if (character == '"' || character == '\'') {
                    in_quote = true;
                    quote = character;
                    continue;
                }
                if (character == '#') {
                    return std::string(line.substr(0, index));
                }
            }
            return std::string(line);
        }

        std::vector<CMakeCommand> parse_cmake_commands(const std::string& content) {
            std::vector<CMakeCommand> commands;
            std::istringstream input(content);
            std::string line;
            std::string pending;
            std::size_t pending_line = 0;
            std::size_t line_number = 0;
            int parenthesis_depth = 0;
            bool collecting = false;

            while (std::getline(input, line)) {
                line = remove_cmake_comment(line);
                const auto first = line.find_first_not_of(" \t\r\n");
                const std::string trimmed = first == std::string::npos
                    ? std::string{}
                    : line.substr(first);
                if (trimmed.empty()) {
                    ++line_number;
                    continue;
                }

                if (!collecting) {
                    if (!utils::parse_cmake_command_start(trimmed).has_value()) {
                        ++line_number;
                        continue;
                    }
                    pending = trimmed;
                    pending_line = line_number;
                    parenthesis_depth = 0;
                    collecting = true;
                } else {
                    pending += " ";
                    pending += trimmed;
                }

                parenthesis_depth += utils::count_paren_delta_outside_quotes(trimmed);
                if (parenthesis_depth <= 0) {
                    const auto start = utils::parse_cmake_command_start(pending);
                    if (start.has_value()) {
                        const std::size_t close = pending.rfind(')');
                        if (close != std::string::npos && close > start->open_pos) {
                            CMakeCommand command;
                            command.name = lowercase(pending.substr(0, start->open_pos));
                            command.arguments = utils::tokenize_cmake_args(
                                std::string_view(pending).substr(
                                    start->open_pos + 1,
                                    close - start->open_pos - 1
                                )
                            );
                            command.start_line = pending_line;
                            command.end_line = line_number;
                            commands.push_back(std::move(command));
                        }
                    }
                    pending.clear();
                    collecting = false;
                }
                ++line_number;
            }
            return commands;
        }

        fs::path resolve_source_token(
            const std::string& token,
            const fs::path& cmake_file,
            const fs::path& project_root
        ) {
            const fs::path value(token);
            if (value.is_absolute()) {
                return value.lexically_normal();
            }
            const fs::path relative_to_cmake = (cmake_file.parent_path() / value).lexically_normal();
            if (fs::is_regular_file(relative_to_cmake)) {
                return relative_to_cmake;
            }
            return (project_root / value).lexically_normal();
        }

        void merge_target(
            std::vector<CMakeTarget>& targets,
            CMakeTarget target
        ) {
            const auto existing = std::ranges::find_if(
                targets,
                [&](const CMakeTarget& candidate) {
                    return candidate.cmake_file == target.cmake_file &&
                           candidate.name == target.name;
                }
            );
            if (existing == targets.end()) {
                targets.push_back(std::move(target));
                return;
            }
            existing->end_line = std::max(existing->end_line, target.end_line);
            existing->sources.insert(
                existing->sources.end(),
                target.sources.begin(),
                target.sources.end()
            );
            std::ranges::sort(existing->sources, {}, path_key);
            existing->sources.erase(
                std::unique(existing->sources.begin(), existing->sources.end()),
                existing->sources.end()
            );
        }

        std::vector<CMakeTarget> parse_cmake_targets(
            const fs::path& cmake_file,
            const fs::path& project_root,
            const std::string& content
        ) {
            std::vector<CMakeTarget> targets;
            for (const auto& command : parse_cmake_commands(content)) {
                if (command.name != "add_library" && command.name != "add_executable" &&
                    command.name != "target_sources") {
                    continue;
                }
                const auto target_name = utils::extract_builtin_target_name(
                    command.name,
                    command.arguments,
                    utils::CMakeTargetNameMode::Strict
                );
                if (!target_name.has_value()) {
                    continue;
                }
                const auto source_tokens = utils::extract_builtin_sources(
                    command.name,
                    command.arguments,
                    utils::CMakeSourceTokenMode::Strict
                );
                if (source_tokens.empty()) {
                    continue;
                }
                CMakeTarget target;
                target.cmake_file = cmake_file;
                target.name = *target_name;
                target.end_line = command.end_line;
                for (const auto& token : source_tokens) {
                    const fs::path source = resolve_source_token(token, cmake_file, project_root);
                    if (!fs::is_regular_file(source)) {
                        target.sources.clear();
                        break;
                    }
                    target.sources.push_back(source);
                }
                if (!target.sources.empty()) {
                    merge_target(targets, std::move(target));
                }
            }
            return targets;
        }

        std::vector<std::pair<fs::path, std::string>> cmake_files(
            const fs::path& project_root,
            const std::function<bool()>& should_cancel
        ) {
            std::vector<std::pair<fs::path, std::string>> files;
            std::error_code error;
            fs::recursive_directory_iterator iterator(project_root, error);
            const fs::recursive_directory_iterator end;
            for (; iterator != end && !error; ++iterator) {
                if (should_cancel && should_cancel()) {
                    break;
                }
                const fs::path path = iterator->path();
                if (iterator->is_directory(error) && utils::is_excluded_cmake_path(path)) {
                    iterator.disable_recursion_pending();
                    error.clear();
                    continue;
                }
                if (!iterator->is_regular_file(error) || path.filename() != "CMakeLists.txt") {
                    error.clear();
                    continue;
                }
                std::ifstream input(path);
                if (!input) {
                    continue;
                }
                files.emplace_back(
                    path,
                    std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>())
                );
            }
            return files;
        }

        bool cmake_truth_value(std::string_view value, bool& known) {
            const std::string normalized = lowercase(value);
            if (normalized == "on" || normalized == "true" || normalized == "yes" ||
                normalized == "1") {
                known = true;
                return true;
            }
            if (normalized == "off" || normalized == "false" || normalized == "no" ||
                normalized == "0") {
                known = true;
                return false;
            }
            known = false;
            return false;
        }

        void record_unity_state(
            const std::vector<CMakeCommand>& commands,
            std::string_view target_name,
            bool& global_unity,
            TargetState& target_state
        ) {
            for (const auto& command : commands) {
                if (command.name == "set" && command.arguments.size() >= 2 &&
                    lowercase(command.arguments[0]) == "cmake_unity_build") {
                    bool known = false;
                    const bool enabled = cmake_truth_value(command.arguments[1], known);
                    if (!known || enabled) {
                        global_unity = global_unity || enabled || !known;
                    }
                    continue;
                }

                if (command.name == "set_property" && command.arguments.size() >= 5 &&
                    lowercase(command.arguments[0]) == "target" &&
                    command.arguments[1] == target_name &&
                    lowercase(command.arguments[2]) == "property" &&
                    lowercase(command.arguments[3]) == "unity_build") {
                    bool known = false;
                    const bool enabled = cmake_truth_value(command.arguments[4], known);
                    target_state.unity_enabled = target_state.unity_enabled || enabled;
                    target_state.unity_state_unknown = target_state.unity_state_unknown || !known;
                    continue;
                }

                if (command.name != "set_target_properties") {
                    continue;
                }
                const auto properties = std::ranges::find_if(
                    command.arguments,
                    [](const std::string& argument) {
                        return lowercase(argument) == "properties";
                    }
                );
                if (properties == command.arguments.end()) {
                    continue;
                }
                const auto target = std::ranges::find(
                    command.arguments.begin(), properties, std::string(target_name)
                );
                if (target == properties) {
                    continue;
                }
                const auto property_index = static_cast<std::size_t>(
                    std::distance(command.arguments.begin(), properties)
                );
                for (std::size_t index = property_index + 1; index + 1 < command.arguments.size(); index += 2) {
                    if (lowercase(command.arguments[index]) != "unity_build") {
                        continue;
                    }
                    bool known = false;
                    const bool enabled = cmake_truth_value(command.arguments[index + 1], known);
                    target_state.unity_enabled = target_state.unity_enabled || enabled;
                    target_state.unity_state_unknown = target_state.unity_state_unknown || !known;
                }
            }
        }

        std::string compiler_identity(const CompilationUnit& command) {
            if (command.command_line.empty()) {
                return {};
            }
            return lowercase(fs::path(command.command_line.front()).filename().string());
        }

        bool is_output_option(std::string_view argument) {
            return argument == "-o" || argument == "-MF" || argument == "-MT" ||
                   argument == "-MQ" || argument == "/Fo" || argument == "/Fd" ||
                   argument == "/Fp" || argument == "/Fa" || argument == "/Fe";
        }

        bool is_attached_output_option(std::string_view argument) {
            return argument.starts_with("-o") || argument.starts_with("-MF") ||
                   argument.starts_with("-MT") || argument.starts_with("-MQ") ||
                   argument.starts_with("/Fo") || argument.starts_with("/Fd") ||
                   argument.starts_with("/Fp") || argument.starts_with("/Fa") ||
                   argument.starts_with("/Fe");
        }

        bool is_pch_option(std::string_view argument) {
            return argument == "-include-pch" || argument == "-Winvalid-pch" ||
                   argument == "/Yc" || argument == "/Yu" || argument.starts_with("/Yc") ||
                   argument.starts_with("/Yu");
        }

        std::optional<std::vector<std::string>> syntax_arguments(
            const CompilationUnit& command,
            const fs::path& source
        ) {
            if (command.command_line.size() < 2) {
                return std::nullopt;
            }
            std::vector<std::string> arguments;
            arguments.reserve(command.command_line.size());
            for (std::size_t index = 1; index < command.command_line.size(); ++index) {
                const std::string& argument = command.command_line[index];
                if (is_pch_option(argument)) {
                    return std::nullopt;
                }
                if (path_key(fs::path(argument)) == path_key(source)) {
                    continue;
                }
                if (is_output_option(argument)) {
                    ++index;
                    continue;
                }
                if (is_attached_output_option(argument) || argument == "-MD" ||
                    argument == "-MMD" || argument == "-MP" || argument == "-fsyntax-only") {
                    continue;
                }
                arguments.push_back(argument);
            }
            return arguments;
        }

        std::string escape_include_path(const fs::path& path) {
            std::string escaped = path.generic_string();
            std::string result;
            result.reserve(escaped.size() + 4);
            for (const char character : escaped) {
                if (character == '\\' || character == '"') {
                    result.push_back('\\');
                }
                result.push_back(character);
            }
            return result;
        }

#if BHA_HAVE_CLANG_TOOLING
        bool validate_unity_translation_unit(
            const std::vector<CompileEvidence>& evidence,
            std::string& diagnostic
        ) {
            if (evidence.empty()) {
                diagnostic = "No compile evidence was available for the unity group";
                return false;
            }
            const auto arguments = syntax_arguments(evidence.front().command, evidence.front().source);
            if (!arguments.has_value()) {
                diagnostic = "The compile command uses a precompiled-header mode that cannot be replayed safely";
                return false;
            }

            std::string code;
            for (const auto& item : evidence) {
                code += "#include \"";
                code += escape_include_path(item.source);
                code += "\"\n";
            }
            const fs::path virtual_file = evidence.front().command.working_directory /
                "__bha_unity_validation__.cpp";
            const std::string compiler = compiler_identity(evidence.front().command);
            const bool clang_cl = compiler.find("clang-cl") != std::string::npos ||
                compiler == "cl.exe" || compiler == "cl";
            const bool valid = clang::tooling::runToolOnCodeWithArgs(
                std::make_unique<clang::SyntaxOnlyAction>(),
                code,
                *arguments,
                virtual_file.string(),
                clang_cl ? "clang-cl" : "clang++"
            );
            if (!valid) {
                diagnostic = "Clang rejected the proposed unity translation unit";
            }
            return valid;
        }
#else
        bool validate_unity_translation_unit(
            const std::vector<CompileEvidence>&,
            std::string& diagnostic
        ) {
            diagnostic = "Unity suggestions require Clang LibTooling for merged-TU validation";
            return false;
        }
#endif

        std::optional<std::vector<std::string>> normalized_environment(
            const CompilationUnit& command,
            const fs::path& source
        ) {
            const auto arguments = syntax_arguments(command, source);
            if (!arguments.has_value()) {
                return std::nullopt;
            }
            std::vector<std::string> normalized;
            normalized.reserve(arguments->size() + 1);
            normalized.push_back(compiler_identity(command));
            normalized.insert(normalized.end(), arguments->begin(), arguments->end());
            return normalized;
        }

        TargetState target_unity_state(
            const fs::path& project_root,
            std::string_view target_name,
            const std::function<bool()>& should_cancel,
            bool& global_unity
        ) {
            TargetState state;
            for (const auto& [cmake_file, content] : cmake_files(project_root, should_cancel)) {
                static_cast<void>(cmake_file);
                record_unity_state(parse_cmake_commands(content), target_name, global_unity, state);
            }
            return state;
        }

    }  // namespace

    Result<SuggestionResult, Error> UnityBuildSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        const auto start = std::chrono::steady_clock::now();

        if (!context.project_index ||
            context.project_index->compile_commands_status() != CompilationDatabaseStatus::Loaded) {
            result.diagnostics.push_back({
                "unity.compile_commands.required",
                "Unity suggestions require a valid compile_commands.json for every source in the target"
            });
            result.generation_time = std::chrono::duration_cast<Duration>(
                std::chrono::steady_clock::now() - start
            );
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        const fs::path project_root = context.project_root.empty()
            ? context.project_index->project_root()
            : context.project_index->resolve(context.project_root);
        if (project_root.empty() || !fs::is_directory(project_root)) {
            result.diagnostics.push_back({
                "unity.project_root.required",
                "Unity suggestions require a project root containing CMakeLists.txt"
            });
            result.generation_time = std::chrono::duration_cast<Duration>(
                std::chrono::steady_clock::now() - start
            );
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        std::unordered_map<std::string, analyzers::FileAnalysisResult> analyzed_files;
        for (const auto& file : context.analysis.files) {
            const fs::path source = context.project_index->resolve(file.file);
            if (is_cxx_source(source) && context.should_analyze(source)) {
                analyzed_files.emplace(path_key(source), file);
            }
        }

        std::unordered_set<std::string> seen_targets;
        std::size_t analyzed_targets = 0;
        std::size_t skipped_targets = 0;
        bool global_unity_cache = false;
        bool global_unity_loaded = false;

        for (const auto& [cmake_file, content] : cmake_files(
                 project_root,
                 [&]() { return context.is_cancelled(); }
             )) {
            if (context.is_cancelled()) {
                break;
            }
            for (auto target : parse_cmake_targets(cmake_file, project_root, content)) {
                ++analyzed_targets;
                if (!seen_targets.insert(target.name).second) {
                    ++skipped_targets;
                    continue;
                }
                // A unity build is only meaningful for a target with at least
                // two independently compiled translation units.
                if (target.sources.size() < 2) {
                    ++skipped_targets;
                    continue;
                }

                std::vector<CompileEvidence> evidence;
                evidence.reserve(target.sources.size());
                std::optional<std::vector<std::string>> environment;
                bool compatible = true;
                for (const auto& source : target.sources) {
                    const auto file = analyzed_files.find(path_key(source));
                    const auto command = context.project_index->compile_command_for(source);
                    if (file == analyzed_files.end() || !command.has_value() ||
                        file->second.compile_time <= Duration::zero()) {
                        compatible = false;
                        break;
                    }
                    const auto current_environment = normalized_environment(*command, source);
                    if (!current_environment.has_value() ||
                        (environment.has_value() && *environment != *current_environment)) {
                        compatible = false;
                        break;
                    }
                    if (!environment.has_value()) {
                        environment = current_environment;
                    }
                    evidence.push_back({source, *command, file->second.compile_time});
                }
                if (!compatible) {
                    ++skipped_targets;
                    continue;
                }

                if (!global_unity_loaded) {
                    bool ignored_global_state = false;
                    const auto state = target_unity_state(
                        project_root,
                        "__bha_unity_global_probe__",
                        [&]() { return context.is_cancelled(); },
                        ignored_global_state
                    );
                    static_cast<void>(state);
                    global_unity_cache = ignored_global_state;
                    global_unity_loaded = true;
                }
                bool global_unity = global_unity_cache;
                TargetState state = target_unity_state(
                    project_root,
                    target.name,
                    [&]() { return context.is_cancelled(); },
                    global_unity
                );
                if (global_unity || state.unity_enabled || state.unity_state_unknown) {
                    ++skipped_targets;
                    continue;
                }

                std::string validation_diagnostic;
                if (!validate_unity_translation_unit(evidence, validation_diagnostic)) {
                    ++skipped_targets;
                    continue;
                }

                Suggestion suggestion;
                suggestion.id = generate_suggestion_id("unity", analyzed_targets, target.name);
                suggestion.type = SuggestionType::UnityBuild;
                suggestion.priority = Priority::Medium;
                suggestion.confidence = 1.0;
                suggestion.title = "Enable CMake unity build for target " + target.name;
                suggestion.description =
                    "Enable CMake UNITY_BUILD for the exact target source set after Clang "
                    "accepted the proposed include-ordered unity translation unit.";
                suggestion.rationale =
                    "The target source list was resolved exactly from CMake, every source "
                    "has a compile-database command with the same environment, and the "
                    "merged translation unit passed Clang syntax validation.";
                suggestion.estimated_savings = Duration::zero();
                suggestion.target_file.path = target.cmake_file;
                suggestion.target_file.action = FileAction::Modify;
                suggestion.target_file.note = "Enable UNITY_BUILD for target " + target.name;
                suggestion.secondary_files.reserve(target.sources.size());
                for (const auto& source : target.sources) {
                    FileTarget file_target;
                    file_target.path = source;
                    file_target.action = FileAction::Modify;
                    file_target.note = "Validated source included by proposed unity translation unit";
                    suggestion.secondary_files.push_back(std::move(file_target));
                }

                const std::size_t insert_line = target.end_line + 1;
                TextEdit edit;
                edit.file = target.cmake_file;
                edit.start_line = insert_line;
                edit.start_col = 0;
                edit.end_line = insert_line;
                edit.end_col = 0;
                edit.new_text = "\nif(TARGET " + target.name + ")\n"
                    "  set_property(TARGET " + target.name + " PROPERTY UNITY_BUILD ON)\n";
                edit.new_text += "endif()\n";
                suggestion.edits.push_back(edit);
                suggestion.after_code.file = target.cmake_file.filename().string();
                suggestion.after_code.code = edit.new_text;
                suggestion.implementation_steps = {
                    "Apply the target-scoped CMake UNITY_BUILD edit",
                    "Configure and build the target with the project compiler",
                    "Run the target tests and compare a fresh build trace"
                };
                suggestion.caveats = {
                    "Savings are intentionally unestimated until a post-edit trace is available",
                    "CMake unity builds combine sources in target order and can expose include-order or ODR issues",
                    "CMake may exclude sources marked SKIP_UNITY_BUILD_INCLUSION"
                };
                suggestion.verification =
                    "Clang merged-TU syntax validation passed; configure, build, and test the target "
                    "with the project compiler before accepting the edit";
                suggestion.is_safe = true;
                suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
                suggestion.impact.total_files_affected = target.sources.size();
                suggestion.impact.cumulative_savings = Duration::zero();
                result.suggestions.push_back(std::move(suggestion));
            }
        }

        result.items_analyzed = analyzed_targets;
        result.items_skipped = skipped_targets;
        result.generation_time = std::chrono::duration_cast<Duration>(
            std::chrono::steady_clock::now() - start
        );
        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_unity_build_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<UnityBuildSuggester>()
        );
    }
}  // namespace bha::suggestions
