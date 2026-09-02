#include "bha/lsp/suggestion_manager.hpp"
#include "bha/lsp/diagnostic_parser.hpp"
#include "bha/build_systems/adapter.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/suggestions/all_suggesters.hpp"
#include "bha/suggestions/suggester.hpp"
#include "bha/suggestions/consolidator.hpp"
#include "bha/lsp/uri.hpp"
#include "bha/utils/path_utils.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <climits>
#endif
#endif

namespace bha::lsp
{
    namespace fs = std::filesystem;
    namespace path_utils = bha::utils;

    namespace {
        template<typename Rep, typename Period>
        [[nodiscard]] std::string format_elapsed_ms(const std::chrono::duration<Rep, Period> elapsed) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            if (ms < 1000) {
                return std::to_string(ms) + "ms";
            }

            std::ostringstream out;
            out << std::fixed << std::setprecision(1) << (static_cast<double>(ms) / 1000.0) << "s";
            return out.str();
        }

        [[nodiscard]] std::vector<fs::path> collect_backup_files(const bha::Suggestion& suggestion) {
            std::vector<fs::path> files;
            std::unordered_set<std::string> seen;

            const auto add_file = [&](const fs::path& file) {
                if (file.empty()) {
                    return;
                }
                const fs::path normalized = file.lexically_normal();
                const std::string key = normalized.generic_string();
                if (seen.insert(key).second) {
                    files.push_back(normalized);
                }
            };

            for (const auto& edit : suggestion.edits) {
                add_file(edit.file);
            }
            add_file(suggestion.target_file.path);
            for (const auto& secondary : suggestion.secondary_files) {
                add_file(secondary.path);
            }

            std::ranges::sort(
                files,
                [](const fs::path& lhs, const fs::path& rhs) {
                    return lhs.generic_string() < rhs.generic_string();
                }
            );

            return files;
        }
    }

    BuildSystemType build_system_type_from_adapter_name(std::string name) {
        std::ranges::transform(name, name.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name == "cmake") {
            return BuildSystemType::CMake;
        }
        if (name == "ninja") {
            return BuildSystemType::Ninja;
        }
        if (name == "make") {
            return BuildSystemType::Make;
        }
        if (name == "msbuild") {
            return BuildSystemType::MSBuild;
        }
        if (name == "bazel") {
            return BuildSystemType::Bazel;
        }
        if (name == "buck2") {
            return BuildSystemType::Buck2;
        }
        if (name == "meson") {
            return BuildSystemType::Meson;
        }
        if (name == "scons") {
            return BuildSystemType::SCons;
        }
        if (name == "xcode") {
            return BuildSystemType::XCode;
        }
        return BuildSystemType::Unknown;
    }

    BuildSystemType detect_build_system_from_build_dir(const std::optional<fs::path>& build_dir) {
        if (!build_dir.has_value() || build_dir->empty() || !fs::exists(*build_dir)) {
            return BuildSystemType::Unknown;
        }
        if (fs::exists(*build_dir / "CMakeCache.txt") || fs::exists(*build_dir / "CMakeFiles")) {
            return BuildSystemType::CMake;
        }
        if (fs::exists(*build_dir / "meson-info") || fs::exists(*build_dir / "meson-private")) {
            return BuildSystemType::Meson;
        }
        if (fs::exists(*build_dir / "build.ninja")) {
            return BuildSystemType::Ninja;
        }
        if (fs::exists(*build_dir / "Makefile") ||
            fs::exists(*build_dir / "makefile") ||
            fs::exists(*build_dir / "GNUmakefile")) {
            return BuildSystemType::Make;
        }
        return BuildSystemType::Unknown;
    }

    fs::path resolve_trace_root(const fs::path& project_root, const std::optional<fs::path>& build_dir) {
        if (!build_dir.has_value()) {
            fs::path default_build_traces = project_root / "build" / "traces";
            if (fs::exists(default_build_traces)) {
                return default_build_traces;
            }

            fs::path sibling_traces = project_root / "traces";
            if (fs::exists(sibling_traces)) {
                return sibling_traces;
            }

            fs::path default_build_dir = project_root / "build";
            if (fs::exists(default_build_dir)) {
                return default_build_dir;
            }

            return default_build_traces;
        }

        fs::path direct_traces = *build_dir / "traces";
        if (fs::exists(direct_traces)) {
            return direct_traces;
        }

        fs::path sibling_traces = build_dir->parent_path() / "traces";
        if (fs::exists(sibling_traces)) {
            return sibling_traces;
        }

        return *build_dir;
    }

    std::unordered_map<std::string, std::vector<fs::path>> index_sources_by_filename(
        const std::vector<fs::path>& sources
    ) {
        std::unordered_map<std::string, std::vector<fs::path>> index;
        index.reserve(sources.size());
        for (const auto& source : sources) {
            const std::string filename = source.filename().string();
            if (filename.empty()) {
                continue;
            }
            index[filename].push_back(source.lexically_normal());
        }
        return index;
    }

    fs::path normalize_path_for_match(fs::path path, const std::optional<fs::path>& base);

    std::optional<fs::path> resolve_trace_source_with_compile_commands(
        const fs::path& raw_source,
        const fs::path& project_root,
        const std::unordered_map<std::string, std::vector<fs::path>>& by_filename
    ) {
        if (raw_source.empty()) {
            return std::nullopt;
        }

        if (raw_source.is_absolute() && fs::exists(raw_source)) {
            return raw_source.lexically_normal();
        }

        if (!project_root.empty()) {
            fs::path candidate = (project_root / raw_source).lexically_normal();
            if (fs::exists(candidate)) {
                return candidate;
            }
        }

        const std::string filename = raw_source.filename().string();
        if (filename.empty()) {
            return std::nullopt;
        }

        const auto it = by_filename.find(filename);
        if (it == by_filename.end() || it->second.empty()) {
            return std::nullopt;
        }
        if (it->second.size() == 1) {
            return it->second.front();
        }

        const std::string raw_suffix = raw_source.lexically_normal().generic_string();
        for (const auto& candidate : it->second) {
            const std::string candidate_str = candidate.generic_string();
            if (candidate_str.size() >= raw_suffix.size() &&
                candidate_str.compare(candidate_str.size() - raw_suffix.size(), raw_suffix.size(), raw_suffix) == 0) {
                return candidate;
            }
        }

        return it->second.front();
    }

    std::optional<fs::path> detect_project_root_with_registered_adapters(
        const fs::path& start_path,
        build_systems::BuildSystemRegistry& registry
    ) {
        if (start_path.empty()) {
            return std::nullopt;
        }

        fs::path current = start_path;
        while (!current.empty()) {
            if (registry.detect(current) != nullptr) {
                return current;
            }
            if (!current.has_parent_path()) {
                break;
            }
            const fs::path parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
        return std::nullopt;
    }

    std::optional<fs::path> compile_command_source_path_from_entry(const nlohmann::json& entry) {
        if (!entry.is_object() || !entry.contains("file") || !entry["file"].is_string()) {
            return std::nullopt;
        }

        std::optional<fs::path> directory;
        if (entry.contains("directory") && entry["directory"].is_string()) {
            directory = fs::path(entry["directory"].get<std::string>());
        }

        return normalize_path_for_match(fs::path(entry["file"].get<std::string>()), directory);
    }

    std::optional<fs::path> resolve_compile_command_backed_source(
        const fs::path& raw_source,
        const fs::path& project_root,
        const std::unordered_set<std::string>& compile_command_sources,
        const std::unordered_map<std::string, std::vector<fs::path>>& compile_sources_by_filename
    ) {
        fs::path normalized = normalize_path_for_match(raw_source, project_root.empty()
            ? std::nullopt
            : std::make_optional(project_root));
        if (!normalized.empty() && compile_command_sources.contains(normalized.generic_string())) {
            return normalized;
        }

        auto resolved = resolve_trace_source_with_compile_commands(raw_source, project_root, compile_sources_by_filename);
        if (!resolved.has_value()) {
            return std::nullopt;
        }

        fs::path normalized_resolved = normalize_path_for_match(*resolved, std::nullopt);
        if (normalized_resolved.empty() ||
            !compile_command_sources.contains(normalized_resolved.generic_string())) {
            return std::nullopt;
        }

        return normalized_resolved;
    }

    std::optional<fs::path> resolve_trace_file_to_project(
        const fs::path& project_root,
        const fs::path& trace_path,
        std::unordered_map<std::string, std::optional<fs::path>>& cache
    ) {
        if (project_root.empty() || trace_path.empty()) {
            return std::nullopt;
        }

        const auto filename = trace_path.filename().string();
        if (auto it = cache.find(filename); it != cache.end()) {
            return it->second;
        }

        auto found = suggestions::find_file_in_repo(project_root, trace_path.filename());
        cache.emplace(filename, found);
        return found;
    }

    std::string normalize_pch_text(const std::string& input) {
        std::istringstream in(input);
        std::vector<std::string> prefix_lines;
        std::vector<std::string> include_targets;
        std::string line;
        bool in_includes = false;

        auto normalize_include = [](const std::string& raw) -> std::string {
            std::string trimmed = raw;
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
                trimmed.erase(trimmed.begin());
            }
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
                trimmed.pop_back();
            }
            if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
                trimmed = trimmed.substr(1, trimmed.size() - 2);
            }
            if (trimmed.size() >= 2 && trimmed.front() == '<' && trimmed.back() == '>') {
                return trimmed;
            }
            if (!trimmed.empty() && trimmed.front() == '/') {
                const std::string marker = "/include/";
                const auto pos = trimmed.rfind(marker);
                std::string include_path = (pos != std::string::npos)
                    ? trimmed.substr(pos + marker.size())
                    : std::filesystem::path(trimmed).filename().string();
                if (!include_path.empty()) {
                    return "<" + include_path + ">";
                }
            }
            if (!trimmed.empty()) {
                return "<" + trimmed + ">";
            }
            return trimmed;
        };

        while (std::getline(in, line)) {
            if (line.starts_with("#include")) {
                in_includes = true;
                auto pos = line.find_first_of("<\"");
                if (pos != std::string::npos) {
                    std::string target = line.substr(pos);
                    include_targets.push_back(normalize_include(target));
                }
                continue;
            }
            if (!in_includes) {
                if (!line.empty() || !prefix_lines.empty()) {
                    prefix_lines.push_back(line);
                }
            }
        }

        std::sort(include_targets.begin(), include_targets.end());
        include_targets.erase(std::unique(include_targets.begin(), include_targets.end()), include_targets.end());

        std::ostringstream out;
        bool wrote_prefix = false;
        for (const auto& pline : prefix_lines) {
            if (!pline.empty() || wrote_prefix) {
                out << pline << "\n";
                wrote_prefix = true;
            }
        }
        if (!wrote_prefix) {
            out << "#pragma once\n\n";
        }
        if (!include_targets.empty()) {
            if (out.str().find("// External/System Headers") == std::string::npos) {
                out << "// External/System Headers\n";
            }
            for (const auto& target : include_targets) {
                if (!target.empty()) {
                    out << "#include " << target << "\n";
                }
            }
        }
        std::string result = out.str();
        while (result.size() >= 2 && result.ends_with("\n\n")) {
            result.pop_back();
        }
        if (!result.empty() && result.back() != '\n') {
            result.push_back('\n');
        }
        return result;
    }

    bool is_build_system_file(const fs::path& path) {
        const auto name = path.filename().string();
        return name == "CMakeLists.txt" ||
               name == "meson.build" ||
               name == "build.ninja" ||
               name == "Makefile" ||
               name == "makefile" ||
               name == "GNUmakefile" ||
               name == "WORKSPACE" ||
               name == "WORKSPACE.bazel" ||
               name == "MODULE.bazel" ||
               name == "BUILD" ||
               name == "BUILD.bazel" ||
               name == "BUCK" ||
               name == "BUCK.v2" ||
               name == ".buckconfig" ||
               name == "SConstruct" ||
               name == "SConscript";
    }

    fs::path resolve_relative_path(const fs::path& path, const fs::path& project_root) {
        if (path.empty() || project_root.empty() || path.is_absolute()) {
            return path;
        }
        return (project_root / path).lexically_normal();
    }

    fs::path remap_build_system_path(const fs::path& path, const fs::path& project_root) {
        if (path.empty() || project_root.empty()) {
            return path;
        }
        if (path_utils::is_under(path, project_root)) {
            return path;
        }
        fs::path candidate = project_root / path.filename();
        if (fs::exists(candidate)) {
            return candidate;
        }
        return path;
    }


    std::string shell_quote(const std::string& input) {
#ifdef _WIN32
        std::string escaped = "\"";
        for (const char c : input) {
            if (c == '"') {
                escaped += "\\\"";
            } else {
                escaped.push_back(c);
            }
        }
        escaped.push_back('"');
        return escaped;
#else
        std::string escaped;
        escaped.reserve(input.size() + 2);
        escaped.push_back('\'');
        for (const char c : input) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped.push_back(c);
            }
        }
        escaped.push_back('\'');
        return escaped;
