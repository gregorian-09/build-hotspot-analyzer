#include "bha/suggestions/unity_build_suggester.hpp"

#include "bha/utils/cmake_parse_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#endif

#include <nlohmann/json.hpp>

namespace bha::suggestions {
    namespace {
        struct CMakeCommand {
            std::string name;
            std::vector<std::string> arguments;
            std::size_t start_line = 0;
            std::size_t end_line = 0;
        };

        struct FileApiTarget {
            fs::path cmake_file;
            std::string name;
            std::vector<fs::path> sources;
            std::size_t end_line = 0;
        };

        struct FileApiModel {
            std::vector<FileApiTarget> targets;
            std::vector<CMakeCommand> cmake_commands;
            bool global_unity = false;
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

        std::string lowercase(std::string_view value) { return utils::lowercase_ascii(value); }

        std::string path_key(const fs::path& path) { return path.lexically_normal().generic_string(); }

        bool is_cxx_source(const fs::path& path) {
            const std::string extension = lowercase(path.extension().string());
            return extension == ".cc" || extension == ".cpp" || extension == ".cxx" || extension == ".c++";
        }

        std::string strip_cmake_comments(std::string_view content) {
            std::string cleaned(content);
            bool in_quote = false;
            bool escaped = false;
            std::optional<std::size_t> bracket_equals;
            std::optional<std::size_t> bracket_comment_equals;

            for (std::size_t index = 0; index < cleaned.size(); ++index) {
                const char character = cleaned[index];
                if (bracket_comment_equals.has_value()) {
                    if (utils::detail::cmake_bracket_closes(cleaned, index, *bracket_comment_equals)) {
                        for (std::size_t offset = 0; offset <= *bracket_comment_equals + 1; ++offset) {
                            cleaned[index + offset] = ' ';
                        }
                        index += *bracket_comment_equals + 1;
                        bracket_comment_equals.reset();
                    } else if (character != '\n') {
                        cleaned[index] = ' ';
                    }
                    continue;
                }
                if (bracket_equals.has_value()) {
                    if (utils::detail::cmake_bracket_closes(cleaned, index, *bracket_equals)) {
                        index += *bracket_equals + 1;
                        bracket_equals.reset();
                    }
                    continue;
                }
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (character == '\\') {
                    escaped = true;
                    continue;
                }
                if (in_quote) {
                    if (character == '"') {
                        in_quote = false;
                    }
                    continue;
                }
                if (character == '"') {
                    in_quote = true;
                    continue;
                }
                if (const auto equals = utils::detail::cmake_bracket_equals(cleaned, index);
                    equals.has_value()) {
                    bracket_equals = *equals;
                    index += *equals + 1;
                    continue;
                }
                if (character == '#') {
                    if (const auto equals = index + 1 < cleaned.size()
                            ? utils::detail::cmake_bracket_equals(cleaned, index + 1)
                            : std::nullopt;
                        equals.has_value()) {
                        cleaned[index] = ' ';
                        const std::size_t opening_end = index + *equals + 2;
                        for (std::size_t offset = 1; offset <= *equals + 2; ++offset) {
                            cleaned[index + offset] = ' ';
                        }
                        bracket_comment_equals = *equals;
                        index = opening_end;
                        continue;
                    }
                    std::size_t comment_end = index;
                    while (comment_end < cleaned.size() && cleaned[comment_end] != '\n') {
                        cleaned[comment_end] = ' ';
                        ++comment_end;
                    }
                    index = comment_end;
                }
            }
            return cleaned;
        }

