#include "bha/lsp/suggestion_manager.hpp"
#include "bha/build_systems/adapter.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/suggestions/all_suggesters.hpp"
#include "bha/suggestions/suggester.hpp"
#include "bha/suggestions/consolidator.hpp"
#include "bha/suggestions/unreal_context.hpp"
#include "bha/lsp/uri.hpp"
#include "bha/utils/path_utils.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <regex>
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
#include <limits.h>
#endif
#endif

namespace bha::lsp
{
    namespace fs = std::filesystem;
    namespace path_utils = bha::path_utils;

    BuildSystemType build_system_type_from_adapter_name(std::string name) {
        std::ranges::transform(name, name.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name == "cmake") return BuildSystemType::CMake;
        if (name == "ninja") return BuildSystemType::Ninja;
        if (name == "make") return BuildSystemType::Make;
        if (name == "msbuild") return BuildSystemType::MSBuild;
        if (name == "bazel") return BuildSystemType::Bazel;
        if (name == "buck2") return BuildSystemType::Buck2;
        if (name == "meson") return BuildSystemType::Meson;
        if (name == "scons") return BuildSystemType::SCons;
        if (name == "xcode") return BuildSystemType::XCode;
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

    bool should_force_unreal_mode(
        const fs::path& project_root,
        const BuildTrace& trace
    ) {
        if (!project_root.empty()) {
            fs::path current = project_root;
            for (int hops = 0; hops < 6 && !current.empty(); ++hops) {
                if (suggestions::is_unreal_project_root(current)) {
                    return true;
                }
                const fs::path parent = current.parent_path();
                if (parent.empty() || parent == current) {
                    break;
                }
                current = parent;
            }
        }

        std::size_t checked = 0;
        for (const auto& unit : trace.units) {
            if (++checked > 20) {
                break;
            }
            fs::path current = unit.source_file.parent_path();
            while (!current.empty() && current.has_parent_path()) {
                if (suggestions::is_unreal_project_root(current)) {
                    return true;
                }
                const fs::path parent = current.parent_path();
                if (parent.empty() || parent == current) {
                    break;
                }
                current = parent;
            }
        }

        return false;
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
            const fs::path candidate = (project_root / raw_source).lexically_normal();
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
            if (line.rfind("#include", 0) == 0) {
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
        const fs::path candidate = project_root / path.filename();
        if (fs::exists(candidate)) {
            return candidate;
        }
        return path;
    }

    std::string resolve_clang_tidy_binary() {
        if (const char* env = std::getenv("BHA_CLANG_TIDY")) {
            fs::path path = env;
            if (!path.empty()) {
                return path.string();
            }
        }

        const std::array candidates{
            fs::path("/usr/bin/clang-tidy"),
            fs::path("/usr/local/bin/clang-tidy"),
            fs::path("clang-tidy")
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
        return "clang-tidy";
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
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (!is_compiler_wrapper(args[i])) {
                return args[i];
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

            std::optional<fs::path> directory;
            if (entry.contains("directory") && entry["directory"].is_string()) {
                directory = fs::path(entry["directory"].get<std::string>());
            }

            fs::path candidate;
            if (entry.contains("file") && entry["file"].is_string()) {
                candidate = entry["file"].get<std::string>();
            } else {
                continue;
            }

            const fs::path normalized_candidate = normalize_path_for_match(candidate, directory);
            if (normalized_candidate != needle && normalized_candidate.filename() != needle.filename()) {
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
                skip_next_value_flag("/Fo") || skip_next_value_flag("/Fe")) {
                if (i + 1 < args.size()) {
                    ++i;
                }
                continue;
            }

            if (arg == "-c" || arg == "-S" || arg == "-E" || arg == "/c" ||
                arg == "-Winvalid-pch" || arg == "-MD" || arg == "-MMD") {
                continue;
            }

            if (arg.starts_with("-o") || arg.starts_with("-MF") || arg.starts_with("-MT") ||
                arg.starts_with("-MQ") || arg.starts_with("-MJ") ||
                arg.starts_with("/Fo") || arg.starts_with("/Fe")) {
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
            command_args.push_back("/Zs");
        } else {
            command_args.push_back("-fsyntax-only");
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

    int run_command_collect_output(
        const std::string& command,
        const int timeout_seconds,
        std::string& output
    ) {
        output.clear();
        std::string effective_command = command;
#ifndef _WIN32
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
            if (output.size() > 1024 * 1024) {
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

    std::string truncate_for_diagnostic(const std::string& text, const std::size_t max_chars = 1200) {
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
        if (!parent.empty() && !sync_directory_to_disk(parent)) {
            return false;
        }
        return true;
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

    bool is_auto_applicable_suggestion(const bha::Suggestion& suggestion) {
        switch (bha::resolve_application_mode(suggestion)) {
            case bha::SuggestionApplicationMode::DirectEdits:
                return suggestion.is_safe && !suggestion.edits.empty();
            case bha::SuggestionApplicationMode::ExternalRefactor:
                if (suggestion.auto_apply_blocked_reason &&
                    !suggestion.auto_apply_blocked_reason->empty()) {
                    return false;
                }
                return has_external_refactor_payload(suggestion);
            case bha::SuggestionApplicationMode::Advisory:
                return false;
        }
        return false;
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

    bool is_unreal_module_rules_file(const fs::path& path) {
        return path.filename().string().ends_with(".Build.cs");
    }

    bool is_unreal_target_rules_file(const fs::path& path) {
        return path.filename().string().ends_with(".Target.cs");
    }

    bool is_unreal_suggestion(const bha::Suggestion& suggestion) {
        const std::string title_lower = [&]() {
            std::string t = suggestion.title;
            std::ranges::transform(t, t.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return t;
        }();
        if (title_lower.find("unreal") != std::string::npos) {
            return true;
        }
        if (is_unreal_module_rules_file(suggestion.target_file.path) ||
            is_unreal_target_rules_file(suggestion.target_file.path)) {
            return true;
        }
        return std::ranges::any_of(suggestion.secondary_files, [](const bha::FileTarget& file) {
            return is_unreal_module_rules_file(file.path) || is_unreal_target_rules_file(file.path);
        });
    }

    std::optional<std::string> infer_unreal_safety_guard(const bha::Suggestion& suggestion) {
        const auto blocked = format_auto_apply_blocked_reason(suggestion);
        if (!blocked.has_value()) {
            return std::nullopt;
        }

        std::string text = *blocked;
        std::ranges::transform(text, text.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (text.find("generated.h") != std::string::npos ||
            text.find("include-order") != std::string::npos) {
            return "generated-header-last-include";
        }
        if (text.find("constructor block") != std::string::npos) {
            return "rules-constructor-block-not-found";
        }
        if (text.find("ambiguous unreal module rules") != std::string::npos ||
            text.find("ambiguous unreal target rules") != std::string::npos ||
            text.find("duplicate unreal module") != std::string::npos ||
            text.find("duplicate unreal target") != std::string::npos) {
            return "ambiguous-rules-ownership";
        }
        if (text.find("uht") != std::string::npos) {
            return "uht-safety";
        }
        return "unreal-safety-guard";
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

    std::optional<bha::Suggestion> parse_include_removal_suggestion_json(const nlohmann::json& item) {
        if (!item.is_object()) {
            return std::nullopt;
        }
        if (!item.contains("type") || item["type"] != "Include Removal") {
            return std::nullopt;
        }

        bha::Suggestion suggestion;
        suggestion.type = bha::SuggestionType::IncludeRemoval;
        suggestion.id = item.value("id", "");
        suggestion.title = item.value("title", "");
        suggestion.description = item.value("description", "");
        suggestion.rationale = item.value("rationale", "");
        suggestion.confidence = item.value("confidence", 0.0);
        suggestion.is_safe = item.value("is_safe", false);
        suggestion.estimated_savings_percent = item.value("estimated_savings_percent", 0.0);
        if (item.contains("estimated_savings_ns")) {
            const auto ns = item.value("estimated_savings_ns", 0LL);
            if (ns > 0) {
                suggestion.estimated_savings = Duration(ns);
            }
        }
        if (item.contains("priority")) {
            if (auto priority = parse_priority(item["priority"]); priority.has_value()) {
                suggestion.priority = *priority;
            }
        }

        if (item.contains("target_file") && item["target_file"].is_object()) {
            const auto& target = item["target_file"];
            suggestion.target_file.path = target.value("path", "");
            suggestion.target_file.note = target.value("note", "");
            if (target.contains("line_start")) {
                if (auto value = parse_size_t(target["line_start"]); value.has_value()) {
                    suggestion.target_file.line_start = *value;
                }
            }
            if (target.contains("line_end")) {
                if (auto value = parse_size_t(target["line_end"]); value.has_value()) {
                    suggestion.target_file.line_end = *value;
                }
            }
            if (target.contains("action")) {
                if (auto action = parse_file_action(target["action"]); action.has_value()) {
                    suggestion.target_file.action = *action;
                }
            }
        }

        if (item.contains("edits") && item["edits"].is_array()) {
            for (const auto& edit_json : item["edits"]) {
                if (!edit_json.is_object()) {
                    continue;
                }
                bha::TextEdit edit;
                edit.file = edit_json.value("file", "");
                if (edit_json.contains("start_line")) {
                    if (auto value = parse_size_t(edit_json["start_line"]); value.has_value()) {
                        edit.start_line = *value;
                    }
                }
                if (edit_json.contains("start_col")) {
                    if (auto value = parse_size_t(edit_json["start_col"]); value.has_value()) {
                        edit.start_col = *value;
                    }
                }
                if (edit_json.contains("end_line")) {
                    if (auto value = parse_size_t(edit_json["end_line"]); value.has_value()) {
                        edit.end_line = *value;
                    }
                }
                if (edit_json.contains("end_col")) {
                    if (auto value = parse_size_t(edit_json["end_col"]); value.has_value()) {
                        edit.end_col = *value;
                    }
                }
                edit.new_text = edit_json.value("new_text", "");
                suggestion.edits.push_back(std::move(edit));
            }
        }

        if (item.contains("implementation_steps") && item["implementation_steps"].is_array()) {
            for (const auto& step : item["implementation_steps"]) {
                if (step.is_string()) {
                    suggestion.implementation_steps.push_back(step.get<std::string>());
                }
            }
        }

        if (item.contains("caveats") && item["caveats"].is_array()) {
            for (const auto& caveat : item["caveats"]) {
                if (caveat.is_string()) {
                    suggestion.caveats.push_back(caveat.get<std::string>());
                }
            }
        }

        if (item.contains("verification") && item["verification"].is_string()) {
            suggestion.verification = item["verification"].get<std::string>();
        }

        if (item.contains("impact") && item["impact"].is_object()) {
            const auto& impact = item["impact"];
            if (impact.contains("total_files_affected")) {
                if (auto files = parse_size_t(impact["total_files_affected"]); files.has_value()) {
                    suggestion.impact.total_files_affected = *files;
                }
            }
        } else {
            suggestion.impact.total_files_affected = suggestion.edits.size();
        }

        if (suggestion.edits.empty()) {
            return std::nullopt;
        }

        return suggestion;
    }

    std::vector<bha::Suggestion> load_include_removal_suggestions_via_cli(
        const fs::path& project_root,
        const fs::path& traces_dir
    ) {
        std::vector<bha::Suggestion> suggestions;
        if (project_root.empty() || traces_dir.empty() || !fs::exists(traces_dir)) {
            return suggestions;
        }

        const std::string cmd =
            "cd " + shell_quote(project_root.string()) + " && " +
            shell_quote(resolve_bha_cli_binary()) + " suggest " +
            shell_quote(traces_dir.string()) +
            " --format json --type include-removal --disable-consolidation --limit 500 2>&1";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return suggestions;
        }

        std::string output;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
            if (output.size() > 4 * 1024 * 1024) {
                break;
            }
        }
        pclose(pipe);

        const auto json_start = output.find('[');
        if (json_start == std::string::npos) {
            return suggestions;
        }

        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(output.substr(json_start));
        } catch (const nlohmann::json::exception&) {
            return suggestions;
        }

        if (!payload.is_array()) {
            return suggestions;
        }

        for (const auto& item : payload) {
            if (auto suggestion = parse_include_removal_suggestion_json(item)) {
                suggestions.push_back(std::move(*suggestion));
            }
        }

        return suggestions;
    }

    std::vector<fs::path> collect_trace_sources(
        const BuildTrace& build_trace,
        const fs::path& project_root,
        const std::size_t limit
    ) {
        std::vector<fs::path> sources;
        std::unordered_set<std::string> seen;
        for (const auto& unit : build_trace.units) {
            fs::path source = unit.source_file;
            if (source.is_relative() && !project_root.empty()) {
                fs::path candidate = (project_root / source).lexically_normal();
                if (fs::exists(candidate)) {
                    source = std::move(candidate);
                } else if (auto resolved = suggestions::find_file_in_repo(project_root, source.filename())) {
                    source = *resolved;
                } else {
                    source = std::move(candidate);
                }
            }
            source = source.lexically_normal();
            const auto ext = source.extension().string();
            if (ext != ".c" && ext != ".cc" && ext != ".cpp" && ext != ".cxx") {
                continue;
            }
            const std::string key = source.generic_string();
            if (seen.insert(key).second) {
                sources.push_back(std::move(source));
                if (sources.size() >= limit) {
                    break;
                }
            }
        }
        return sources;
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
            if (!entry.is_object() || !entry.contains("file") || !entry["file"].is_string()) {
                continue;
            }
            fs::path source = entry["file"].get<std::string>();
            source = source.lexically_normal();
            const std::string key = source.generic_string();
            if (!seen.insert(key).second) {
                continue;
            }
            sources.push_back(std::move(source));
            if (sources.size() >= limit) {
                break;
            }
        }

        return sources;
    }

    std::vector<bha::TextEdit> collect_verified_include_removal_edits(
        const fs::path& compile_commands_path,
        const fs::path& project_root,
        const BuildTrace& build_trace
    ) {
        std::vector<bha::TextEdit> edits;
        const fs::path build_dir = compile_commands_path.parent_path();
        auto sources = collect_compile_commands_sources(compile_commands_path, 25);
        if (sources.empty()) {
            sources = collect_trace_sources(build_trace, project_root, 25);
        }
        if (sources.empty()) {
            return edits;
        }

        const std::string clang_tidy = resolve_clang_tidy_binary();
        for (const auto& source : sources) {
            if (!fs::exists(source)) {
                continue;
            }

            const std::string cmd =
                shell_quote(clang_tidy) + " -checks=" + shell_quote("-*,misc-include-cleaner") +
                " -p " + shell_quote(build_dir.string()) +
                " " + shell_quote(source.string()) + " --quiet 2>&1";

            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) {
                continue;
            }

            std::string output;
            char buffer[4096];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                output += buffer;
                if (output.size() > 1024 * 1024) {
                    break;
                }
            }
            pclose(pipe);

            if (output.empty()) {
                continue;
            }

            std::istringstream stream(output);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.find("[misc-include-cleaner]") == std::string::npos ||
                    line.find("not used directly") == std::string::npos ||
                    line.find(": warning:") == std::string::npos) {
                    continue;
                }

                const auto first_colon = line.find(':');
                if (first_colon == std::string::npos) {
                    continue;
                }
                const auto second_colon = line.find(':', first_colon + 1);
                if (second_colon == std::string::npos) {
                    continue;
                }

                fs::path diag_file = line.substr(0, first_colon);
                std::error_code ec;
                if (diag_file.is_relative()) {
                    diag_file = fs::absolute(diag_file, ec);
                }
                const auto line_number = std::stoul(
                    line.substr(first_colon + 1, second_colon - first_colon - 1)
                );
                if (line_number == 0) {
                    continue;
                }

                const auto directives = suggestions::find_include_directives(diag_file);
                const auto it_dir = std::find_if(
                    directives.begin(),
                    directives.end(),
                    [&](const suggestions::IncludeDirective& include_dir) {
                        return include_dir.line + 1 == line_number;
                    }
                );
                if (it_dir == directives.end()) {
                    continue;
                }

                edits.push_back(suggestions::make_delete_line_edit(diag_file, it_dir->line));
            }
        }

        std::ranges::sort(
            edits,
            [](const bha::TextEdit& lhs, const bha::TextEdit& rhs) {
                if (lhs.file != rhs.file) {
                    return lhs.file < rhs.file;
                }
                return lhs.start_line < rhs.start_line;
            }
        );
        edits.erase(
            std::unique(
                edits.begin(),
                edits.end(),
                [](const bha::TextEdit& lhs, const bha::TextEdit& rhs) {
                    return lhs.file == rhs.file &&
                           lhs.start_line == rhs.start_line &&
                           lhs.end_line == rhs.end_line &&
                           lhs.new_text == rhs.new_text;
                }
            ),
            edits.end()
        );

        return edits;
    }

    std::optional<bha::Suggestion> build_verified_include_removal_suggestion(
        const fs::path& compile_commands_path,
        const fs::path& project_root,
        const BuildTrace& build_trace
    ) {
        auto edits = collect_verified_include_removal_edits(compile_commands_path, project_root, build_trace);
        if (edits.empty()) {
            return std::nullopt;
        }

        bha::Suggestion suggestion;
        suggestion.id = "";
        suggestion.type = bha::SuggestionType::IncludeRemoval;
        suggestion.priority = bha::Priority::High;
        suggestion.confidence = 0.98;
        suggestion.title = "Include Cleanup (" + std::to_string(edits.size()) + " includes)";
        suggestion.description = "clang-tidy misc-include-cleaner verified unused includes and generated explicit removals.";
        suggestion.rationale = "This suggestion is based on explicit semantic diagnostics from clang-tidy.";
        suggestion.target_file.path = edits.front().file;
        suggestion.target_file.line_start = edits.front().start_line + 1;
        suggestion.target_file.line_end = edits.front().end_line;
        suggestion.target_file.action = FileAction::Modify;
        suggestion.target_file.note = "Remove unused include confirmed by clang-tidy";
        suggestion.edits = std::move(edits);
        suggestion.impact.total_files_affected = suggestion.edits.size();
        suggestion.is_safe = true;
        suggestion.implementation_steps = {
            "Apply the explicit removals reported by clang-tidy misc-include-cleaner",
            "Rebuild and run tests"
        };
        suggestion.verification = "Compile all supported targets after applying the edits";
        return suggestion;
    }

    SuggestionManager::SuggestionManager(const SuggestionManagerConfig& config)
        : config_(config)
    {
        suggestions::register_all_suggesters();
    }

    void SuggestionManager::evict_old_backups() {
        // Evict by count limit
        while (config_.max_backups > 0 && backups_.size() > config_.max_backups && !backup_lru_.empty()) {
            const auto oldest_id = backup_lru_.front();
            backup_lru_.pop_front();
            if (config_.use_disk_backups && !config_.workspace_root.empty()) {
                cleanup_disk_backup(oldest_id);
            }
            backups_.erase(oldest_id);
        }

        // Evict by size limit
        while (config_.max_backup_bytes > 0 && calculate_backup_size() > config_.max_backup_bytes && !backup_lru_.empty()) {
            const auto oldest_id = backup_lru_.front();
            backup_lru_.pop_front();
            if (config_.use_disk_backups && !config_.workspace_root.empty()) {
                cleanup_disk_backup(oldest_id);
            }
            backups_.erase(oldest_id);
        }
    }

    void SuggestionManager::evict_old_analysis_cache() {
        while (config_.max_analysis_cache > 0 && analysis_cache_.size() > config_.max_analysis_cache && !analysis_lru_.empty()) {
            const auto oldest_id = analysis_lru_.front();
            analysis_lru_.pop_front();
            analysis_cache_.erase(oldest_id);
        }
    }

    std::size_t SuggestionManager::calculate_backup_size() const {
        std::size_t total = 0;
        for (const auto& backup : backups_ | std::views::values) {
            for (const auto& [path, content] : backup.files) {
                total += content.size();
            }
        }
        return total;
    }

    AnalysisResult SuggestionManager::analyze_project(
        const fs::path& project_root,
        const std::optional<fs::path>& build_dir,
        bool rebuild,
        const ProgressCallback& on_progress,
        const AnalyzeSuggestionOptions& analyze_options
    ) {
        auto start = std::chrono::steady_clock::now();

        auto report = [&on_progress](const std::string& msg, const int pct) {
            if (on_progress) on_progress(msg, pct);
        };

        report("Detecting build system...", 5);

        auto& registry = build_systems::BuildSystemRegistry::instance();
        auto* adapter = registry.detect(project_root);

        if (!adapter) {
            throw std::runtime_error("Could not detect build system");
        }

        build_systems::BuildOptions options;
        options.enable_tracing = true;
        if (build_dir) {
            options.build_dir = *build_dir;
        }

        std::optional<fs::path> compile_commands_path;
        if (build_dir) {
            const fs::path direct_compile_commands = *build_dir / "compile_commands.json";
            if (fs::exists(direct_compile_commands)) {
                compile_commands_path = direct_compile_commands;
            }
        }

        if (rebuild) {
            report("Running build...", 10);
            if (auto build_result = adapter->build(project_root, options); !build_result.is_ok() || !build_result.value().success) {
                throw std::runtime_error("Build failed");
            }
        }

        report("Loading compile commands...", 20);
        if (auto compile_commands_result = adapter->get_compile_commands(project_root, options); !compile_commands_result.is_ok()) {
            if (!config_.allow_missing_compile_commands) {
                throw std::runtime_error("Could not find compile_commands.json");
            }
        } else {
            compile_commands_path = compile_commands_result.value();
        }

        report("Parsing trace files...", 30);
        BuildTrace build_trace;
        build_trace.timestamp = std::chrono::system_clock::now();
        build_trace.build_system = detect_build_system_from_build_dir(build_dir);
        if (build_trace.build_system == BuildSystemType::Unknown && adapter) {
            build_trace.build_system = build_system_type_from_adapter_name(adapter->name());
        }

        fs::path traces_dir = build_dir.value_or(project_root / "build");
        if (build_dir) {
            const fs::path sibling_traces = build_dir->parent_path() / "traces";
            if (fs::exists(sibling_traces)) {
                traces_dir = sibling_traces;
            }
        }
        int files_analyzed = 0;

        auto should_skip_unit = [](const fs::path& source_path) {
            const auto filename = source_path.filename().string();
            if (filename.find("cmake_pch") != std::string::npos) {
                return true;
            }
            if (filename.find("CMakeCXXCompilerId") != std::string::npos) {
                return true;
            }
            if (filename.find("CompilerId") != std::string::npos) {
                return true;
            }
            return false;
        };

        if (fs::exists(traces_dir)) {
            for (const auto& trace_file : parsers::collect_trace_files(traces_dir, true)) {
                if (auto parse_result = parsers::parse_trace_file(trace_file); parse_result.is_ok()) {
                    auto unit = std::move(parse_result.value());
                    if (should_skip_unit(unit.source_file)) {
                        continue;
                    }
                    build_trace.total_time += unit.metrics.total_time;
                    build_trace.units.push_back(std::move(unit));
                    files_analyzed++;
                }
            }
        }

        if (build_trace.units.empty()) {
            throw std::runtime_error("No trace files found");
        }

        if (!project_root.empty()) {
            std::unordered_map<std::string, std::optional<fs::path>> resolve_cache;
            resolve_cache.reserve(build_trace.units.size() * 2);

            for (auto& unit : build_trace.units) {
                if (path_utils::is_under(unit.source_file, traces_dir)) {
                    if (auto resolved = resolve_trace_file_to_project(project_root, unit.source_file, resolve_cache)) {
                        unit.source_file = *resolved;
                        unit.metrics.path = unit.source_file;
                    }
                }

                for (auto& inc : unit.includes) {
                    if (path_utils::is_under(inc.header, traces_dir)) {
                        if (auto resolved = resolve_trace_file_to_project(project_root, inc.header, resolve_cache)) {
                            inc.header = *resolved;
                        }
                    }
                }
            }
        }

        if (compile_commands_path.has_value()) {
            auto compile_sources = collect_compile_commands_sources(*compile_commands_path, 10000);
            if (!compile_sources.empty()) {
                const auto by_filename = index_sources_by_filename(compile_sources);
                for (auto& unit : build_trace.units) {
                    if (unit.source_file.is_relative() || !fs::exists(unit.source_file)) {
                        if (auto resolved = resolve_trace_source_with_compile_commands(
                            unit.source_file,
                            project_root,
                            by_filename
                        )) {
                            unit.source_file = *resolved;
                            unit.metrics.path = *resolved;
                        }
                    }
                }
            }
        }

        report("Running analyzers...", 50);

        AnalysisOptions analysis_opts;
        auto analysis_result = analyzers::run_full_analysis(build_trace, analysis_opts);

        if (!analysis_result.is_ok()) {
            throw std::runtime_error("Analysis failed: " + analysis_result.error().message());
        }

        report("Generating suggestions...", 70);

        // Configure suggester options
        SuggesterOptions suggester_opts;
        suggester_opts.min_confidence = config_.min_confidence;
        suggester_opts.include_unsafe = config_.include_unsafe_suggestions;
        suggester_opts.enable_consolidation = true;
        suggester_opts.compile_commands_path = compile_commands_path;
        if (!analyze_options.enabled_types.empty()) {
            suggester_opts.enabled_types = analyze_options.enabled_types;
        }
        if (analyze_options.include_unsafe.has_value()) {
            suggester_opts.include_unsafe = *analyze_options.include_unsafe;
        }
        if (analyze_options.min_confidence.has_value()) {
            suggester_opts.min_confidence = *analyze_options.min_confidence;
        }
        if (analyze_options.enable_consolidation.has_value()) {
            suggester_opts.enable_consolidation = *analyze_options.enable_consolidation;
        }
        if (analyze_options.relax_heuristics) {
            auto& pch = suggester_opts.heuristics.pch;
            auto& templates = suggester_opts.heuristics.templates;
            auto& headers = suggester_opts.heuristics.headers;
            auto& unity = suggester_opts.heuristics.unity_build;
            auto& forward_decl = suggester_opts.heuristics.forward_decl;
            auto& codegen = suggester_opts.heuristics.codegen;
            auto& unreal = suggester_opts.heuristics.unreal;
            pch.min_include_count = 1;
            pch.min_aggregate_time = std::chrono::milliseconds(1);
            templates.min_instantiation_count = 1;
            templates.min_total_time = std::chrono::milliseconds(1);
            headers.min_parse_time = std::chrono::milliseconds(1);
            headers.min_includers_for_split = 1;
            unity.min_files_threshold = 2;
            unity.min_group_total_time = std::chrono::milliseconds(1);
            forward_decl.min_parse_time = std::chrono::milliseconds(1);
            codegen.long_codegen_threshold = std::chrono::milliseconds(1);
            unreal.min_module_files_for_unity = 1;
            unreal.min_module_include_time_for_pch = std::chrono::milliseconds(1);
        }
        if (should_force_unreal_mode(project_root, build_trace)) {
            suggester_opts.heuristics.unreal.enabled = true;
        }

        // Generate suggestions using all registered suggesters
        auto suggestions_result = suggestions::generate_all_suggestions(
            build_trace,
            analysis_result.value(),
            suggester_opts,
            project_root
        );

        if (!suggestions_result.is_ok()) {
            throw std::runtime_error("Suggestion generation failed: " + suggestions_result.error().message());
        }

        auto bha_suggestions = std::move(suggestions_result.value());

        bool has_include_removal = std::ranges::any_of(
            bha_suggestions,
            [](const bha::Suggestion& suggestion) {
                return suggestion.type == bha::SuggestionType::IncludeRemoval;
            }
        );
        if (!has_include_removal) {
            auto cli_include_removals = load_include_removal_suggestions_via_cli(project_root, traces_dir);
            if (!cli_include_removals.empty()) {
                for (auto& suggestion : cli_include_removals) {
                    bha_suggestions.push_back(std::move(suggestion));
                }
                has_include_removal = true;
            }
        }
        if (!has_include_removal && compile_commands_path.has_value()) {
            if (auto verified_include = build_verified_include_removal_suggestion(*compile_commands_path, project_root, build_trace)) {
                bha_suggestions.push_back(std::move(*verified_include));
            }
        }

        if (!project_root.empty()) {
            for (auto& sug : bha_suggestions) {
                sug.target_file.path = resolve_relative_path(sug.target_file.path, project_root);
                for (auto& secondary : sug.secondary_files) {
                    secondary.path = resolve_relative_path(secondary.path, project_root);
                }
                for (auto& edit : sug.edits) {
                    edit.file = resolve_relative_path(edit.file, project_root);
                    if (edit.file.filename() == "pch.h" && !edit.new_text.empty()) {
                        edit.new_text = normalize_pch_text(edit.new_text);
                    }
                }

                if (is_build_system_file(sug.target_file.path)) {
                    sug.target_file.path = remap_build_system_path(sug.target_file.path, project_root);
                }
                for (auto& secondary : sug.secondary_files) {
                    if (is_build_system_file(secondary.path)) {
                        secondary.path = remap_build_system_path(secondary.path, project_root);
                    }
                }
                for (auto& edit : sug.edits) {
                    if (is_build_system_file(edit.file)) {
                        edit.file = remap_build_system_path(edit.file, project_root);
                    }
                }
            }
        }

        // Convert bha::Suggestion to lsp::Suggestion
        suggestions_.clear();
        bha_suggestions_.clear();
        std::vector<Suggestion> lsp_suggestions;

        for (auto& bha_sug : bha_suggestions) {

            std::string sug_id = generate_analysis_id();
            bha_sug.id = sug_id;

            bha_suggestions_[sug_id] = bha_sug;
            auto lsp_sug = convert_suggestion(bha_sug);
            lsp_suggestions.push_back(lsp_sug);

            suggestions_[sug_id] = convert_to_detailed(bha_sug);
        }

        auto end = std::chrono::steady_clock::now();
        auto duration_count = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        int duration_ms = static_cast<int>(duration_count);

        std::string analysis_id = generate_analysis_id();
        last_analysis_id_ = analysis_id;
        analysis_cache_[analysis_id] = std::move(build_trace);
        analysis_lru_.push_back(analysis_id);
        if (compile_commands_path.has_value()) {
            fs::path compile_commands = *compile_commands_path;
            if (compile_commands.is_relative() && !project_root.empty()) {
                compile_commands = (project_root / compile_commands).lexically_normal();
            } else {
                compile_commands = compile_commands.lexically_normal();
            }
            last_compile_commands_path_ = compile_commands;
        } else {
            last_compile_commands_path_ = std::nullopt;
        }
        last_project_root_ = project_root;
        last_build_dir_ = build_dir;
        last_analyze_options_ = analyze_options;

        // Evict old analysis cache entries if over limit
        evict_old_analysis_cache();

        report("Finalizing results...", 95);

        AnalysisResult result;
        result.analysis_id = analysis_id;
        result.suggestions = std::move(lsp_suggestions);
        result.baseline_metrics = extract_build_metrics(analysis_cache_[analysis_id]);
        result.files_analyzed = files_analyzed;
        result.duration_ms = duration_ms;

        report("Analysis complete", 100);

        return result;
    }

    DetailedSuggestion SuggestionManager::get_suggestion_details(const std::string& suggestion_id) {
        const auto it = suggestions_.find(suggestion_id);
        if (it == suggestions_.end()) {
            throw std::runtime_error("Invalid suggestion ID: " + suggestion_id);
        }
        return it->second;
    }

    bool SuggestionManager::validate_forward_decl_suggestion(
        const bha::Suggestion& suggestion,
        const std::vector<fs::path>& changed_files,
        std::vector<Diagnostic>& errors
    ) const {
        if (!config_.enforce_forward_decl_syntax_gate) {
            return true;
        }

        if (!last_compile_commands_path_.has_value() || !fs::exists(*last_compile_commands_path_)) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message =
                "Forward declaration apply blocked: compile_commands.json is unavailable for syntax validation";
            errors.push_back(std::move(diag));
            return false;
        }

        if (last_analysis_id_.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message =
                "Forward declaration apply blocked: no prior analysis context is available for validation";
            errors.push_back(std::move(diag));
            return false;
        }

        const auto analysis_it = analysis_cache_.find(last_analysis_id_);
        if (analysis_it == analysis_cache_.end()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message =
                "Forward declaration apply blocked: cached analysis trace is no longer available";
            errors.push_back(std::move(diag));
            return false;
        }

        std::unordered_set<std::string> touched_paths;
        const auto add_touched = [&touched_paths](const fs::path& path) {
            const fs::path normalized = normalize_path_for_match(path);
            if (!normalized.empty()) {
                touched_paths.insert(normalized.generic_string());
            }
        };

        for (const auto& file : changed_files) {
            add_touched(file);
        }
        add_touched(suggestion.target_file.path);
        for (const auto& secondary : suggestion.secondary_files) {
            add_touched(secondary.path);
        }

        if (touched_paths.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message =
                "Forward declaration apply blocked: could not determine affected files for validation";
            errors.push_back(std::move(diag));
            return false;
        }

        std::vector<fs::path> candidate_sources;
        std::unordered_set<std::string> seen_sources;
        for (const auto& unit : analysis_it->second.units) {
            const fs::path source = normalize_path_for_match(unit.source_file);
            if (source.empty() || !is_cpp_source_path(source)) {
                continue;
            }

            bool impacted = touched_paths.contains(source.generic_string());
            if (!impacted) {
                for (const auto& include : unit.includes) {
                    const fs::path header = normalize_path_for_match(include.header);
                    if (!header.empty() && touched_paths.contains(header.generic_string())) {
                        impacted = true;
                        break;
                    }
                }
            }

            if (!impacted) {
                continue;
            }

            const std::string key = source.generic_string();
            if (seen_sources.insert(key).second) {
                candidate_sources.push_back(source);
            }
        }

        if (candidate_sources.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message =
                "Forward declaration apply blocked: no affected translation units were found for syntax validation";
            errors.push_back(std::move(diag));
            return false;
        }

        const std::size_t validation_cap = config_.max_forward_decl_validation_units == 0
            ? candidate_sources.size()
            : std::min(candidate_sources.size(), config_.max_forward_decl_validation_units);

        std::regex diagnostic_regex(R"(([^:]+):(\d+):(\d+):\s*(error|warning):\s*(.*))");
        for (std::size_t i = 0; i < validation_cap; ++i) {
            const fs::path& source = candidate_sources[i];
            auto compile_args = load_compile_command_args_for_source(last_compile_commands_path_, source);
            if (compile_args.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Forward declaration apply blocked: no compile command found for " + source.string();
                errors.push_back(std::move(diag));
                return false;
            }

            const auto syntax_cmd = build_syntax_check_command(compile_args, source);
            if (!syntax_cmd.has_value()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Forward declaration apply blocked: failed to construct syntax check command for " +
                    source.string();
                errors.push_back(std::move(diag));
                return false;
            }

            std::string syntax_output;
            const int exit_code = run_command_collect_output(
                *syntax_cmd,
                config_.forward_decl_validation_timeout_seconds,
                syntax_output
            );

            if (exit_code == 0) {
                continue;
            }

            bool emitted_compiler_diag = false;
            std::smatch match;
            auto search_start = syntax_output.cbegin();
            while (std::regex_search(search_start, syntax_output.cend(), match, diagnostic_regex)) {
                Diagnostic diag;
                diag.range.start.line = std::stoi(match[2]) - 1;
                diag.range.start.character = std::stoi(match[3]) - 1;
                diag.range.end = diag.range.start;
                diag.severity = (match[4] == "error") ? DiagnosticSeverity::Error : DiagnosticSeverity::Warning;
                diag.source = "compiler";
                diag.message = match[5];
                errors.push_back(std::move(diag));
                emitted_compiler_diag = true;
                search_start = match.suffix().first;
            }

            if (!emitted_compiler_diag) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                if (config_.forward_decl_validation_timeout_seconds > 0 && exit_code == 124) {
                    diag.message = "Forward declaration syntax validation timed out for " + source.string();
                } else if (!syntax_output.empty()) {
                    diag.message = "Forward declaration syntax validation failed for " + source.string() +
                        ":\n" + truncate_for_diagnostic(syntax_output);
                } else if (exit_code == -1) {
                    diag.message = "Forward declaration syntax validation failed to launch compiler process";
                } else {
                    diag.message = "Forward declaration syntax validation failed for " + source.string() +
                        " with exit code " + std::to_string(exit_code);
                }
                errors.push_back(std::move(diag));
            }

            return false;
        }

        return true;
    }

    ApplySuggestionResult SuggestionManager::apply_suggestion(
        const std::string& suggestion_id,
        bool /*skip_validation*/,
        bool skip_rebuild,
        bool create_backup_flag
    ) {
        ApplySuggestionResult result;
        result.success = false;

        auto bha_it = bha_suggestions_.find(suggestion_id);
        if (bha_it == bha_suggestions_.end()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Invalid suggestion ID: " + suggestion_id;
            result.errors.push_back(diag);
            return result;
        }

        const auto& bha_sug = bha_it->second;
        const auto application_mode = bha::resolve_application_mode(bha_sug);
        const bool enforce_forward_decl_validation =
            bha_sug.type == bha::SuggestionType::ForwardDeclaration &&
            config_.enforce_forward_decl_syntax_gate;

        std::vector<FileBackup> transactional_snapshot;
        const auto capture_transactional_snapshot = [&](const std::vector<fs::path>& files) -> bool {
            std::unordered_set<std::string> seen;
            for (const auto& file : files) {
                const fs::path normalized = file.lexically_normal();
                const std::string key = normalized.generic_string();
                if (!seen.insert(key).second) {
                    continue;
                }

                std::ifstream in(normalized, std::ios::binary);
                if (!in) {
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.source = "bha-lsp";
                    diag.message = "Failed to capture rollback snapshot for " + normalized.string();
                    result.errors.push_back(std::move(diag));
                    return false;
                }

                transactional_snapshot.push_back(FileBackup{
                    normalized,
                    std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>())
                });
            }
            return true;
        };
        const auto restore_transactional_snapshot = [&]() -> bool {
            bool success = true;
            for (const auto& backup : transactional_snapshot) {
                try {
                    if (const fs::path parent = backup.path.parent_path(); !parent.empty()) {
                        fs::create_directories(parent);
                    }
                    std::ofstream out(backup.path, std::ios::binary | std::ios::trunc);
                    if (!out) {
                        success = false;
                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.source = "bha-lsp";
                        diag.message = "Failed to restore snapshot file: " + backup.path.string();
                        result.errors.push_back(std::move(diag));
                        continue;
                    }
                    out << backup.content;
                } catch (const std::exception& e) {
                    success = false;
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.source = "bha-lsp";
                    diag.message = "Exception while restoring snapshot for " + backup.path.string() + ": " + e.what();
                    result.errors.push_back(std::move(diag));
                }
            }
            return success;
        };

        std::vector<fs::path> changed_files;
        if (application_mode == bha::SuggestionApplicationMode::ExternalRefactor) {
            if (bha_sug.type != bha::SuggestionType::PIMPLPattern ||
                !bha_sug.refactor_class_name ||
                !bha_sug.refactor_compile_commands_path ||
                bha_sug.secondary_files.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "This external refactor suggestion is missing the metadata required to invoke bha-refactor";
                result.errors.push_back(diag);
                return result;
            }

            const fs::path refactor_binary = resolve_bha_refactor_binary();
            if (!fs::exists(refactor_binary)) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "bha-refactor binary was not found; build with BHA_BUILD_REFACTOR_TOOLS=ON or set BHA_REFACTOR";
                result.errors.push_back(diag);
                return result;
            }

            std::ostringstream cmd;
            cmd << shell_quote(refactor_binary.string())
                << " pimpl"
                << " --compile-commands " << shell_quote(bha_sug.refactor_compile_commands_path->string())
                << " --source " << shell_quote(bha_sug.target_file.path.string())
                << " --header " << shell_quote(bha_sug.secondary_files.front().path.string())
                << " --class " << shell_quote(*bha_sug.refactor_class_name)
                << " --dry-run --output-format json";

            std::array<char, 512> buffer{};
            std::string output;
            if (FILE* pipe = popen(cmd.str().c_str(), "r")) {
                while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                    output.append(buffer.data());
                }
                pclose(pipe);
            } else {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "Failed to launch bha-refactor";
                result.errors.push_back(diag);
                return result;
            }

            nlohmann::json refactor_json;
            try {
                refactor_json = nlohmann::json::parse(output);
            } catch (const nlohmann::json::exception&) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "bha-refactor returned invalid JSON";
                result.errors.push_back(diag);
                return result;
            }

            if (!refactor_json.value("success", false)) {
                if (refactor_json.contains("diagnostics") && refactor_json["diagnostics"].is_array()) {
                    for (const auto& item : refactor_json["diagnostics"]) {
                        result.errors.push_back(parse_refactor_diagnostic(item));
                    }
                }
                if (result.errors.empty()) {
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.message = "bha-refactor did not produce an applicable refactor";
                    diag.source = "bha-refactor";
                    result.errors.push_back(diag);
                }
                return result;
            }

            std::unordered_map<std::string, std::vector<ExternalReplacement>> replacements_by_file;
            std::vector<fs::path> files_to_backup;
            if (refactor_json.contains("replacements") && refactor_json["replacements"].is_array()) {
                for (const auto& item : refactor_json["replacements"]) {
                    if (!item.is_object()) {
                        continue;
                    }
                    ExternalReplacement replacement;
                    replacement.file = item.value("file", "");
                    replacement.offset = item.value("offset", std::size_t{0});
                    replacement.length = item.value("length", std::size_t{0});
                    replacement.replacement_text = item.value("replacement_text", "");
                    if (replacement.file.empty()) {
                        continue;
                    }
                    if (!replacements_by_file.contains(replacement.file.string())) {
                        files_to_backup.push_back(replacement.file);
                    }
                    replacements_by_file[replacement.file.string()].push_back(std::move(replacement));
                }
            }

            if (replacements_by_file.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "bha-refactor returned no replacements";
                result.errors.push_back(diag);
                return result;
            }

            if (create_backup_flag && !files_to_backup.empty()) {
                const std::string backup_id = create_backup(files_to_backup);
                if (backup_id.empty()) {
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.source = "bha-lsp";
                    diag.message = "Failed to create durable backup before applying external refactor suggestion";
                    result.errors.push_back(std::move(diag));
                    return result;
                }
                result.backup_id = backup_id;
            }

            for (auto& [file_path_str, file_replacements] : replacements_by_file) {
                const fs::path file_path(file_path_str);
                if (!apply_replacements_to_file(file_path, std::move(file_replacements))) {
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.message = "Failed to apply bha-refactor replacements to " + file_path.string();
                    diag.source = "bha-refactor";
                    result.errors.push_back(diag);
                    if (result.backup_id) {
                        if (auto revert = revert_changes_detailed(*result.backup_id); !revert.success) {
                            for (auto& revert_error : revert.errors) {
                                result.errors.push_back(std::move(revert_error));
                            }
                            Diagnostic rollback_diag;
                            rollback_diag.severity = DiagnosticSeverity::Error;
                            rollback_diag.message = "Automatic rollback failed after external refactor apply failure";
                            rollback_diag.source = "bha-lsp";
                            result.errors.push_back(std::move(rollback_diag));
                        }
                    }
                    return result;
                }
                changed_files.push_back(file_path);
            }
        } else {
            if (bha_sug.type == bha::SuggestionType::PIMPLPattern && bha_sug.edits.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "PIMPL suggestions without concrete edits remain advisory-only";
                result.errors.push_back(diag);
                return result;
            }

            std::vector<fs::path> files_to_backup;
            if (bha_sug.target_file.action == FileAction::Modify ||
                bha_sug.target_file.action == FileAction::AddInclude) {
                files_to_backup.push_back(bha_sug.target_file.path);
            }
            for (const auto& secondary : bha_sug.secondary_files) {
                if (secondary.action == bha::FileAction::Modify ||
                    secondary.action == bha::FileAction::AddInclude) {
                    files_to_backup.push_back(secondary.path);
                }
            }

            if (create_backup_flag && !files_to_backup.empty()) {
                const std::string backup_id = create_backup(files_to_backup);
                if (backup_id.empty()) {
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.source = "bha-lsp";
                    diag.message = "Failed to create durable backup before applying suggestion " + suggestion_id;
                    result.errors.push_back(std::move(diag));
                    return result;
                }
                result.backup_id = backup_id;
            }
            if (enforce_forward_decl_validation && !files_to_backup.empty() &&
                !capture_transactional_snapshot(files_to_backup)) {
                return result;
            }

            if (!apply_file_changes(bha_sug, changed_files)) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "Failed to apply file changes for suggestion " + suggestion_id +
                               " (" + bha_sug.title + ")";
                result.errors.push_back(diag);

                if (result.backup_id) {
                    if (auto revert = revert_changes_detailed(*result.backup_id); !revert.success) {
                        for (auto& revert_error : revert.errors) {
                            result.errors.push_back(std::move(revert_error));
                        }
                        Diagnostic rollback_diag;
                        rollback_diag.severity = DiagnosticSeverity::Error;
                        rollback_diag.message = "Automatic rollback failed after apply failure";
                        rollback_diag.source = "bha-lsp";
                        result.errors.push_back(std::move(rollback_diag));
                    }
                } else if (enforce_forward_decl_validation && !transactional_snapshot.empty()) {
                    if (!restore_transactional_snapshot()) {
                        Diagnostic rollback_diag;
                        rollback_diag.severity = DiagnosticSeverity::Error;
                        rollback_diag.message =
                            "Automatic rollback failed after forward-declaration apply failure";
                        rollback_diag.source = "bha-lsp";
                        result.errors.push_back(std::move(rollback_diag));
                    }
                }
                return result;
            }
        }

        if (enforce_forward_decl_validation &&
            !validate_forward_decl_suggestion(bha_sug, changed_files, result.errors)) {
            bool rollback_success = !result.backup_id.has_value() && transactional_snapshot.empty();
            if (result.backup_id) {
                const auto rollback = revert_changes_detailed(*result.backup_id);
                rollback_success = rollback.success;
                if (!rollback.success) {
                    result.errors.insert(result.errors.end(), rollback.errors.begin(), rollback.errors.end());
                }
            } else if (!transactional_snapshot.empty()) {
                rollback_success = restore_transactional_snapshot();
            }

            if (!rollback_success) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message =
                    "Forward declaration syntax validation failed and automatic rollback did not complete";
                result.errors.push_back(std::move(diag));
            }
            return result;
        }

        for (const auto& file : changed_files) {
            result.changed_files.push_back(file.string());
        }

        if (!skip_rebuild && !last_analysis_id_.empty()) {
            auto& analysis = analysis_cache_[last_analysis_id_];
            auto& registry = build_systems::BuildSystemRegistry::instance();

            if (!analysis.units.empty()) {
                const fs::path source_dir = analysis.units[0].source_file.parent_path();
                const auto project_root = detect_project_root_with_registered_adapters(source_dir, registry);
                build_systems::IBuildSystemAdapter* adapter =
                    project_root.has_value() ? registry.detect(*project_root) : nullptr;

                if (project_root.has_value() && adapter != nullptr) {
                    build_systems::BuildOptions options;

                    if (auto build_result = adapter->build(*project_root, options); build_result.is_ok() && build_result.value().success) {
                        BuildResult lsp_build_result;
                        lsp_build_result.success = true;
                        result.build_result = lsp_build_result;
                    } else {
                        BuildResult lsp_build_result;
                        lsp_build_result.success = false;
                        result.build_result = lsp_build_result;

                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.message = "Build failed after applying suggestion";
                        result.errors.push_back(diag);

                        if (result.backup_id) {
                            if (auto revert = revert_changes_detailed(*result.backup_id); !revert.success) {
                                for (auto& revert_error : revert.errors) {
                                    result.errors.push_back(std::move(revert_error));
                                }
                                Diagnostic rollback_diag;
                                rollback_diag.severity = DiagnosticSeverity::Error;
                                rollback_diag.message = "Automatic rollback failed after build failure";
                                rollback_diag.source = "bha-lsp";
                                result.errors.push_back(std::move(rollback_diag));
                            }
                        }
                        return result;
                    }
                }
            }
        }

        result.success = true;
        return result;
    }

    ApplySuggestionResult SuggestionManager::apply_edit_bundle(
        const std::vector<bha::TextEdit>& edits,
        const bool create_backup_flag
    ) {
        ApplySuggestionResult result;
        result.success = false;

        if (edits.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "No edits provided";
            result.errors.push_back(std::move(diag));
            return result;
        }

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

        if (create_backup_flag && !files_to_backup.empty()) {
            const std::string backup_id = create_backup(files_to_backup);
            if (backup_id.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Failed to create durable backup before applying edit bundle";
                result.errors.push_back(std::move(diag));
                return result;
            }
            result.backup_id = backup_id;
        }

        bha::Suggestion synthetic;
        synthetic.title = "Direct text edit bundle";
        synthetic.type = bha::SuggestionType::IncludeRemoval;
        synthetic.is_safe = true;
        synthetic.edits = edits;
        synthetic.application_mode = bha::SuggestionApplicationMode::DirectEdits;

        std::vector<fs::path> changed_files;
        if (!apply_file_changes(synthetic, changed_files)) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Failed to apply edit bundle";
            result.errors.push_back(std::move(diag));
            if (result.backup_id) {
                if (auto revert = revert_changes_detailed(*result.backup_id); !revert.success) {
                    for (auto& revert_error : revert.errors) {
                        result.errors.push_back(std::move(revert_error));
                    }
                    Diagnostic rollback_diag;
                    rollback_diag.severity = DiagnosticSeverity::Error;
                    rollback_diag.message = "Automatic rollback failed after edit-bundle apply failure";
                    rollback_diag.source = "bha-lsp";
                    result.errors.push_back(std::move(rollback_diag));
                }
            }
            return result;
        }

        for (const auto& file : changed_files) {
            result.changed_files.push_back(file.string());
        }
        result.success = true;
        return result;
    }

    ApplySuggestionResult SuggestionManager::apply_all_suggestions(
        const std::vector<std::string>& suggestion_ids,
        const bool stop_on_error
    ) {
        ApplySuggestionResult combined_result;
        combined_result.success = true;

        for (const auto& id : suggestion_ids) {
            if (auto result = apply_suggestion(id); !result.success) {
                combined_result.success = false;
                combined_result.errors.insert(combined_result.errors.end(),
                                            result.errors.begin(),
                                            result.errors.end());
                if (stop_on_error) {
                    break;
                }
            } else {
                combined_result.changed_files.insert(combined_result.changed_files.end(),
                                                   result.changed_files.begin(),
                                                   result.changed_files.end());
            }
        }

        return combined_result;
    }

    bool SuggestionManager::revert_changes(const std::string& backup_id) {
        const auto it = backups_.find(backup_id);
        if (it == backups_.end()) {
            if (config_.use_disk_backups && !config_.workspace_root.empty()) {
                if (restore_disk_backup(backup_id)) {
                    if (!config_.keep_backups) {
                        cleanup_disk_backup(backup_id);
                    }
                    return true;
                }
            }
            return false;
        }

        const bool is_disk_backup = config_.use_disk_backups && !config_.workspace_root.empty() &&
                              fs::exists(get_backup_path(backup_id));

        if (is_disk_backup) {
            if (!restore_disk_backup(backup_id)) {
                return false;
            }
            if (!config_.keep_backups) {
                cleanup_disk_backup(backup_id);
            }
        } else {
            for (const auto& backup = it->second; const auto& [path, content] : backup.files) {
                try {
                    std::ofstream out(path, std::ios::binary);
                    if (!out) {
                        return false;
                    }
                    out << content;
                } catch (...) {
                    return false;
                }
            }
        }

        backups_.erase(it);
        backup_lru_.remove(backup_id);
        return true;
    }

    std::string SuggestionManager::create_backup(const std::vector<fs::path>& files) {
        if (config_.use_disk_backups && !config_.workspace_root.empty()) {
            return create_disk_backup(files);
        }
        return create_memory_backup(files);
    }

    std::string SuggestionManager::create_memory_backup(const std::vector<fs::path>& files) {
        Backup backup;
        backup.id = generate_backup_id();
        backup.timestamp = std::chrono::system_clock::now();

        for (const auto& file : files) {
            if (fs::exists(file)) {
                FileBackup file_backup;
                file_backup.path = file;

                if (std::ifstream in(file, std::ios::binary); in) {
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    file_backup.content = ss.str();
                    backup.files.push_back(std::move(file_backup));
                }
            }
        }

        backups_[backup.id] = std::move(backup);
        backup_lru_.push_back(backup.id);

        evict_old_backups();

        return backup.id;
    }

    fs::path SuggestionManager::get_backup_path(const std::string& backup_id) const {
        const fs::path backup_dir = config_.workspace_root / config_.backup_directory;
        return backup_dir / backup_id;
    }

    std::string SuggestionManager::create_disk_backup(const std::vector<fs::path>& files) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        std::ostringstream timestamp_ss;
        timestamp_ss << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");

        std::string backup_id = timestamp_ss.str() + "-" + std::to_string(++backup_counter_);
        fs::path backup_path = get_backup_path(backup_id);

        std::error_code ec;
        fs::create_directories(backup_path, ec);
        if (ec) {
            return {};
        }

        Backup backup;
        backup.id = backup_id;
        backup.timestamp = now;

        for (const auto& file : files) {
            if (!fs::exists(file)) continue;

            FileBackup file_backup;
            file_backup.path = file;

            fs::path relative = fs::relative(file, config_.workspace_root, ec);
            if (ec) {
                fs::remove_all(backup_path, ec);
                return {};
            }
            fs::path dest = backup_path / "files" / relative;
            if (const fs::path parent = dest.parent_path(); !parent.empty()) {
                fs::create_directories(parent, ec);
                if (ec) {
                    fs::remove_all(backup_path, ec);
                    return {};
                }
            }

            if (!copy_file_with_sync(file, dest)) {
                fs::remove_all(backup_path, ec);
                return {};
            }
            backup.files.push_back(std::move(file_backup));
        }

        if (!write_backup_metadata(backup_path, backup)) {
            fs::remove_all(backup_path, ec);
            return {};
        }

        if (!sync_directory_to_disk(backup_path)) {
            fs::remove_all(backup_path, ec);
            return {};
        }
        const fs::path root_backup_dir = (config_.workspace_root / config_.backup_directory).lexically_normal();
        if (!root_backup_dir.empty() && !sync_directory_to_disk(root_backup_dir)) {
            fs::remove_all(backup_path, ec);
            return {};
        }

        backups_[backup_id] = std::move(backup);
        backup_lru_.push_back(backup_id);
        evict_old_backups();

        return backup_id;
    }

    bool SuggestionManager::write_backup_metadata(const fs::path& backup_dir, const Backup& backup) {
        const fs::path meta_path = backup_dir / "metadata.txt";
        const fs::path tmp_meta_path = backup_dir / "metadata.txt.tmp";
        std::ofstream out(tmp_meta_path, std::ios::trunc);
        if (!out) return false;

        const auto time_t = std::chrono::system_clock::to_time_t(backup.timestamp);
        out << "id=" << backup.id << "\n";
        out << "timestamp=" << time_t << "\n";
        out << "file_count=" << backup.files.size() << "\n";

        for (const auto& [path, content] : backup.files) {
            out << "file=" << path.string() << "\n";
        }
        out.flush();
        out.close();
        if (!out.good()) {
            return false;
        }

        if (!sync_file_to_disk(tmp_meta_path)) {
            return false;
        }

        std::error_code ec;
        fs::rename(tmp_meta_path, meta_path, ec);
        if (ec) {
            fs::remove(meta_path, ec);
            ec.clear();
            fs::rename(tmp_meta_path, meta_path, ec);
            if (ec) {
                return false;
            }
        }

        if (!sync_file_to_disk(meta_path)) {
            return false;
        }
        if (!sync_directory_to_disk(backup_dir)) {
            return false;
        }
        return true;
    }

    std::optional<Backup> SuggestionManager::read_backup_metadata(const fs::path& backup_dir) {
        fs::path meta_path = backup_dir / "metadata.txt";
        std::ifstream in(meta_path);
        if (!in) return std::nullopt;

        Backup backup;
        std::string line;
        while (std::getline(in, line)) {
            auto eq_pos = line.find('=');
            if (eq_pos == std::string::npos) continue;

            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);

            if (key == "id") {
                backup.id = value;
            } else if (key == "timestamp") {
                auto time_t = std::stoll(value);
                backup.timestamp = std::chrono::system_clock::from_time_t(time_t);
            } else if (key == "file") {
                FileBackup fb;
                fb.path = value;
                backup.files.push_back(std::move(fb));
            }
        }

        return backup.id.empty() ? std::nullopt : std::make_optional(std::move(backup));
    }

    bool SuggestionManager::restore_disk_backup(const std::string& backup_id) const
    {
        const fs::path backup_path = get_backup_path(backup_id);
        if (!fs::exists(backup_path)) return false;

        auto metadata = read_backup_metadata(backup_path);
        if (!metadata) return false;

        for (const auto& [path, content] : metadata->files) {
            fs::path relative = fs::relative(path, config_.workspace_root);
            fs::path src = backup_path / "files" / relative;

            if (!fs::exists(src)) continue;

            try {
                if (const fs::path parent = path.parent_path(); !parent.empty()) {
                    fs::create_directories(parent);
                }
                fs::copy_file(src, path, fs::copy_options::overwrite_existing);
            } catch (const std::exception&) {
                return false;
            }
        }

        return true;
    }

    void SuggestionManager::cleanup_disk_backup(const std::string& backup_id) const
    {
        if (const fs::path backup_path = get_backup_path(backup_id); fs::exists(backup_path)) {
            std::error_code ec;
            fs::remove_all(backup_path, ec);
        }
    }

    bool SuggestionManager::validate_files_exist(const std::vector<fs::path>& files) {
        return std::ranges::all_of(files, [](const auto& file) {
            return fs::exists(file);
        });
    }

    namespace {
        /**
         * Converts a 0-based line and column position to a byte offset in the content.
         * Handles both Unix (LF) and Windows (CRLF) line endings.
         */
        std::optional<std::size_t> line_col_to_offset(
            const std::string& content,
            const std::size_t line,
            const std::size_t col
        ) {
            std::size_t current_line = 0;
            std::size_t line_start = 0;

            for (std::size_t i = 0; i < content.size(); ++i) {
                if (current_line == line) {
                    std::size_t pos = line_start;
                    std::size_t units = 0;
                    while (pos < content.size() && content[pos] != '\n' && content[pos] != '\r') {
                        if (units >= col) {
                            return pos;
                        }

                        const unsigned char lead = static_cast<unsigned char>(content[pos]);
                        std::size_t advance = 1;
                        uint32_t codepoint = 0;
                        if ((lead & 0x80u) == 0) {
                            codepoint = lead;
                            advance = 1;
                        } else if ((lead & 0xE0u) == 0xC0u && pos + 1 < content.size()) {
                            codepoint = ((lead & 0x1Fu) << 6) |
                                        (static_cast<unsigned char>(content[pos + 1]) & 0x3Fu);
                            advance = 2;
                        } else if ((lead & 0xF0u) == 0xE0u && pos + 2 < content.size()) {
                            codepoint = ((lead & 0x0Fu) << 12) |
                                        ((static_cast<unsigned char>(content[pos + 1]) & 0x3Fu) << 6) |
                                        (static_cast<unsigned char>(content[pos + 2]) & 0x3Fu);
                            advance = 3;
                        } else if ((lead & 0xF8u) == 0xF0u && pos + 3 < content.size()) {
                            codepoint = ((lead & 0x07u) << 18) |
                                        ((static_cast<unsigned char>(content[pos + 1]) & 0x3Fu) << 12) |
                                        ((static_cast<unsigned char>(content[pos + 2]) & 0x3Fu) << 6) |
                                        (static_cast<unsigned char>(content[pos + 3]) & 0x3Fu);
                            advance = 4;
                        } else {
                            codepoint = lead;
                            advance = 1;
                        }

                        units += (codepoint > 0xFFFFu) ? 2u : 1u;
                        pos += advance;
                    }
                    return pos;
                }
                if (content[i] == '\n') {
                    ++current_line;
                    line_start = i + 1;
                }
            }

            // Handle last line (no trailing newline)
            if (current_line == line) {
                std::size_t pos = line_start;
                std::size_t units = 0;
                while (pos < content.size() && content[pos] != '\n' && content[pos] != '\r') {
                    if (units >= col) {
                        return pos;
                    }

                    const unsigned char lead = static_cast<unsigned char>(content[pos]);
                    std::size_t advance = 1;
                    uint32_t codepoint = 0;
                    if ((lead & 0x80u) == 0) {
                        codepoint = lead;
                        advance = 1;
                    } else if ((lead & 0xE0u) == 0xC0u && pos + 1 < content.size()) {
                        codepoint = ((lead & 0x1Fu) << 6) |
                                    (static_cast<unsigned char>(content[pos + 1]) & 0x3Fu);
                        advance = 2;
                    } else if ((lead & 0xF0u) == 0xE0u && pos + 2 < content.size()) {
                        codepoint = ((lead & 0x0Fu) << 12) |
                                    ((static_cast<unsigned char>(content[pos + 1]) & 0x3Fu) << 6) |
                                    (static_cast<unsigned char>(content[pos + 2]) & 0x3Fu);
                        advance = 3;
                    } else if ((lead & 0xF8u) == 0xF0u && pos + 3 < content.size()) {
                        codepoint = ((lead & 0x07u) << 18) |
                                    ((static_cast<unsigned char>(content[pos + 1]) & 0x3Fu) << 12) |
                                    ((static_cast<unsigned char>(content[pos + 2]) & 0x3Fu) << 6) |
                                    (static_cast<unsigned char>(content[pos + 3]) & 0x3Fu);
                        advance = 4;
                    } else {
                        codepoint = lead;
                        advance = 1;
                    }

                    units += (codepoint > 0xFFFFu) ? 2u : 1u;
                    pos += advance;
                }
                return pos;
            }

            if (line > current_line) {
                return content.size();
            }

            return std::nullopt;
        }

        /**
         * Applies a single TextEdit to the content string.
         * TextEdit uses 0-based line and column numbers.
         */
        bool apply_single_edit(std::string& content, const bha::TextEdit& edit) {
            auto start_offset = line_col_to_offset(content, edit.start_line, edit.start_col);
            auto end_offset = line_col_to_offset(content, edit.end_line, edit.end_col);

            if (!start_offset || !end_offset) {
                return false;
            }

            // Ensure start <= end
            if (*start_offset > *end_offset) {
                std::swap(start_offset, end_offset);
            }

            // Clamp to content bounds
            *start_offset = std::min(*start_offset, content.size());
            *end_offset = std::min(*end_offset, content.size());

            // Replace the range with new text
            content.replace(*start_offset, *end_offset - *start_offset, edit.new_text);
            return true;
        }

        /**
         * Applies multiple TextEdits to a file.
         * Edits are sorted in reverse order (by position) to avoid offset shifts.
         */
        bool apply_edits_to_file(const fs::path& file_path, std::vector<bha::TextEdit> edits) {
            if (edits.empty()) {
                return true;
            }

            std::ifstream in(file_path);
            if (!in) {
                return false;
            }
            std::string content(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>()
            );
            in.close();

            // Sort edits in reverse order (later positions first)
            // This ensures applying one edit doesn't shift positions of subsequent edits
            std::ranges::sort(edits, [](const bha::TextEdit& a, const bha::TextEdit& b) {
                if (a.start_line != b.start_line) {
                    return a.start_line > b.start_line;  // Later lines first
                }
                return a.start_col > b.start_col;  // Later columns first
            });

            for (const auto& edit : edits) {
                if (!apply_single_edit(content, edit)) {
                    return false;
                }
            }

            std::ofstream out(file_path);
            if (!out) {
                return false;
            }
            out << content;
            return out.good();
        }

    }  // namespace

    bool apply_replacements_to_file(
        const fs::path& file_path,
        std::vector<ExternalReplacement> replacements
    ) {
        if (replacements.empty()) {
            return true;
        }

        std::ifstream in(file_path, std::ios::binary);
        if (!in) {
            return false;
        }
        std::string content(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        in.close();

        std::ranges::sort(replacements, [](const ExternalReplacement& lhs, const ExternalReplacement& rhs) {
            if (lhs.offset != rhs.offset) {
                return lhs.offset > rhs.offset;
            }
            return lhs.length > rhs.length;
        });

        for (const auto& replacement : replacements) {
            if (replacement.offset > content.size()) {
                return false;
            }
            const auto clamped_length = std::min(replacement.length, content.size() - replacement.offset);
            content.replace(replacement.offset, clamped_length, replacement.replacement_text);
        }

        std::ofstream out(file_path, std::ios::binary);
        if (!out) {
            return false;
        }
        out << content;
        return out.good();
    }

    bool SuggestionManager::apply_file_changes(const bha::Suggestion& suggestion, std::vector<fs::path>& changed_files) {
        if (!suggestion.edits.empty()) {
            std::unordered_map<std::string, std::vector<bha::TextEdit>> edits_by_file;
            for (const auto& edit : suggestion.edits) {
                bha::TextEdit normalized = edit;
                if (edit.file.filename() == "pch.h" && !edit.new_text.empty()) {
                    normalized.new_text = normalize_pch_text(edit.new_text);
                }
                edits_by_file[normalized.file.string()].push_back(std::move(normalized));
            }

            for (auto& [file_path_str, file_edits] : edits_by_file) {
                fs::path file_path(file_path_str);

                // For new files, create them first
                if (!fs::exists(file_path)) {
                    if (const fs::path parent = file_path.parent_path(); !parent.empty()) {
                        fs::create_directories(parent);
                    }
                    std::ofstream out(file_path);
                    if (!out) {
                        return false;
                    }
                    // Write empty file, edits will add content
                    out.close();
                }

                if (!apply_edits_to_file(file_path, std::move(file_edits))) {
                    return false;
                }
                if (file_path.filename() == "pch.h") {
                    std::ifstream in(file_path);
                    if (in) {
                        std::string content(
                            (std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>()
                        );
                        in.close();
                        if (!content.empty()) {
                            std::ofstream out(file_path);
                            if (out) {
                                out << normalize_pch_text(content);
                            }
                        }
                    }
                }
                changed_files.push_back(file_path);
            }

            return true;
        }

        // Fallback: apply using FileTarget actions (for suggestions without precise edits)
        auto apply_file_target = [&](const FileTarget& target) -> bool {
            try {
                if (target.action == FileAction::Create) {
                    if (fs::exists(target.path)) {
                        return false;
                    }
                    if (const fs::path parent = target.path.parent_path(); !parent.empty()) {
                        fs::create_directories(parent);
                    }
                    std::ofstream out(target.path);
                    if (!out) {
                        return false;
                    }
                    if (!suggestion.after_code.code.empty()) {
                        out << suggestion.after_code.code;
                    }
                    changed_files.push_back(target.path);
                }
                else if (target.action == FileAction::Modify) {
                    if (!fs::exists(target.path)) {
                        return false;
                    }
                    if (!suggestion.after_code.code.empty()) {
                        std::ofstream out(target.path);
                        if (!out) {
                            return false;
                        }
                        out << suggestion.after_code.code;
                        changed_files.push_back(target.path);
                    }
                }
                else if (target.action == FileAction::AddInclude) {
                    if (!fs::exists(target.path)) {
                        return false;
                    }
                    std::ifstream in(target.path);
                    if (!in) {
                        return false;
                    }
                    std::string content(
                        (std::istreambuf_iterator(in)),
                        std::istreambuf_iterator<char>()
                    );
                    in.close();

                    if (target.note && !target.note->empty()) {
                        if (size_t first_include = content.find("#include"); first_include != std::string::npos) {
                            content.insert(first_include, *target.note + "\n");
                        } else {
                            content = *target.note + "\n" + content;
                        }
                        std::ofstream out(target.path);
                        if (!out) {
                            return false;
                        }
                        out << content;
                        changed_files.push_back(target.path);
                    }
                }
            } catch (...) {
                return false;
            }
            return true;
        };

        return apply_file_target(suggestion.target_file) &&
               std::ranges::all_of(
                   suggestion.secondary_files,
                   apply_file_target
               );
    }

    std::vector<Suggestion> SuggestionManager::get_all_suggestions() const {
        std::vector<Suggestion> result;
        result.reserve(suggestions_.size());
        for (const auto& detailed : suggestions_ | std::views::values) {
            result.push_back(detailed);
        }
        return result;
    }

    std::optional<Suggestion> SuggestionManager::get_suggestion(const std::string& id) const {
        if (const auto it = suggestions_.find(id); it != suggestions_.end()) {
            return static_cast<const Suggestion&>(it->second);
        }
        return std::nullopt;
    }

    const bha::Suggestion* SuggestionManager::get_bha_suggestion(const std::string& id) const {
        if (const auto it = bha_suggestions_.find(id); it != bha_suggestions_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::optional<BuildMetrics> SuggestionManager::get_last_baseline_metrics() const {
        if (last_analysis_id_.empty()) {
            return std::nullopt;
        }
        const auto analysis_it = analysis_cache_.find(last_analysis_id_);
        if (analysis_it == analysis_cache_.end()) {
            return std::nullopt;
        }
        return extract_build_metrics(analysis_it->second);
    }


    BuildMetrics SuggestionManager::extract_build_metrics(const BuildTrace& trace) {
        BuildMetrics metrics;

        const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(trace.total_time).count();
        metrics.total_duration_ms = static_cast<int>(total_ms);
        metrics.files_compiled = static_cast<int>(trace.units.size());
        metrics.files_up_to_date = 0;

        std::vector<std::pair<std::string, int>> file_times;
        for (const auto& unit : trace.units) {
            const auto unit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(unit.metrics.total_time).count();
            file_times.emplace_back(unit.source_file.filename().string(), static_cast<int>(unit_ms));
        }

        std::ranges::sort(file_times,
                          [](const auto& a, const auto& b) { return a.second > b.second; });

        for (size_t i = 0; i < std::min(file_times.size(), static_cast<size_t>(10)); ++i) {
            BuildMetrics::FileMetric fm;
            fm.file = file_times[i].first;
            fm.duration_ms = file_times[i].second;
            fm.percentage = (static_cast<double>(file_times[i].second) / static_cast<double>(total_ms)) * 100.0;
            metrics.slowest_files.push_back(fm);
        }

        return metrics;
    }

    Priority SuggestionManager::calculate_priority(const double improvement_percentage) {
        if (improvement_percentage >= 20.0) {
            return Priority::High;
        }
        if (improvement_percentage >= 5.0) {
            return Priority::Medium;
        }
        return Priority::Low;
    }

    std::string SuggestionManager::generate_analysis_id() {
        return "ana-" + std::to_string(++analysis_counter_);
    }

    std::string SuggestionManager::generate_backup_id() {
        return "backup-" + std::to_string(++backup_counter_);
    }

    Suggestion SuggestionManager::convert_suggestion(const bha::Suggestion& bha_sug) {
        Suggestion lsp_sug{};
        lsp_sug.id = bha_sug.id;

        switch (bha_sug.type) {
        case bha::SuggestionType::PCHOptimization:
            lsp_sug.type = SuggestionType::PrecompiledHeader;
            break;
        case bha::SuggestionType::HeaderSplit:
            lsp_sug.type = SuggestionType::HeaderSplit;
            break;
        case bha::SuggestionType::UnityBuild:
            lsp_sug.type = SuggestionType::UnityBuild;
            break;
        case bha::SuggestionType::ExplicitTemplate:
            lsp_sug.type = SuggestionType::TemplateOptimization;
            break;
        case bha::SuggestionType::IncludeRemoval:
            lsp_sug.type = SuggestionType::IncludeReduction;
            break;
        case bha::SuggestionType::ForwardDeclaration:
            lsp_sug.type = SuggestionType::ForwardDeclaration;
            break;
        case bha::SuggestionType::PIMPLPattern:
            lsp_sug.type = SuggestionType::PIMPLPattern;
            break;
        case bha::SuggestionType::MoveToCpp:
            lsp_sug.type = SuggestionType::MoveToCpp;
            break;
        }

        switch (bha_sug.priority) {
        case bha::Priority::Critical:
        case bha::Priority::High:
            lsp_sug.priority = Priority::High;
            break;
        case bha::Priority::Medium:
            lsp_sug.priority = Priority::Medium;
            break;
        case bha::Priority::Low:
            lsp_sug.priority = Priority::Low;
            break;
        }

        lsp_sug.title = bha_sug.title;
        lsp_sug.description = bha_sug.description;
        lsp_sug.confidence = bha_sug.confidence;
        lsp_sug.auto_applicable = is_auto_applicable_suggestion(bha_sug);
        lsp_sug.application_mode = std::string(
            bha::to_string(bha::resolve_application_mode(bha_sug))
        );
        if (bha_sug.refactor_class_name) {
            lsp_sug.refactor_class_name = *bha_sug.refactor_class_name;
        }
        if (bha_sug.refactor_compile_commands_path) {
            lsp_sug.compile_commands_path = bha_sug.refactor_compile_commands_path->string();
        }
        lsp_sug.application_summary = format_application_summary(bha_sug);
        lsp_sug.application_guidance = format_application_guidance(bha_sug);
        lsp_sug.auto_apply_blocked_reason = format_auto_apply_blocked_reason(bha_sug);
        if (is_unreal_suggestion(bha_sug)) {
            lsp_sug.project_context = "unreal";
            std::unordered_set<std::string> seen_module_rules;
            std::unordered_set<std::string> seen_target_rules;
            const auto add_file = [&](const fs::path& path) {
                const std::string normalized = path.lexically_normal().string();
                if (is_unreal_module_rules_file(path)) {
                    if (seen_module_rules.insert(normalized).second) {
                        lsp_sug.module_rules_files.push_back(normalized);
                    }
                }
                if (is_unreal_target_rules_file(path)) {
                    if (seen_target_rules.insert(normalized).second) {
                        lsp_sug.target_rules_files.push_back(normalized);
                    }
                }
            };
            add_file(bha_sug.target_file.path);
            for (const auto& file : bha_sug.secondary_files) {
                add_file(file.path);
            }
            lsp_sug.safety_guard = infer_unreal_safety_guard(bha_sug);
        }

        const auto savings_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                bha_sug.estimated_savings
            ).count();

        lsp_sug.estimated_impact.time_saved_ms = static_cast<int>(savings_ms);
        lsp_sug.estimated_impact.percentage = bha_sug.estimated_savings_percent;
        lsp_sug.estimated_impact.files_affected =
            static_cast<int>(bha_sug.impact.total_files_affected);

        const auto steps = bha_sug.implementation_steps.size();
        lsp_sug.estimated_impact.complexity =
            steps <= 2 ? Complexity::Simple :
            steps <= 5 ? Complexity::Moderate :
                         Complexity::Complex;

        // Populate source location from FileTarget
        if (!bha_sug.target_file.path.empty()) {
            lsp_sug.target_uri = uri::path_to_uri(bha_sug.target_file.path);

            if (bha_sug.target_file.has_line_range()) {
                Range range{};
                // LSP uses 0-based lines, BHA uses 1-based
                range.start.line = static_cast<int>(bha_sug.target_file.line_start) - 1;
                range.start.character = bha_sug.target_file.has_column_range()
                    ? static_cast<int>(bha_sug.target_file.col_start) - 1
                    : 0;
                range.end.line = static_cast<int>(bha_sug.target_file.line_end) - 1;
                range.end.character = bha_sug.target_file.has_column_range()
                    ? static_cast<int>(bha_sug.target_file.col_end) - 1
                    : 0;
                lsp_sug.range = range;
            }
        }

        return lsp_sug;
    }

    DetailedSuggestion SuggestionManager::convert_to_detailed(const bha::Suggestion& bha_sug) {
        DetailedSuggestion detailed;

        static_cast<Suggestion&>(detailed) = convert_suggestion(bha_sug);
        detailed.rationale = bha_sug.rationale;

        if (bha_sug.target_file.action == FileAction::Create) {
            detailed.files_to_create.push_back(bha_sug.target_file.path.string());
        }
        for (const auto& file : bha_sug.secondary_files) {
            if (file.action == FileAction::Create) {
                detailed.files_to_create.push_back(file.path.string());
            }
        }

        if (bha_sug.target_file.action == FileAction::Modify ||
            bha_sug.target_file.action == FileAction::AddInclude) {
            detailed.files_to_modify.push_back(bha_sug.target_file.path.string());
            }
        for (const auto& file : bha_sug.secondary_files) {
            if (file.action == FileAction::Modify ||
                file.action == FileAction::AddInclude) {
                detailed.files_to_modify.push_back(file.path.string());
                }
        }

        detailed.dependencies = bha_sug.implementation_steps;
        detailed.application_summary = format_application_summary(bha_sug);
        detailed.application_guidance = format_application_guidance(bha_sug);
        detailed.auto_apply_blocked_reason = format_auto_apply_blocked_reason(bha_sug);

        return detailed;
    }

    ApplyAllResult SuggestionManager::apply_all_suggestions(
        const std::optional<std::string>& min_priority,
        const bool safe_only
    ) {
        struct PendingSuggestion {
            std::string key;
        };

        const auto parse_numeric_id = [](const std::string& id) -> int {
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
        };

        const auto stable_suggestion_key = [](const bha::Suggestion& suggestion) {
            struct EditKey {
                std::string file;
                std::size_t start_line = 0;
                std::size_t start_col = 0;
                std::size_t end_line = 0;
                std::size_t end_col = 0;
                std::size_t new_text_hash = 0;
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
                    std::hash<std::string>{}(edit.new_text)
                });
            }
            std::ranges::sort(edits, [](const EditKey& lhs, const EditKey& rhs) {
                if (lhs.file != rhs.file) return lhs.file < rhs.file;
                if (lhs.start_line != rhs.start_line) return lhs.start_line < rhs.start_line;
                if (lhs.start_col != rhs.start_col) return lhs.start_col < rhs.start_col;
                if (lhs.end_line != rhs.end_line) return lhs.end_line < rhs.end_line;
                if (lhs.end_col != rhs.end_col) return lhs.end_col < rhs.end_col;
                return lhs.new_text_hash < rhs.new_text_hash;
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
            key << static_cast<int>(suggestion.type)
                << "|" << suggestion.target_file.path.lexically_normal().generic_string()
                << "|" << static_cast<int>(suggestion.target_file.action);
            for (const auto& edit : edits) {
                key << "|e:" << edit.file
                    << ":" << edit.start_line
                    << ":" << edit.start_col
                    << ":" << edit.end_line
                    << ":" << edit.end_col
                    << ":" << edit.new_text_hash;
            }
            for (const auto& file : secondary) {
                key << "|s:" << file.path << ":" << file.action;
            }
            return key.str();
        };

        ApplyAllResult result;
        result.success = true;
        result.applied_count = 0;
        result.skipped_count = 0;

        std::optional<bha::Priority> priority_threshold;
        if (min_priority) {
            std::string prio_lower = *min_priority;
            std::ranges::transform(prio_lower, prio_lower.begin(), ::tolower);
            if (prio_lower == "critical") {
                priority_threshold = bha::Priority::Critical;
            } else if (prio_lower == "high") {
                priority_threshold = bha::Priority::High;
            } else if (prio_lower == "medium") {
                priority_threshold = bha::Priority::Medium;
            } else if (prio_lower == "low") {
                priority_threshold = bha::Priority::Low;
            }
        }

        const auto meets_priority_threshold = [&](const bha::Suggestion& suggestion) {
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
        };

        const auto is_candidate_enabled = [&](const bha::Suggestion& suggestion) {
            if (safe_only && !is_auto_applicable_suggestion(suggestion)) {
                return false;
            }
            return meets_priority_threshold(suggestion);
        };

        const auto is_higher_ranked = [&](const std::string& lhs_id, const std::string& rhs_id) {
            const auto lhs_it = bha_suggestions_.find(lhs_id);
            const auto rhs_it = bha_suggestions_.find(rhs_id);
            if (lhs_it == bha_suggestions_.end() || rhs_it == bha_suggestions_.end()) {
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
            const int lhs_numeric = parse_numeric_id(lhs_id);
            const int rhs_numeric = parse_numeric_id(rhs_id);
            if (lhs_numeric >= 0 && rhs_numeric >= 0) {
                return lhs_numeric < rhs_numeric;
            }
            return lhs_id < rhs_id;
        };

        const auto build_key_index = [&]() {
            std::unordered_map<std::string, std::string> index;
            for (const auto& [id, suggestion] : bha_suggestions_) {
                if (!is_candidate_enabled(suggestion)) {
                    continue;
                }
                const std::string key = stable_suggestion_key(suggestion);
                if (auto it = index.find(key); it == index.end() || is_higher_ranked(id, it->second)) {
                    index[key] = id;
                }
            }
            return index;
        };

        std::vector<fs::path> all_files_to_backup;
        std::vector<PendingSuggestion> pending;
        std::unordered_set<std::string> seen_pending_keys;

        for (const auto& [id, bha_sug] : bha_suggestions_) {
            if (!is_candidate_enabled(bha_sug)) {
                result.skipped_count++;
                continue;
            }

            const std::string key = stable_suggestion_key(bha_sug);
            if (!seen_pending_keys.insert(key).second) {
                continue;
            }
            pending.push_back(PendingSuggestion{key});

            if (bha_sug.target_file.action == bha::FileAction::Modify ||
                bha_sug.target_file.action == bha::FileAction::AddInclude) {
                all_files_to_backup.push_back(bha_sug.target_file.path);
            }
            for (const auto& secondary : bha_sug.secondary_files) {
                if (secondary.action == bha::FileAction::Modify ||
                    secondary.action == bha::FileAction::AddInclude) {
                    all_files_to_backup.push_back(secondary.path);
                }
            }
        }

        {
            const auto index = build_key_index();
            std::ranges::sort(
                pending,
                [&](const PendingSuggestion& lhs, const PendingSuggestion& rhs) {
                    const auto lhs_it = index.find(lhs.key);
                    const auto rhs_it = index.find(rhs.key);
                    if (lhs_it == index.end() && rhs_it == index.end()) {
                        return lhs.key < rhs.key;
                    }
                    if (lhs_it == index.end()) {
                        return false;
                    }
                    if (rhs_it == index.end()) {
                        return true;
                    }
                    return is_higher_ranked(lhs_it->second, rhs_it->second);
                }
            );
        }

        // Single backup for all changes
        if (!all_files_to_backup.empty()) {
            result.backup_id = create_backup(all_files_to_backup);
            if (result.backup_id.empty()) {
                result.success = false;
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Failed to create durable backup before apply-all operation";
                result.errors.push_back(std::move(diag));
                return result;
            }
        }

        std::unordered_set<std::string> changed_file_set;
        const bool can_rerank = config_.rerank_remaining_after_each_apply &&
            last_project_root_.has_value() && !last_project_root_->empty();

        while (!pending.empty()) {
            auto key_index = build_key_index();
            pending.erase(
                std::remove_if(
                    pending.begin(),
                    pending.end(),
                    [&](const PendingSuggestion& item) {
                        if (key_index.contains(item.key)) {
                            return false;
                        }
                        result.skipped_count++;
                        return true;
                    }
                ),
                pending.end()
            );
            if (pending.empty()) {
                break;
            }

            auto best_it = pending.begin();
            std::string best_id = key_index.at(best_it->key);
            for (auto it = std::next(pending.begin()); it != pending.end(); ++it) {
                const std::string candidate_id = key_index.at(it->key);
                if (is_higher_ranked(candidate_id, best_id)) {
                    best_it = it;
                    best_id = candidate_id;
                }
            }

            const std::string selected_key = best_it->key;
            pending.erase(best_it);

            auto apply_result = apply_suggestion(best_id, false, true, false);
            if (apply_result.success) {
                result.applied_count++;
                result.applied_suggestion_ids.push_back(best_id);
                for (const auto& file : apply_result.changed_files) {
                    if (changed_file_set.insert(file).second) {
                        result.changed_files.push_back(file);
                    }
                }

                if (!pending.empty() && can_rerank) {
                    try {
                        analyze_project(
                            *last_project_root_,
                            last_build_dir_,
                            true,
                            nullptr,
                            last_analyze_options_
                        );
                    } catch (const std::exception& e) {
                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.source = "bha-lsp";
                        diag.message =
                            "Failed to re-analyze project after applying suggestion " +
                            best_id + " (" + selected_key + "): " + e.what();
                        result.errors.push_back(std::move(diag));
                        break;
                    }
                }
            } else {
                result.skipped_count++;
                result.errors.insert(result.errors.end(),
                                     apply_result.errors.begin(),
                                     apply_result.errors.end());
            }
        }

        result.success = result.errors.empty();
        return result;
    }

    RevertResult SuggestionManager::revert_changes_detailed(const std::string& backup_id) {
        RevertResult result;
        result.success = true;

        const auto it = backups_.find(backup_id);
        bool is_disk_backup = config_.use_disk_backups && !config_.workspace_root.empty() &&
                              fs::exists(get_backup_path(backup_id));

        if (it == backups_.end() && !is_disk_backup) {
            result.success = false;
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Invalid backup ID: " + backup_id;
            result.errors.push_back(diag);
            return result;
        }

        if (is_disk_backup) {
            fs::path backup_path = get_backup_path(backup_id);
            auto metadata = read_backup_metadata(backup_path);
            if (!metadata) {
                result.success = false;
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "Failed to read backup metadata";
                result.errors.push_back(diag);
                return result;
            }

            for (const auto& file : metadata->files) {
                fs::path relative = fs::relative(file.path, config_.workspace_root);
                fs::path src = backup_path / "files" / relative;

                try {
                    if (!fs::exists(src)) {
                        result.success = false;
                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.message = "Backup file not found: " + src.string();
                        result.errors.push_back(diag);
                        continue;
                    }
                    if (const fs::path parent = file.path.parent_path(); !parent.empty()) {
                        fs::create_directories(parent);
                    }
                    fs::copy_file(src, file.path, fs::copy_options::overwrite_existing);
                    result.restored_files.push_back(file.path.string());
                } catch (const std::exception& e) {
                    result.success = false;
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.message = "Error restoring " + file.path.string() + ": " + e.what();
                    result.errors.push_back(diag);
                }
            }

            if (result.success && !config_.keep_backups) {
                cleanup_disk_backup(backup_id);
            }
        } else if (it != backups_.end()) {
            for (const auto& backup = it->second; const auto& [path, content] : backup.files) {
                try {
                    if (std::ofstream out(path, std::ios::binary); !out) {
                        result.success = false;
                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.message = "Failed to restore file: " + path.string();
                        result.errors.push_back(diag);
                    } else {
                        out << content;
                        result.restored_files.push_back(path.string());
                    }
                } catch (const std::exception& e) {
                    result.success = false;
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.message = "Error restoring " + path.string() + ": " + e.what();
                    result.errors.push_back(diag);
                }
            }
        }

        if (result.success && it != backups_.end()) {
            backups_.erase(it);
            backup_lru_.remove(backup_id);
        }
        return result;
    }
}