#endif
    }

    std::string to_lower_ascii(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return text;
    }

    std::vector<std::string> split_shell_command(const std::string& command) {
        std::vector<std::string> parts;
        std::string current;
        char quote = '\0';
        bool escaped = false;

        for (const char ch : command) {
            if (escaped) {
                current.push_back(ch);
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (quote != '\0') {
                if (ch == quote) {
                    quote = '\0';
                } else {
                    current.push_back(ch);
                }
                continue;
            }
            if (ch == '"' || ch == '\'') {
                quote = ch;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!current.empty()) {
                    parts.push_back(std::move(current));
                    current.clear();
                }
                continue;
            }
            current.push_back(ch);
        }

        if (!current.empty()) {
            parts.push_back(std::move(current));
        }
        return parts;
    }

    bool is_cpp_source_path(const fs::path& path) {
        const std::string ext = to_lower_ascii(path.extension().string());
        return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++" || ext == ".cu";
    }

    fs::path normalize_path_for_match(fs::path path, const std::optional<fs::path>& base = std::nullopt) {
        if (path.empty()) {
            return path;
        }
        if (path.is_relative() && base.has_value() && !base->empty()) {
            path = *base / path;
        }
        std::error_code ec;
        if (path.is_relative()) {
            path = fs::absolute(path, ec);
        }
        return path.lexically_normal();
    }

    bool is_compiler_wrapper(const std::string& arg) {
        static const std::unordered_set<std::string> wrappers = {
            "ccache", "sccache", "distcc", "icecc", "gomacc"
        };
        const std::string name = to_lower_ascii(fs::path(arg).filename().string());
        return wrappers.contains(name);
    }

    std::string select_primary_compiler_token(const std::vector<std::string>& args) {
        if (args.empty()) {
            return {};
        }
        for (const auto& arg : args) {
            if (!is_compiler_wrapper(arg)) {
                return arg;
            }
        }
        return args.front();
    }

    bool is_msvc_driver(const std::string& compiler_token) {
        const std::string name = to_lower_ascii(fs::path(compiler_token).filename().string());
        if (name.find("clang-cl") != std::string::npos) {
            return false;
        }
        return name == "cl" || name == "cl.exe";
    }

    std::vector<std::string> load_compile_command_args_for_source(
        const std::optional<fs::path>& compile_commands_path,
        const fs::path& source_file
    ) {
        std::vector<std::string> args;
        if (!compile_commands_path.has_value() || compile_commands_path->empty() || !fs::exists(*compile_commands_path)) {
            return args;
        }

        std::ifstream in(*compile_commands_path);
        if (!in) {
            return args;
        }

        nlohmann::json compile_db;
        try {
            in >> compile_db;
        } catch (const nlohmann::json::exception&) {
            return args;
        }
        if (!compile_db.is_array()) {
            return args;
        }

        const fs::path needle = normalize_path_for_match(source_file);
        for (const auto& entry : compile_db) {
            if (!entry.is_object()) {
                continue;
            }

            auto normalized_candidate = compile_command_source_path_from_entry(entry);
            if (!normalized_candidate.has_value()) {
                continue;
            }

            if (*normalized_candidate != needle && normalized_candidate->filename() != needle.filename()) {
                continue;
            }

            if (entry.contains("arguments") && entry["arguments"].is_array()) {
                for (const auto& arg : entry["arguments"]) {
                    if (arg.is_string()) {
                        args.push_back(arg.get<std::string>());
                    }
                }
            } else if (entry.contains("command") && entry["command"].is_string()) {
                args = split_shell_command(entry["command"].get<std::string>());
            }

            std::optional<fs::path> directory;
            if (entry.contains("directory") && entry["directory"].is_string()) {
                directory = fs::path(entry["directory"].get<std::string>());
            }
            if (!args.empty() && directory.has_value()) {
                for (auto& arg : args) {
                    fs::path path_arg(arg);
                    if (path_arg.is_relative() && is_cpp_source_path(path_arg)) {
                        arg = normalize_path_for_match(path_arg, directory).string();
                    }
                }
            }
            return args;
        }

        return args;
    }

    std::vector<std::string> filter_compile_args_for_syntax_check(
        const std::vector<std::string>& args,
        const fs::path& source_file
    ) {
        std::vector<std::string> filtered;
        if (args.empty()) {
            return filtered;
        }

        filtered.reserve(args.size() + 2);
        filtered.push_back(args.front());

        const fs::path normalized_source = normalize_path_for_match(source_file);
        for (std::size_t i = 1; i < args.size(); ++i) {
            const std::string& arg = args[i];
            const std::string arg_lower = to_lower_ascii(arg);

            const auto skip_next_value_flag = [&](const std::string& flag) {
                return arg == flag;
            };

            if (skip_next_value_flag("-o") || skip_next_value_flag("-MF") || skip_next_value_flag("-MT") ||
                skip_next_value_flag("-MQ") || skip_next_value_flag("-MJ") ||
                skip_next_value_flag("-include") || skip_next_value_flag("-include-pch") ||
                skip_next_value_flag("/Fo") || skip_next_value_flag("/Fe") ||
                skip_next_value_flag("/Fp") || skip_next_value_flag("/FI") ||
                skip_next_value_flag("/Yu") || skip_next_value_flag("/Yc")) {
                if (i + 1 < args.size()) {
                    ++i;
                }
                continue;
            }

            if (arg == "-Xclang" && i + 1 < args.size()) {
                const std::string& next = args[i + 1];
                if (next == "-include-pch" || next == "-include" || next == "-emit-pch") {
                    ++i;
                    if (i + 1 < args.size() && args[i + 1] == "-Xclang") {
                        ++i;
                    }
                    if (i + 1 < args.size()) {
                        ++i;
                    }
                    continue;
                }
            }

            if (arg == "-c" || arg == "-S" || arg == "-E" || arg == "/c" ||
                arg == "-Winvalid-pch" || arg == "-MD" || arg == "-MMD") {
                continue;
            }

            if (arg.starts_with("-o") || arg.starts_with("-MF") || arg.starts_with("-MT") ||
                arg.starts_with("-MQ") || arg.starts_with("-MJ") ||
                arg.starts_with("-include") || arg.starts_with("-include-pch") ||
                arg.starts_with("/Fo") || arg.starts_with("/Fe") ||
                arg.starts_with("/Fp") || arg.starts_with("/FI") ||
                arg.starts_with("/Yu") || arg.starts_with("/Yc")) {
                continue;
            }

            const fs::path arg_path(arg);
            if (is_cpp_source_path(arg_path)) {
                if (normalize_path_for_match(arg_path) == normalized_source ||
                    arg_path.filename() == normalized_source.filename()) {
                    continue;
                }
            }

            // Skip linker-like flags when present in compile database commands.
            if (arg == "-Wl" || arg.starts_with("-Wl,") || arg_lower == "/link") {
                continue;
            }

            filtered.push_back(arg);
        }

        return filtered;
    }

    std::optional<std::string> build_syntax_check_command(
        const std::vector<std::string>& compile_args,
        const fs::path& source_file
    ) {
        if (compile_args.empty()) {
            return std::nullopt;
        }

        auto command_args = filter_compile_args_for_syntax_check(compile_args, source_file);
        if (command_args.empty()) {
            return std::nullopt;
        }

        const std::string compiler_token = select_primary_compiler_token(command_args);
        if (compiler_token.empty()) {
            return std::nullopt;
        }

        if (is_msvc_driver(compiler_token)) {
            command_args.emplace_back("/Zs");
        } else {
            command_args.emplace_back("-fsyntax-only");
        }
        command_args.push_back(normalize_path_for_match(source_file).string());

        std::ostringstream cmd;
        for (std::size_t i = 0; i < command_args.size(); ++i) {
            if (i > 0) {
                cmd << ' ';
            }
            cmd << shell_quote(command_args[i]);
        }
        return cmd.str();
    }

    std::optional<std::string> build_header_syntax_check_command(
        const std::vector<std::string>& compile_args,
        const fs::path& reference_source_file,
        const fs::path& header_file
    ) {
        if (compile_args.empty()) {
            return std::nullopt;
        }

        auto command_args = filter_compile_args_for_syntax_check(compile_args, reference_source_file);
        if (command_args.empty()) {
            return std::nullopt;
        }

        const std::string compiler_token = select_primary_compiler_token(command_args);
        if (compiler_token.empty()) {
            return std::nullopt;
        }

        if (is_msvc_driver(compiler_token)) {
            command_args.emplace_back("/Zs");
            const bool has_language_mode = std::ranges::any_of(command_args, [](const std::string& token) {
                return token == "/TP" || token == "/TC" ||
                       token.starts_with("/Tp") || token.starts_with("/Tc");
            });
            if (!has_language_mode) {
                command_args.emplace_back("/TP");
            }
        } else {
            command_args.emplace_back("-Winvalid-pch");
            command_args.emplace_back("-fsyntax-only");
            command_args.emplace_back("-x");
            command_args.emplace_back("c++-header");
        }
        command_args.push_back(normalize_path_for_match(header_file).string());

        std::ostringstream cmd;
        for (std::size_t i = 0; i < command_args.size(); ++i) {
            if (i > 0) {
                cmd << ' ';
            }
            cmd << shell_quote(command_args[i]);
        }
        return cmd.str();
    }

    int run_command_collect_output(
        const std::string& command,
        const int timeout_seconds,
        std::string& output
    ) {
        output.clear();
        std::string effective_command = command;
#ifdef _WIN32
        (void)timeout_seconds;
#else
        if (timeout_seconds > 0 && std::system("command -v timeout >/dev/null 2>&1") == 0) {
            effective_command = "timeout --signal=TERM " +
                std::to_string(timeout_seconds) + "s /bin/bash -lc " + shell_quote(command);
        }
#endif

        FILE* pipe = popen((effective_command + " 2>&1").c_str(), "r");
        if (!pipe) {
            return -1;
        }

        std::array<char, 4096> buffer{};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
            if (output.size() > std::size_t{1024} * 1024) {
                break;
            }
        }

        int raw_status = pclose(pipe);
        int exit_code = raw_status;