        std::vector<CMakeCommand> parse_cmake_commands(const std::string& content) {
            std::vector<CMakeCommand> commands;
            std::istringstream input(strip_cmake_comments(content));
            std::string line;
            std::string pending;
            std::size_t pending_line = 0;
            std::size_t line_number = 0;
            int parenthesis_depth = 0;
            bool collecting = false;

            while (std::getline(input, line)) {
                const auto first = line.find_first_not_of(" \t\r\n");
                const std::string trimmed = first == std::string::npos ? std::string{} : line.substr(first);
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

                parenthesis_depth = utils::count_paren_delta_outside_quotes(pending);
                if (parenthesis_depth <= 0) {
                    const auto start = utils::parse_cmake_command_start(pending);
                    if (start.has_value()) {
                        const std::size_t close = pending.rfind(')');
                        if (close != std::string::npos && close > start->open_pos) {
                            CMakeCommand command;
                            command.name = lowercase(pending.substr(0, start->open_pos));
                            command.arguments = utils::tokenize_cmake_args(
                                std::string_view(pending).substr(start->open_pos + 1, close - start->open_pos - 1));
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

        using Json = nlohmann::json;

        bool cmake_truth_value(std::string_view value, bool& known);

        bool is_path_within(const fs::path& path, const fs::path& directory) {
            const fs::path relative = path.lexically_normal().lexically_relative(directory.lexically_normal());
            if (relative.empty()) {
                return false;
            }
            const auto first = relative.begin();
            return first == relative.end() || *first != "..";
        }

        bool is_at_least_as_new(const fs::path& candidate, const fs::path& reference) {
            std::error_code error;
            const auto candidate_time = fs::last_write_time(candidate, error);
            if (error) {
                return false;
            }
            const auto reference_time = fs::last_write_time(reference, error);
            return !error && candidate_time >= reference_time;
        }

        std::optional<Json> read_json_file(const fs::path& path) {
            std::ifstream input(path);
            if (!input) {
                return std::nullopt;
                }
            try {
                Json value;
                input >> value;
                return value;
            } catch (const Json::exception&) {
                return std::nullopt;
            }
        }

        std::optional<fs::path> latest_file_api_index(const fs::path& build_directory) {
            const fs::path reply_directory = build_directory / ".cmake" / "api" / "v1" / "reply";
            std::error_code error;
            if (!fs::is_directory(reply_directory, error)) {
                return std::nullopt;
            }
            std::optional<fs::path> latest;
            for (const auto& entry : fs::directory_iterator(reply_directory, error)) {
                if (error || !entry.is_regular_file(error)) {
                    error.clear();
                    continue;
                }
                const std::string name = entry.path().filename().string();
                if (!name.starts_with("index-")) {
                    continue;
                }
                if (!latest.has_value() || name > latest->filename().string()) {
                    latest = entry.path();
                }
            }
            return latest;
        }

        std::optional<Json> file_api_object(const Json& index, std::string_view kind, const fs::path& reply_directory) {
            const int expected_major = kind == "codemodel" ? 2 : kind == "cmakeFiles" ? 1
                                                             : kind == "cache"        ? 2
                                                                                      : 0;
            if (expected_major == 0) {
                return std::nullopt;
            }
            if (!index.contains("objects") || !index["objects"].is_array()) {
                return std::nullopt;
            }
            for (const auto& object : index["objects"]) {
                if (!object.is_object() || object.value("kind", "") != kind || !object.contains("version") ||
                    !object["version"].is_object() || object["version"].value("major", 0) != expected_major ||
                    !object.contains("jsonFile") || !object["jsonFile"].is_string()) {
                    continue;
                }
                const fs::path json_file = (reply_directory / object["jsonFile"].get<std::string>()).lexically_normal();
                if (!is_path_within(json_file, reply_directory)) {
                    return std::nullopt;
                }
                return read_json_file(json_file);
            }
            return std::nullopt;
        }

        fs::path resolve_file_api_path(std::string_view value, const fs::path& source_root) {
            const fs::path path(value);
            return path.is_absolute() ? path.lexically_normal() : (source_root / path).lexically_normal();
        }

        std::optional<std::pair<fs::path, std::size_t>> direct_cmake_declaration(const fs::path& cmake_file,
                                                                                 std::string_view target_name,
                                                                                 std::string_view command_name,
                                                                                 std::size_t backtrace_line) {
            std::ifstream input(cmake_file);
            if (!input) {
                return std::nullopt;
            }
            const std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            const std::size_t zero_based_line = backtrace_line == 0 ? 0 : backtrace_line - 1;
            for (const auto& command : parse_cmake_commands(content)) {
                if (command.name != command_name || command.arguments.empty() ||
                    command.arguments.front() != target_name || zero_based_line < command.start_line ||
                    zero_based_line > command.end_line) {
                    continue;
                }
                return std::pair{cmake_file, command.end_line};
            }
            return std::nullopt;
        }

        std::optional<FileApiTarget> parse_file_api_target(const Json& target, const fs::path& source_root,
                                                           const fs::path& build_root) {
            if (!target.is_object() || !target.contains("name") || !target["name"].is_string() ||
                !target.contains("type") || !target["type"].is_string() || target.value("imported", false) ||
                target.value("abstract", false) || target.value("type", "") == "INTERFACE_LIBRARY" ||
                target.value("type", "") == "UTILITY") {
                return std::nullopt;
            }
            const std::string type = target["type"].get<std::string>();
            if (type != "EXECUTABLE" && type != "STATIC_LIBRARY" && type != "SHARED_LIBRARY" &&
                type != "MODULE_LIBRARY" && type != "OBJECT_LIBRARY") {
                return std::nullopt;
            }
            if (!target.contains("backtrace") || !target["backtrace"].is_number_unsigned() ||
                !target.contains("backtraceGraph") || !target["backtraceGraph"].is_object()) {
                return std::nullopt;
            }

            const Json& graph = target["backtraceGraph"];
            if (!graph.contains("nodes") || !graph["nodes"].is_array() || !graph.contains("files") ||
                !graph["files"].is_array() || !graph.contains("commands") || !graph["commands"].is_array()) {
                return std::nullopt;
            }
            const std::size_t backtrace = target["backtrace"].get<std::size_t>();
            if (backtrace >= graph["nodes"].size()) {
                return std::nullopt;
            }
            const Json& node = graph["nodes"][backtrace];
            if (!node.contains("file") || !node["file"].is_number_unsigned() || !node.contains("line") ||
                !node["line"].is_number_unsigned() || !node.contains("command") ||
                !node["command"].is_number_unsigned()) {
                return std::nullopt;
            }
            const std::size_t file_index = node["file"].get<std::size_t>();
            const std::size_t command_index = node["command"].get<std::size_t>();
            if (file_index >= graph["files"].size() || command_index >= graph["commands"].size() ||
                !graph["files"][file_index].is_string() || !graph["commands"][command_index].is_string()) {
                return std::nullopt;
            }
            const std::string command_name = lowercase(graph["commands"][command_index].get<std::string>());
            if (command_name != "add_library" && command_name != "add_executable") {
                return std::nullopt;
            }
            const fs::path cmake_file =
                resolve_file_api_path(graph["files"][file_index].get<std::string>(), source_root);
            const auto declaration = direct_cmake_declaration(cmake_file, target["name"].get<std::string>(),
                                                              command_name, node["line"].get<std::size_t>());
            if (!declaration.has_value()) {
                return std::nullopt;
            }
            if (!is_path_within(cmake_file, source_root)) {
                return std::nullopt;
            }

            if (!target.contains("sources") || !target["sources"].is_array() || !target.contains("compileGroups") ||
                !target["compileGroups"].is_array()) {
                return std::nullopt;
            }
            std::vector<fs::path> sources;
            for (const auto& source : target["sources"]) {
                if (!source.is_object() || !source.contains("path") || !source["path"].is_string()) {
                    return std::nullopt;
                }
                const fs::path path = resolve_file_api_path(source["path"].get<std::string>(), source_root);
                const bool has_compile_group =
                    source.contains("compileGroupIndex") && source["compileGroupIndex"].is_number_unsigned();
                if (!has_compile_group) {
                    if (is_cxx_source(path)) {
                        return std::nullopt;
                    }
                    continue;
                }
                if (source.value("isGenerated", false) || is_path_within(path, build_root) || !is_cxx_source(path) ||
                    !fs::is_regular_file(path)) {
                    return std::nullopt;
                }
                const std::size_t group_index = source["compileGroupIndex"].get<std::size_t>();
                if (group_index >= target["compileGroups"].size() ||
                    !target["compileGroups"][group_index].is_object() ||
                    target["compileGroups"][group_index].value("language", "") != "CXX") {
                    return std::nullopt;
                }
                if (std::ranges::find(sources, path) != sources.end()) {
                    return std::nullopt;
                }
                sources.push_back(path);
            }
            if (sources.empty()) {
                return std::nullopt;
            }

            FileApiTarget result;
            result.cmake_file = declaration->first;
            result.name = target["name"].get<std::string>();
            result.sources = std::move(sources);
            result.end_line = declaration->second;
            return result;
        }

        std::optional<FileApiModel> load_file_api_model(const fs::path& project_root,
                                                        const fs::path& compile_commands_path,
                                                        const std::function<bool()>& should_cancel) {
            try {
                if (should_cancel && should_cancel()) {
                    return std::nullopt;
                }
                const auto index_path = latest_file_api_index(compile_commands_path.parent_path());
                if (!index_path.has_value() || !fs::is_regular_file(compile_commands_path) ||
                    !is_at_least_as_new(*index_path, compile_commands_path)) {
                    return std::nullopt;
                }
                const auto index = read_json_file(*index_path);
                if (!index.has_value()) {
                    return std::nullopt;
                }
                const fs::path reply_directory = index_path->parent_path();
                const auto codemodel = file_api_object(*index, "codemodel", reply_directory);
                const auto cmake_files_object = file_api_object(*index, "cmakeFiles", reply_directory);
                if (!codemodel.has_value() || !cmake_files_object.has_value() || !codemodel->contains("paths") ||
                    !(*codemodel)["paths"].is_object() || !(*codemodel)["paths"].contains("source") ||
                    !(*codemodel)["paths"]["source"].is_string() || !(*codemodel)["paths"].contains("build") ||
                    !(*codemodel)["paths"]["build"].is_string() || !codemodel->contains("configurations") ||
                    !(*codemodel)["configurations"].is_array() || (*codemodel)["configurations"].size() != 1 ||
                    !cmake_files_object->contains("paths") || !(*cmake_files_object)["paths"].is_object() ||
                    !cmake_files_object->contains("inputs") || !(*cmake_files_object)["inputs"].is_array() ||
                    (cmake_files_object->contains("globsDependent") &&
                     (!(*cmake_files_object)["globsDependent"].is_array() ||
                      !(*cmake_files_object)["globsDependent"].empty()))) {
                    return std::nullopt;
                }

                const fs::path source_root =
                    fs::path((*codemodel)["paths"]["source"].get<std::string>()).lexically_normal();
                const fs::path build_root =
                    fs::path((*codemodel)["paths"]["build"].get<std::string>()).lexically_normal();
                if (path_key(source_root) != path_key(project_root)) {
                    return std::nullopt;
                }
                const Json& configuration = (*codemodel)["configurations"][0];
                if (!configuration.contains("targets") || !configuration["targets"].is_array()) {
                    return std::nullopt;
                }

                FileApiModel model;
                for (const auto& input : (*cmake_files_object)["inputs"]) {
                    if (should_cancel && should_cancel()) {
                        return std::nullopt;
                    }
                    if (!input.is_object() || input.value("isGenerated", false) || input.value("isExternal", false) ||
                        !input.contains("path") || !input["path"].is_string()) {
                        continue;
                    }
                    const fs::path path = resolve_file_api_path(input["path"].get<std::string>(), source_root);
                    std::ifstream file(path);
                    if (!file || !is_at_least_as_new(*index_path, path)) {
                        return std::nullopt;
                    }
                    const std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
                    auto commands = parse_cmake_commands(content);
                    model.cmake_commands.insert(model.cmake_commands.end(), std::make_move_iterator(commands.begin()),
                                                std::make_move_iterator(commands.end()));
                }

                if (const auto cache = file_api_object(*index, "cache", reply_directory);
                    cache.has_value() && cache->contains("entries") && (*cache)["entries"].is_array()) {
                    for (const auto& entry : (*cache)["entries"]) {
                        if (!entry.is_object() || entry.value("name", "") != "CMAKE_UNITY_BUILD") {
                            continue;
                        }
                        bool known = false;
                        const bool enabled = cmake_truth_value(entry.value("value", ""), known);
                        model.global_unity = enabled || !known;
                        break;
                    }
                }

                for (const auto& target_reference : configuration["targets"]) {
                    if (should_cancel && should_cancel()) {
                        return std::nullopt;
                    }
                    if (!target_reference.is_object() || !target_reference.contains("jsonFile") ||
                        !target_reference["jsonFile"].is_string()) {
                        continue;
                    }
                    const fs::path target_path =
                        (reply_directory / target_reference["jsonFile"].get<std::string>()).lexically_normal();
                    if (!is_path_within(target_path, reply_directory)) {
                        return std::nullopt;
                    }
                    const auto target = read_json_file(target_path);
                    if (!target.has_value()) {
                        return std::nullopt;
                    }
                    if (const auto parsed = parse_file_api_target(*target, source_root, build_root);
                        parsed.has_value()) {
                        model.targets.push_back(*parsed);
                    }
                }
                return model;
            } catch (const Json::exception&) {
                return std::nullopt;
            }
        }

        bool cmake_truth_value(std::string_view value, bool& known) {
            const std::string normalized = lowercase(value);
            if (normalized == "on" || normalized == "true" || normalized == "yes" || normalized == "1") {
                known = true;
                return true;
            }
            if (normalized == "off" || normalized == "false" || normalized == "no" || normalized == "0") {
                known = true;
                return false;
            }
            known = false;
            return false;
        }

        bool cmake_expression_is_dynamic(std::string_view value) {
            return value.find('$') != std::string_view::npos || value.find('<') != std::string_view::npos ||
                   value.find('>') != std::string_view::npos;
        }

        void record_unity_target_property(std::string_view property, const std::optional<std::string_view>& value,
                                          TargetState& target_state) {
            const std::string name = lowercase(property);
            if (name == "unity_build") {
                if (!value.has_value()) {
                    target_state.unity_state_unknown = true;
                    return;
                }
                bool known = false;
                const bool enabled = cmake_truth_value(*value, known);
                target_state.unity_enabled = target_state.unity_enabled || enabled;
                target_state.unity_state_unknown = target_state.unity_state_unknown || !known;
                return;
            }
            if (name == "unity_build_mode") {
                // GROUP requires per-source UNITY_GROUP modeling. Only the
                // default-compatible BATCH mode is accepted here.
                target_state.unity_state_unknown =
                    target_state.unity_state_unknown || !value.has_value() || lowercase(*value) != "batch";
                return;
            }
            if (name == "unity_build_code_before_include" || name == "unity_build_code_after_include") {
                // These properties inject arbitrary code around every source
                // include and therefore are outside the synthetic TU model.
                target_state.unity_state_unknown = true;
            }
        }

        void record_unity_state(const std::vector<CMakeCommand>& commands, std::string_view target_name,
                                bool& global_unity, TargetState& target_state) {
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

                if (command.name == "set_property" && command.arguments.size() >= 2 &&
                    lowercase(command.arguments[0]) == "target") {
                    const auto property = std::ranges::find_if(
                        command.arguments.begin() + 1, command.arguments.end(),
                        [](const std::string& argument) { return lowercase(argument) == "property"; });
                    const bool target_list_contains_candidate =
                        property != command.arguments.end() &&
                        std::ranges::find(command.arguments.begin() + 1, property, std::string(target_name)) !=
                            property;
                    if (!target_list_contains_candidate || property == command.arguments.end()) {
                        if (property != command.arguments.end() &&
                            std::ranges::any_of(
                                command.arguments.begin() + 1, property,
                                [](const std::string& argument) { return cmake_expression_is_dynamic(argument); })) {
                            target_state.unity_state_unknown = true;
                        }
                        continue;
                    }
                    if (property + 1 == command.arguments.end()) {
                        target_state.unity_state_unknown = true;
                        continue;
                    }
                    const auto property_name = property + 1;
                    const auto value = property + 2 < command.arguments.end()
                        ? std::optional<std::string_view>(*(property + 2))
                        : std::nullopt;
                    record_unity_target_property(*property_name, value, target_state);
                    continue;
                }

                if (command.name != "set_target_properties") {
                    continue;
                }
                const auto properties = std::ranges::find_if(
                    command.arguments, [](const std::string& argument) { return lowercase(argument) == "properties"; });
                if (properties == command.arguments.end()) {
                    continue;
                }
                const auto target = std::ranges::find(command.arguments.begin(), properties, std::string(target_name));
                if (target == properties) {
                    if (std::ranges::any_of(command.arguments.begin(), properties, [](const std::string& argument) {
                            return cmake_expression_is_dynamic(argument);
                        })) {
                        target_state.unity_state_unknown = true;
                    }
                    continue;
                }
                const auto property_index =
                    static_cast<std::size_t>(std::distance(command.arguments.begin(), properties));
                if (property_index + 1 >= command.arguments.size()) {
                    target_state.unity_state_unknown = true;
                    continue;
                }
                for (std::size_t index = property_index + 1; index < command.arguments.size(); index += 2) {
                    if (cmake_expression_is_dynamic(command.arguments[index])) {
                        target_state.unity_state_unknown = true;
                        continue;
                    }
                    const auto value = index + 1 < command.arguments.size()
                        ? std::optional<std::string_view>(command.arguments[index + 1])
                        : std::nullopt;
                    record_unity_target_property(command.arguments[index], value, target_state);
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
            return argument == "-o" || argument == "-MF" || argument == "-MT" || argument == "-MQ" ||
                   argument == "/Fo" || argument == "/Fd" || argument == "/Fp" || argument == "/Fa" ||
                   argument == "/Fe";
        }

        bool is_attached_output_option(std::string_view argument) {
            return argument.starts_with("-o") || argument.starts_with("-MF") || argument.starts_with("-MT") ||
                   argument.starts_with("-MQ") || argument.starts_with("/Fo") || argument.starts_with("/Fd") ||
                   argument.starts_with("/Fp") || argument.starts_with("/Fa") || argument.starts_with("/Fe");
        }

        bool is_pch_option(std::string_view argument) {
            return argument == "-include-pch" || argument == "-Winvalid-pch" || argument == "/Yc" ||
                   argument == "/Yu" || argument.starts_with("/Yc") || argument.starts_with("/Yu");
        }

        std::optional<std::vector<std::string>> syntax_arguments(const CompilationUnit& command,
                                                                 const fs::path& source) {
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
                if (is_attached_output_option(argument) || argument == "-MD" || argument == "-MMD" ||
                    argument == "-MP" || argument == "-fsyntax-only") {
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
        bool validate_unity_translation_unit(const std::vector<CompileEvidence>& evidence, std::string& diagnostic) {
            if (evidence.empty()) {
                diagnostic = "No compile evidence was available for the unity group";
                return false;
            }
            const auto arguments = syntax_arguments(evidence.front().command, evidence.front().source);
            if (!arguments.has_value()) {
                diagnostic = "The compile command uses a precompiled-header mode that "
                             "cannot be replayed safely";
                return false;
            }

            std::string code;
            for (const auto& item : evidence) {
                code += "#include \"";
                code += escape_include_path(item.source);
                code += "\"\n";
            }
            const fs::path virtual_file = evidence.front().command.working_directory / "__bha_unity_validation__.cpp";
            const std::string compiler = compiler_identity(evidence.front().command);
            const bool clang_cl =
                compiler.find("clang-cl") != std::string::npos || compiler == "cl.exe" || compiler == "cl";
            const bool valid =
                clang::tooling::runToolOnCodeWithArgs(std::make_unique<clang::SyntaxOnlyAction>(), code, *arguments,
                                                      virtual_file.string(), clang_cl ? "clang-cl" : "clang++");
            if (!valid) {
                diagnostic = "Clang rejected the proposed unity translation unit";
            }
            return valid;
        }
#else
        bool validate_unity_translation_unit(const std::vector<CompileEvidence>&, std::string& diagnostic) {
            diagnostic = "Unity suggestions require Clang LibTooling for merged-TU validation";
            return false;
        }
#endif

        std::optional<std::vector<std::string>> normalized_environment(const CompilationUnit& command,
                                                                       const fs::path& source) {
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

        TargetState target_unity_state(const std::vector<CMakeCommand>& cmake_commands, std::string_view target_name,
                                       bool& global_unity) {
            TargetState state;
            record_unity_state(cmake_commands, target_name, global_unity, state);
            return state;
        }

    }  // namespace

    Result<SuggestionResult, Error> UnityBuildSuggester::suggest(const SuggestionContext& context) const {
        SuggestionResult result;
        const auto start = std::chrono::steady_clock::now();

        if (!context.project_index ||
            context.project_index->compile_commands_status() != CompilationDatabaseStatus::Loaded) {
            result.diagnostics.push_back({"unity.compile_commands.required",
                                          "Unity suggestions require a valid compile_commands.json for every "
                                          "source in the target"});
            result.generation_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        const fs::path project_root = context.project_root.empty()
            ? context.project_index->project_root()
            : context.project_index->resolve(context.project_root);
        if (project_root.empty() || !fs::is_directory(project_root)) {
            result.diagnostics.push_back(
                {"unity.project_root.required", "Unity suggestions require a project root containing CMakeLists.txt"});
            result.generation_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        if (!context.options.compile_commands_path.has_value()) {
            result.diagnostics.push_back({"unity.file_api.required", "Unity suggestions require a CMake File "
                                                                     "API reply beside compile_commands.json"});
            result.generation_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }
        const fs::path compile_commands_path =
            context.options.compile_commands_path->is_relative()
                ? (project_root / *context.options.compile_commands_path).lexically_normal()
                : context.options.compile_commands_path->lexically_normal();
        const auto file_api_model =
            load_file_api_model(project_root, compile_commands_path, [&]() { return context.is_cancelled(); });
        if (!file_api_model.has_value()) {
            if (context.is_cancelled()) {
                result.generation_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
                return Result<SuggestionResult, Error>::success(std::move(result));
            }
            result.diagnostics.push_back({"unity.file_api.required", "Unity suggestions require a current CMake "
                                                                     "File API codemodel and cmakeFiles reply"});
            result.generation_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        std::unordered_map<std::string, analyzers::FileAnalysisResult> analyzed_files;
        for (const auto& file : context.analysis.files) {
            const fs::path source = context.project_index->resolve(file.file);
            if (is_cxx_source(source) && context.should_analyze(source)) {
                analyzed_files.emplace(path_key(source), file);
            }
        }

        std::size_t analyzed_targets = 0;
        std::size_t skipped_targets = 0;
        std::unordered_map<std::string, std::size_t> target_name_counts;
        for (const auto& target : file_api_model->targets) {
            ++target_name_counts[target.name];
        }

        for (const auto& target : file_api_model->targets) {
            if (context.is_cancelled()) {
                break;
            }
            ++analyzed_targets;
            if (target_name_counts[target.name] != 1) {
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

            bool global_unity = file_api_model->global_unity;
            TargetState state = target_unity_state(file_api_model->cmake_commands, target.name, global_unity);
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
            suggestion.description = "Enable CMake UNITY_BUILD for the configured target source set after "
                                     "Clang "
                "accepted the proposed include-ordered unity translation unit.";
            suggestion.rationale = "The target source list and source order came from the configured "
                                   "CMake "
                                   "File API codemodel, every source has a compile-database command with "
                                   "the same environment, and the merged translation unit passed Clang "
                                   "syntax validation.";
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
            edit.new_text = "\nif(TARGET " + target.name +
                            ")\n"
                            "  set_property(TARGET " +
                            target.name + " PROPERTY UNITY_BUILD ON)\n";
            edit.new_text += "endif()\n";
            suggestion.edits.push_back(edit);
            suggestion.after_code.file = target.cmake_file.filename().string();
            suggestion.after_code.code = edit.new_text;
            suggestion.implementation_steps = {"Apply the target-scoped CMake UNITY_BUILD edit",
                "Configure and build the target with the project compiler",
                                               "Run the target tests and compare a fresh build trace"};
            suggestion.caveats = {"Savings are intentionally unestimated until a post-edit trace is "
                                  "available",
                                  "CMake unity builds combine sources in target order and can expose "
                                  "include-order or ODR issues",
                                  "CMake may exclude sources marked SKIP_UNITY_BUILD_INCLUSION",
                                  "A current single-configuration CMake File API reply is required; "
                                  "unsupported or stale build models fail closed"};
            suggestion.verification = "The configured CMake File API target model and "
                                      "Clang merged-TU syntax validation "
                                      "passed; configure, build, and test the target "
                                      "with the project compiler before "
                                      "accepting the edit";
            suggestion.is_safe = true;
            suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
            suggestion.impact.total_files_affected = target.sources.size();
            suggestion.impact.cumulative_savings = Duration::zero();
            result.suggestions.push_back(std::move(suggestion));
        }

        result.items_analyzed = analyzed_targets;
        result.items_skipped = skipped_targets;
        result.generation_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_unity_build_suggester() {
        SuggesterRegistry::instance().register_suggester(std::make_unique<UnityBuildSuggester>());
    }
}  // namespace bha::suggestions