#ifndef _WIN32
        if (WIFEXITED(raw_status)) {
            exit_code = WEXITSTATUS(raw_status);
        }
#endif
        return exit_code;
    }

    std::string truncate_for_diagnostic(const std::string& text, std::size_t max_chars = 1200);

    bool append_validation_command_failure(
        const std::string& validation_label,
        const fs::path& validated_path,
        const int timeout_seconds,
        const int exit_code,
        const std::string& syntax_output,
        std::vector<Diagnostic>& errors
    ) {
        const auto compiler_diagnostics = parse_compiler_diagnostics(syntax_output);
        errors.insert(errors.end(), compiler_diagnostics.begin(), compiler_diagnostics.end());

        if (!compiler_diagnostics.empty()) {
            return false;
        }

        Diagnostic diag;
        diag.severity = DiagnosticSeverity::Error;
        diag.source = "bha-lsp";
        if (timeout_seconds > 0 && exit_code == 124) {
            diag.message = validation_label + " syntax validation timed out for " + validated_path.string();
        } else if (!syntax_output.empty()) {
            diag.message = validation_label + " syntax validation failed for " + validated_path.string() +
                ":\n" + truncate_for_diagnostic(syntax_output);
        } else if (exit_code == -1) {
            diag.message = validation_label + " syntax validation failed to launch compiler process";
        } else {
            diag.message = validation_label + " syntax validation failed for " + validated_path.string() +
                " with exit code " + std::to_string(exit_code);
        }
        errors.push_back(std::move(diag));
        return false;
    }

    std::string truncate_for_diagnostic(const std::string& text, const std::size_t max_chars) {
        if (text.size() <= max_chars) {
            return text;
        }
        return text.substr(0, max_chars) + "\n... (truncated)";
    }

    bool sync_file_to_disk(const fs::path& path) {
#ifdef _WIN32
        const int fd = _open(path.string().c_str(), _O_RDONLY | _O_BINARY);
        if (fd < 0) {
            return false;
        }
        const bool ok = _commit(fd) == 0;
        _close(fd);
        return ok;
#else
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return false;
        }
        const bool ok = ::fsync(fd) == 0;
        ::close(fd);
        return ok;
#endif
    }

    bool sync_directory_to_disk(const fs::path& dir) {
#ifdef _WIN32
        (void)dir;
        return true;
#else
        int open_flags = O_RDONLY;
#ifdef O_DIRECTORY
        open_flags |= O_DIRECTORY;
#endif
        const int fd = ::open(dir.c_str(), open_flags);
        if (fd < 0) {
            return false;
        }
        const bool ok = ::fsync(fd) == 0;
        ::close(fd);
        return ok;
#endif
    }

    bool copy_file_with_sync(const fs::path& src, const fs::path& dest) {
        std::ifstream in(src, std::ios::binary);
        if (!in) {
            return false;
        }
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }

        out << in.rdbuf();
        out.flush();
        out.close();
        in.close();
        if (!out.good()) {
            return false;
        }

        if (!sync_file_to_disk(dest)) {
            return false;
        }

        const fs::path parent = dest.parent_path();
        return parent.empty() || sync_directory_to_disk(parent);
    }

    std::string resolve_bha_cli_binary() {
        if (const char* env = std::getenv("BHA_CLI")) {
            fs::path path = env;
            if (!path.empty()) {
                return path.string();
            }
        }

#ifdef __linux__
        std::array<char, PATH_MAX> buffer{};
        const auto len = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (len > 0) {
            buffer[static_cast<std::size_t>(len)] = '\0';
            const fs::path sibling = fs::path(buffer.data()).parent_path() / "bha";
            if (fs::exists(sibling)) {
                return sibling.string();
            }
        }
#endif

        const std::array candidates{
            fs::current_path() / "build" / "bha",
            fs::path("bha")
        };
        for (const auto& candidate : candidates) {
            if (candidate.is_absolute()) {
                if (fs::exists(candidate)) {
                    return candidate.string();
                }
                continue;
            }
            return candidate.string();
        }
        return "bha";
    }

    std::string resolve_bha_refactor_binary() {
        if (const char* env = std::getenv("BHA_REFACTOR")) {
            fs::path path = env;
            if (!path.empty()) {
                return path.string();
            }
        }

#ifdef __linux__
        std::array<char, PATH_MAX> buffer{};
        const auto len = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (len > 0) {
            buffer[static_cast<std::size_t>(len)] = '\0';
            const fs::path self_dir = fs::path(buffer.data()).parent_path();
            const std::array candidates{
                (self_dir.parent_path() / "bha-refactor").lexically_normal(),
                (self_dir / "bha-refactor").lexically_normal()
            };
            for (const auto& candidate : candidates) {
                if (fs::exists(candidate)) {
                    return candidate.string();
                }
            }
        }
#endif

        const std::array candidates{
            fs::current_path() / "build" / "bha-refactor",
            fs::path("bha-refactor")
        };
        for (const auto& candidate : candidates) {
            if (candidate.is_absolute()) {
                if (fs::exists(candidate)) {
                    return candidate.string();
                }
                continue;
            }
            return candidate.string();
        }
        return "bha-refactor";
    }

    std::string format_application_summary(const bha::Suggestion& suggestion) {
        if (suggestion.application_summary && !suggestion.application_summary->empty()) {
            return *suggestion.application_summary;
        }
        switch (bha::resolve_application_mode(suggestion)) {
            case bha::SuggestionApplicationMode::DirectEdits:
                return "Auto-apply via direct text edits";
            case bha::SuggestionApplicationMode::ExternalRefactor:
                return "Auto-apply via external refactor tool";
            case bha::SuggestionApplicationMode::Advisory:
                return "Manual review only";
        }
        return "Manual review only";
    }

    bool has_external_refactor_payload(const bha::Suggestion& suggestion) {
        return suggestion.type == bha::SuggestionType::PIMPLPattern &&
               suggestion.refactor_class_name.has_value() &&
               suggestion.refactor_compile_commands_path.has_value() &&
               !suggestion.secondary_files.empty();
    }

    std::vector<fs::path> collect_unique_edit_files(const std::vector<bha::TextEdit>& edits) {
        std::vector<fs::path> files_to_backup;
        std::unordered_set<std::string> seen;
        files_to_backup.reserve(edits.size());
        for (const auto& edit : edits) {
            const fs::path file = edit.file;
            const std::string key = file.lexically_normal().generic_string();
            if (!seen.insert(key).second) {
                continue;
            }
            files_to_backup.push_back(file);
        }
        return files_to_backup;
    }

    struct SuggestionApplyCapabilities {
        bha::SuggestionApplicationMode mode = bha::SuggestionApplicationMode::Advisory;
        bool is_auto_applicable = false;
        bool has_bulk_apply_path = false;
    };

    SuggestionApplyCapabilities resolve_apply_capabilities(const bha::Suggestion& suggestion) {
        const auto mode = bha::resolve_application_mode(suggestion);
        const bool has_blocked_reason =
            suggestion.auto_apply_blocked_reason && !suggestion.auto_apply_blocked_reason->empty();

        SuggestionApplyCapabilities capabilities;
        capabilities.mode = mode;

        switch (mode) {
            case bha::SuggestionApplicationMode::DirectEdits:
                capabilities.is_auto_applicable = suggestion.is_safe && !suggestion.edits.empty();
                capabilities.has_bulk_apply_path = !suggestion.edits.empty();
                break;
            case bha::SuggestionApplicationMode::ExternalRefactor:
                if (!has_blocked_reason && has_external_refactor_payload(suggestion)) {
                    capabilities.is_auto_applicable = true;
                    capabilities.has_bulk_apply_path = true;
                }
                break;
            case bha::SuggestionApplicationMode::Advisory:
                break;
        }

        return capabilities;
    }

    bool is_auto_applicable_suggestion(const bha::Suggestion& suggestion) {
        return resolve_apply_capabilities(suggestion).is_auto_applicable;
    }

    bool has_bulk_apply_path(const bha::Suggestion& suggestion) {
        return resolve_apply_capabilities(suggestion).has_bulk_apply_path;
    }

    bool requires_compile_command_syntax_validation(const bha::Suggestion& suggestion) {
        switch (suggestion.type) {
            case bha::SuggestionType::ForwardDeclaration:
            case bha::SuggestionType::IncludeRemoval:
            case bha::SuggestionType::HeaderSplit:
            case bha::SuggestionType::ExplicitTemplate:
            case bha::SuggestionType::PCHOptimization:
                return true;
            default:
                return false;
        }
    }

    std::string validation_label_for_suggestion(const bha::Suggestion& suggestion) {
        switch (suggestion.type) {
            case bha::SuggestionType::ForwardDeclaration:
                return "Forward declaration";
            case bha::SuggestionType::IncludeRemoval:
                return "Include cleanup";
            case bha::SuggestionType::HeaderSplit:
                return "Header split";
            case bha::SuggestionType::ExplicitTemplate:
                return "Explicit template instantiation";
            case bha::SuggestionType::PCHOptimization:
                return "PCH";
            default:
                return "Suggestion";
        }
    }

    int parse_numeric_suggestion_id(const std::string& id) {
        if (!id.starts_with("ana-")) {
            return -1;
        }
        const std::string suffix = id.substr(4);
        if (suffix.empty()) {
            return -1;
        }
        try {
            return std::stoi(suffix);
        } catch (...) {
            return -1;
        }
    }

    std::string stable_suggestion_key(const bha::Suggestion& suggestion) {
        struct EditKey {
            std::string file;
            std::size_t start_line = 0;
            std::size_t start_col = 0;
            std::size_t end_line = 0;
            std::size_t end_col = 0;
            std::string new_text;
        };

        struct SecondaryFileKey {
            std::string path;
            int action = 0;
        };

        std::vector<EditKey> edits;
        edits.reserve(suggestion.edits.size());
        for (const auto& edit : suggestion.edits) {
            edits.push_back(EditKey{
                edit.file.lexically_normal().generic_string(),
                edit.start_line,
                edit.start_col,
                edit.end_line,
                edit.end_col,
                edit.new_text
            });
        }
        std::ranges::sort(edits, [](const EditKey& lhs, const EditKey& rhs) {
            if (lhs.file != rhs.file) return lhs.file < rhs.file;
            if (lhs.start_line != rhs.start_line) return lhs.start_line < rhs.start_line;
            if (lhs.start_col != rhs.start_col) return lhs.start_col < rhs.start_col;
            if (lhs.end_line != rhs.end_line) return lhs.end_line < rhs.end_line;
            if (lhs.end_col != rhs.end_col) return lhs.end_col < rhs.end_col;
            return lhs.new_text < rhs.new_text;
        });

        std::vector<SecondaryFileKey> secondary;
        secondary.reserve(suggestion.secondary_files.size());
        for (const auto& file : suggestion.secondary_files) {
            secondary.push_back(SecondaryFileKey{
                file.path.lexically_normal().generic_string(),
                static_cast<int>(file.action)
            });
        }
        std::ranges::sort(secondary, [](const SecondaryFileKey& lhs, const SecondaryFileKey& rhs) {
            if (lhs.path != rhs.path) return lhs.path < rhs.path;
            return lhs.action < rhs.action;
        });

        std::ostringstream key;
        const auto append_string = [&key](const std::string_view value) {
            key << value.size() << ':';
            key.write(value.data(), static_cast<std::streamsize>(value.size()));
        };

        key << "t:" << static_cast<int>(suggestion.type) << ';';
        key << "target:";
        append_string(suggestion.target_file.path.lexically_normal().generic_string());
        key << ':' << static_cast<int>(suggestion.target_file.action);
        for (const auto& edit : edits) {
            key << "|e:";
            append_string(edit.file);
            key << ':' << edit.start_line
                << ":" << edit.start_col
                << ":" << edit.end_line
                << ":" << edit.end_col
                << ':';
            append_string(edit.new_text);
        }
        for (const auto& file : secondary) {
            key << "|s:";
            append_string(file.path);
            key << ':' << file.action;
        }
        return key.str();
    }

    std::optional<bha::Priority> parse_priority_threshold(
        const std::optional<std::string>& min_priority
    ) {
        if (!min_priority) {
            return std::nullopt;
        }

        std::string prio_lower = *min_priority;
        std::ranges::transform(prio_lower, prio_lower.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (prio_lower == "critical") {
            return bha::Priority::Critical;
        }
        if (prio_lower == "high") {
            return bha::Priority::High;
        }
        if (prio_lower == "medium") {
            return bha::Priority::Medium;
        }
        if (prio_lower == "low") {
            return bha::Priority::Low;
        }
        return std::nullopt;
    }

    bool suggestion_meets_priority_threshold(
        const bha::Suggestion& suggestion,
        const std::optional<bha::Priority>& priority_threshold
    ) {
        if (!priority_threshold) {
            return true;
        }
        switch (*priority_threshold) {
            case bha::Priority::Low:
                return true;
            case bha::Priority::Medium:
                return suggestion.priority == bha::Priority::Medium ||
                    suggestion.priority == bha::Priority::High ||
                    suggestion.priority == bha::Priority::Critical;
            case bha::Priority::High:
                return suggestion.priority == bha::Priority::High ||
                    suggestion.priority == bha::Priority::Critical;
            case bha::Priority::Critical:
                return suggestion.priority == bha::Priority::Critical;
        }
        return false;
    }

    bool is_apply_all_candidate_enabled(
        const bha::Suggestion& suggestion,
        const std::optional<bha::Priority>& priority_threshold,
        const bool safe_only
    ) {
        // Bulk apply must only consider suggestions with a proven automatic apply path.
        // Suggestions that merely have concrete edits are not eligible unless they are
        // also marked auto-applicable by the shared capability resolver.
        if (!is_auto_applicable_suggestion(suggestion)) {
            return false;
        }
        (void)safe_only;
        return suggestion_meets_priority_threshold(suggestion, priority_threshold);
    }

    bool is_higher_ranked_suggestion(
        const std::string& lhs_id,
        const std::string& rhs_id,
        const std::map<std::string, bha::Suggestion>& suggestions
    ) {
        const auto lhs_it = suggestions.find(lhs_id);
        const auto rhs_it = suggestions.find(rhs_id);
        if (lhs_it == suggestions.end() || rhs_it == suggestions.end()) {
            return lhs_id < rhs_id;
        }
        const auto& lhs = lhs_it->second;
        const auto& rhs = rhs_it->second;
        if (lhs.priority != rhs.priority) {
            return lhs.priority < rhs.priority;
        }
        if (lhs.estimated_savings != rhs.estimated_savings) {
            return lhs.estimated_savings > rhs.estimated_savings;
        }
        if (lhs.confidence != rhs.confidence) {
            return lhs.confidence > rhs.confidence;
        }
        const int lhs_numeric = parse_numeric_suggestion_id(lhs_id);
        const int rhs_numeric = parse_numeric_suggestion_id(rhs_id);
        if (lhs_numeric >= 0 && rhs_numeric >= 0) {
            return lhs_numeric < rhs_numeric;
        }
        return lhs_id < rhs_id;
    }

    std::unordered_map<std::string, std::string> build_apply_all_candidate_index(
        const std::map<std::string, bha::Suggestion>& suggestions,
        const std::optional<bha::Priority>& priority_threshold,
        const bool safe_only
    ) {
        std::unordered_map<std::string, std::string> index;
        for (const auto& [id, suggestion] : suggestions) {
            if (!is_apply_all_candidate_enabled(suggestion, priority_threshold, safe_only)) {
                continue;
            }
            const std::string key = stable_suggestion_key(suggestion);
            if (auto it = index.find(key);
                it == index.end() || is_higher_ranked_suggestion(id, it->second, suggestions)) {
                index[key] = id;
            }
        }
        return index;
    }

    std::string format_application_guidance(const bha::Suggestion& suggestion) {
        if (suggestion.application_guidance && !suggestion.application_guidance->empty()) {
            return *suggestion.application_guidance;
        }
        switch (bha::resolve_application_mode(suggestion)) {
            case bha::SuggestionApplicationMode::DirectEdits:
                return "BHA will apply concrete text edits, then rebuild-validate the project.";
            case bha::SuggestionApplicationMode::ExternalRefactor:
                return "BHA will invoke bha-refactor for semantic rewrites, apply the returned replacements, then rebuild-validate the result.";
            case bha::SuggestionApplicationMode::Advisory:
                if (suggestion.type == bha::SuggestionType::PIMPLPattern) {
                    return "This class shape is outside the current automatic PIMPL subset. Review the suggestion and refactor it manually.";
                }
                return "This suggestion does not expose a safe automatic apply path. Review and apply it manually.";
        }
        return "This suggestion does not expose a safe automatic apply path. Review and apply it manually.";
    }

    std::optional<std::string> format_auto_apply_blocked_reason(const bha::Suggestion& suggestion) {
        if (suggestion.auto_apply_blocked_reason && !suggestion.auto_apply_blocked_reason->empty()) {
            return suggestion.auto_apply_blocked_reason;
        }
        if (bha::resolve_application_mode(suggestion) != bha::SuggestionApplicationMode::Advisory) {
            return std::nullopt;
        }
        if (suggestion.type == bha::SuggestionType::PIMPLPattern) {
            return "The target class is outside the current supported automatic PIMPL refactor subset.";
        }
        return "No safe automatic apply path is available for this suggestion.";
    }

    struct ExternalReplacement {
        fs::path file;
        std::size_t offset = 0;
        std::size_t length = 0;
        std::string replacement_text;
    };

    bool apply_replacements_to_file(
        const fs::path& file_path,
        std::vector<ExternalReplacement> replacements
    );

    std::optional<std::size_t> parse_size_t(const nlohmann::json& value) {
        if (value.is_number_unsigned()) {
            return value.get<std::size_t>();
        }
        if (value.is_number_integer()) {
            const auto parsed = value.get<long long>();
            if (parsed >= 0) {
                return static_cast<std::size_t>(parsed);
            }
        }
        return std::nullopt;
    }

    std::optional<bha::Priority> parse_priority(const nlohmann::json& value) {
        if (!value.is_string()) {
            return std::nullopt;
        }
        const std::string parsed = value.get<std::string>();
        if (parsed == "High") {
            return bha::Priority::High;
        }
        if (parsed == "Medium") {
            return bha::Priority::Medium;
        }
        if (parsed == "Low") {
            return bha::Priority::Low;
        }
        return std::nullopt;
    }

    std::optional<bha::FileAction> parse_file_action(const nlohmann::json& value) {
        if (!value.is_string()) {
            return std::nullopt;
        }
        const std::string parsed = value.get<std::string>();
        if (parsed == "CREATE") {
            return bha::FileAction::Create;
        }
        if (parsed == "MODIFY") {
            return bha::FileAction::Modify;
        }
        if (parsed == "ADD_INCLUDE") {
            return bha::FileAction::AddInclude;
        }
        if (parsed == "REMOVE") {
            return bha::FileAction::Remove;
        }
        return std::nullopt;
    }

    DiagnosticSeverity parse_refactor_severity(const nlohmann::json& value) {
        if (!value.is_string()) {
            return DiagnosticSeverity::Error;
        }
        std::string severity = value.get<std::string>();
        std::ranges::transform(
            severity,
            severity.begin(),
            [](const unsigned char c) { return static_cast<char>(std::tolower(c)); }
        );
        if (severity == "warning") {
            return DiagnosticSeverity::Warning;
        }
        if (severity == "note" || severity == "info" || severity == "information") {
            return DiagnosticSeverity::Information;
        }
        if (severity == "hint") {
            return DiagnosticSeverity::Hint;
        }
        return DiagnosticSeverity::Error;
    }

    Diagnostic parse_refactor_diagnostic(const nlohmann::json& value) {
        Diagnostic diag;
        diag.severity = DiagnosticSeverity::Error;
        diag.source = "bha-refactor";
        diag.message = value.value("message", "bha-refactor failed");

        if (value.contains("severity")) {
            diag.severity = parse_refactor_severity(value["severity"]);
        }
        if (value.contains("line")) {
            if (const auto line = parse_size_t(value["line"]); line.has_value() && *line > 0) {
                diag.range.start.line = static_cast<int>(*line - 1);
                diag.range.end.line = diag.range.start.line;
            }
        }
        if (value.contains("column")) {
            if (const auto col = parse_size_t(value["column"]); col.has_value() && *col > 0) {
                diag.range.start.character = static_cast<int>(*col - 1);
                diag.range.end.character = diag.range.start.character;
            }
        }
        return diag;
    }


    std::vector<fs::path> collect_compile_commands_sources(
        const fs::path& compile_commands_path,
        const std::size_t limit
    ) {
        std::vector<fs::path> sources;
        if (compile_commands_path.empty() || !fs::exists(compile_commands_path)) {
            return sources;
        }

        std::ifstream input(compile_commands_path);
        if (!input) {
            return sources;
        }

        nlohmann::json payload;
        try {
            input >> payload;
        } catch (const nlohmann::json::exception&) {
            return sources;
        }

        if (!payload.is_array()) {
            return sources;
        }

        std::unordered_set<std::string> seen;
        for (const auto& entry : payload) {
            auto source = compile_command_source_path_from_entry(entry);
            if (!source.has_value() || source->empty()) {
                continue;
            }
            const std::string key = source->generic_string();
            if (!seen.insert(key).second) {
                continue;
            }
            sources.push_back(std::move(*source));
            if (limit != 0 && sources.size() >= limit) {
                break;
            }
        }

        return sources;
    }


    SuggestionManager::SuggestionManager(const SuggestionManagerConfig& config)
        : config_(config)
    {
        suggestions::register_all_suggesters();
    }


    // Focused implementation units kept in one translation unit for shared helper linkage.
#include "suggestion_manager_analysis.inc"
#include "suggestion_manager_apply.inc"
#include "suggestion_manager_backup.inc"
#include "suggestion_manager_conversion.inc"
}
