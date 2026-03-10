//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_SUGGESTER_HPP
#define BHA_SUGGESTER_HPP

/**
 * @file suggester.hpp
 * @brief Interface for suggestion generators.
 *
 * Suggesters analyze build traces and analysis results to produce
 * actionable optimization suggestions. Each suggester focuses on
 * a specific optimization strategy:
 *
 * - PCHSuggester: Identifies candidates for precompiled headers
 * - ForwardDeclSuggester: Finds opportunities for forward declarations
 * - IncludeSuggester: Detects removable or reducible includes
 * - TemplateSuggester: Suggests explicit instantiations
 *
 * All suggesters follow the Result<T,E> error handling pattern.
 */

#include "bha/types.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/utils/file_utils.hpp"

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <memory>
#include <regex>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace bha::suggestions {
    namespace fs = std::filesystem;

    /**
     * Context passed to suggesters containing all analysis data.
     */
    struct SuggestionContext {
        const BuildTrace& trace;
        const analyzers::AnalysisResult& analysis;
        const SuggesterOptions& options;
        fs::path project_root;

        /// Optional cancellation token. Suggesters should check this periodically
        /// in long-running loops and return early if canceled.
        std::atomic<bool>* cancelled = nullptr;

        std::optional<std::chrono::steady_clock::time_point> deadline{};

        /// Optional filter for incremental analysis. When set, only analyze
        /// files in this list. Empty means analyze all files.
        std::vector<fs::path> target_files{};
        std::unordered_set<std::string> target_files_lookup{};

        /// Check if the operation has been canceled.
        [[nodiscard]] bool is_cancelled() const noexcept {
            if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
                return true;
            }
            if (deadline.has_value() && std::chrono::steady_clock::now() >= *deadline) {
                return true;
            }
            return false;
        }

        /// Check if a file should be analyzed (respects target_files filter).
        [[nodiscard]] bool should_analyze(const fs::path& file) const {
            if (target_files.empty()) {
                return true;
            }

            fs::path normalized_file = file;
            if (normalized_file.is_relative() && !project_root.empty()) {
                normalized_file = project_root / normalized_file;
            }
            normalized_file = normalized_file.lexically_normal();
            if (!target_files_lookup.empty()) {
                if (normalized_file.parent_path().empty()) {
                    return target_files_lookup.contains(normalized_file.filename().string());
                }
                return target_files_lookup.contains(normalized_file.generic_string());
            }
            return std::ranges::any_of(
                target_files,
                [&](const fs::path& target) {
                    if (target.parent_path().empty()) {
                        return normalized_file.filename() == target;
                    }
                    return normalized_file == target.lexically_normal();
                }
            );
        }
    };

    [[nodiscard]] inline bool has_build_system_marker(const fs::path& dir) {
        if (fs::exists(dir / "CMakeLists.txt") ||
            fs::exists(dir / "meson.build") ||
            fs::exists(dir / "build.ninja") ||
            fs::exists(dir / "Makefile") ||
            fs::exists(dir / "makefile") ||
            fs::exists(dir / "GNUmakefile") ||
            fs::exists(dir / "WORKSPACE") ||
            fs::exists(dir / "WORKSPACE.bazel") ||
            fs::exists(dir / "MODULE.bazel") ||
            fs::exists(dir / "BUILD") ||
            fs::exists(dir / "BUILD.bazel") ||
            fs::exists(dir / "BUCK") ||
            fs::exists(dir / "BUCK.v2") ||
            fs::exists(dir / ".buckconfig") ||
            fs::exists(dir / "SConstruct") ||
            fs::exists(dir / "SConscript")) {
            return true;
        }

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension().string();
            if (ext == ".sln" || ext == ".vcxproj" || ext == ".xcodeproj" || ext == ".xcworkspace") {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] inline bool has_build_system_marker_fast(const fs::path& dir) {
        return fs::exists(dir / "CMakeLists.txt") ||
               fs::exists(dir / "meson.build") ||
               fs::exists(dir / "build.ninja") ||
               fs::exists(dir / "Makefile") ||
               fs::exists(dir / "makefile") ||
               fs::exists(dir / "GNUmakefile") ||
               fs::exists(dir / "WORKSPACE") ||
               fs::exists(dir / "WORKSPACE.bazel") ||
               fs::exists(dir / "MODULE.bazel") ||
               fs::exists(dir / "BUILD") ||
               fs::exists(dir / "BUILD.bazel") ||
               fs::exists(dir / "BUCK") ||
               fs::exists(dir / "BUCK.v2") ||
               fs::exists(dir / ".buckconfig") ||
               fs::exists(dir / "SConstruct") ||
               fs::exists(dir / "SConscript");
    }

    [[nodiscard]] inline fs::path find_repository_root(const fs::path& path) {
        fs::path current = path;
        if (fs::exists(current)) {
            if (fs::is_regular_file(current)) {
                current = current.parent_path();
            }
        } else if (path.has_parent_path()) {
            current = path.parent_path();
        }

        std::optional<fs::path> git_root;
        while (!current.empty() && current.has_parent_path() && current != current.parent_path()) {
            if (has_build_system_marker(current)) {
                return current;
            }
            if (!git_root.has_value() && fs::exists(current / ".git")) {
                git_root = current;
            }
            current = current.parent_path();
        }

        if (git_root.has_value()) {
            return *git_root;
        }

        return {};
    }

    [[nodiscard]] inline std::optional<fs::path> find_file_in_repo(
        const fs::path& repo_root,
        const fs::path& filename
    ) {
        static std::unordered_map<std::string, fs::path> cache;
        if (repo_root.empty() || filename.empty()) return std::nullopt;

        const std::string key = repo_root.string() + "|" + filename.string();
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second.empty() ? std::nullopt : std::optional<fs::path>(it->second);
        }

        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(repo_root, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().filename() == filename) {
                cache.emplace(key, entry.path());
                return entry.path();
            }
        }

        cache.emplace(key, fs::path());
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<fs::path> resolve_trace_repo_root(const fs::path& path) {
        if (path.empty()) {
            return std::nullopt;
        }

        fs::path normalized = path;
        if (normalized.is_relative()) {
            normalized = fs::current_path() / normalized;
        }
        normalized = normalized.lexically_normal();

        // Look for ".../traces/..." with a sibling "repo" directory.
        fs::path prefix;
        std::vector<fs::path> parts;
        for (const auto& part : normalized) {
            parts.push_back(part);
        }
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (parts[i] != "traces") {
                continue;
            }

            prefix.clear();
            for (std::size_t k = 0; k < i; ++k) {
                prefix /= parts[k];
            }

            const fs::path sibling_repo = prefix / "repo";
            if (fs::exists(sibling_repo)) {
                return sibling_repo;
            }

        }

        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<fs::path> resolve_trace_repo_file(const fs::path& path) {
        if (auto repo_root = resolve_trace_repo_root(path)) {
            if (auto found = find_file_in_repo(*repo_root, path.filename())) {
                return *found;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline fs::path resolve_source_path(const fs::path& path) {
        if (auto resolved = resolve_trace_repo_file(path)) {
            return *resolved;
        }
        if (path.is_relative() && path.parent_path().empty()) {
            const fs::path root = find_repository_root(fs::current_path());
            if (!root.empty()) {
                if (auto found = find_file_in_repo(root, path.filename())) {
                    return *found;
                }
            }
        }
        return path;
    }

    [[nodiscard]] inline std::optional<fs::path> find_cmake_source_root_from_cache(
        const fs::path& build_dir
    ) {
        if (build_dir.empty()) {
            return std::nullopt;
        }
        const fs::path cache_path = build_dir / "CMakeCache.txt";
        std::error_code ec;
        if (!fs::exists(cache_path, ec) || ec) {
            return std::nullopt;
        }
        auto content = file_utils::read_file(cache_path);
        if (content.is_err()) {
            return std::nullopt;
        }
        std::istringstream input(content.value());
        std::string line;
        while (std::getline(input, line)) {
            if (line.rfind("CMAKE_HOME_DIRECTORY:", 0) != 0 &&
                line.rfind("CMAKE_SOURCE_DIR:", 0) != 0) {
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            fs::path root = line.substr(eq + 1);
            if (!root.empty() && fs::exists(root / "CMakeLists.txt")) {
                return root;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline fs::path find_project_root_from_trace_path(const fs::path& trace_path) {
        if (trace_path.empty()) {
            return {};
        }
        fs::path dir = trace_path;
        if (fs::is_regular_file(dir)) {
            dir = dir.parent_path();
        }
        // 1) Prefer CMake source root from a nearby build dir cache.
        for (fs::path current = dir; !current.empty();) {
            if (auto cmake_root = find_cmake_source_root_from_cache(current)) {
                return *cmake_root;
            }
            const fs::path parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
        // 2) Scan direct child directories of ancestors for CMakeCache.txt.
        for (fs::path current = dir; !current.empty();) {
            std::error_code ec;
            std::vector<std::pair<fs::path, int>> queue;
            queue.reserve(256);
            std::size_t checked = 0;
            for (const auto& entry : fs::directory_iterator(current, ec)) {
                if (ec) break;
                if (!entry.is_directory()) continue;
                if (++checked > 200) break;
                queue.emplace_back(entry.path(), 0);
            }

            std::size_t idx = 0;
            while (idx < queue.size() && checked <= 500) {
                const auto [path, depth] = queue[idx++];
                if (auto cmake_root = find_cmake_source_root_from_cache(path)) {
                    return *cmake_root;
                }
                if (depth >= 1) {
                    continue;
                }
                std::error_code child_ec;
                for (const auto& child : fs::directory_iterator(path, child_ec)) {
                    if (child_ec) break;
                    if (!child.is_directory()) continue;
                    if (++checked > 500) break;
                    queue.emplace_back(child.path(), depth + 1);
                }
            }
            const fs::path parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
        // 3) Match a trace filename against nearby build-system roots.
        auto resolve_trace_sample = [&]() -> fs::path {
            fs::path sample = trace_path;
            if (fs::is_regular_file(sample)) {
                return sample;
            }
            if (!fs::is_directory(sample)) {
                return {};
            }
            std::error_code ec;
            std::size_t checked = 0;
            for (const auto& entry : fs::recursive_directory_iterator(sample, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;
                if (++checked > 200) break;
                if (entry.path().extension() == ".json") {
                    return entry.path();
                }
            }
            return {};
        };

        const fs::path sample_trace = resolve_trace_sample();
        std::string sample_name = sample_trace.filename().string();
        if (sample_name.size() > 5 && sample_name.ends_with(".json")) {
            sample_name = sample_name.substr(0, sample_name.size() - 5);
        } else {
            sample_name.clear();
        }

        if (!sample_name.empty()) {
            std::size_t checked_dirs = 0;
            for (fs::path current = dir; !current.empty();) {
                std::error_code ec;
                for (const auto& entry : fs::directory_iterator(current, ec)) {
                    if (ec) break;
                    if (!entry.is_directory()) continue;
                    if (++checked_dirs > 300) break;
                    const fs::path candidate = entry.path();
                    if (!has_build_system_marker_fast(candidate)) {
                        continue;
                    }
                    if (fs::exists(candidate / sample_name)) {
                        return candidate;
                    }
                    if (auto found = find_file_in_repo(candidate, fs::path(sample_name))) {
                        return candidate;
                    }
                }
                if (checked_dirs > 300) {
                    break;
                }
                const fs::path parent = current.parent_path();
                if (parent == current) {
                    break;
                }
                current = parent;
            }
        }
        // 2) Fall back to nearest directory with build-system markers.
        for (fs::path current = dir; !current.empty();) {
            if (has_build_system_marker_fast(current)) {
                return current;
            }
            const fs::path parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
        return {};
    }

    [[nodiscard]] inline std::string make_repo_relative(const fs::path& path) {
        if (auto repo_root = resolve_trace_repo_root(path)) {
            if (auto found = find_file_in_repo(*repo_root, path.filename())) {
                std::error_code ec;
                auto rel = fs::relative(*found, *repo_root, ec);
                if (!ec && !rel.empty()) {
                    return rel.generic_string();
                }
            }
        }

        if (path.is_relative() && path.parent_path().empty()) {
            const fs::path root = find_repository_root(fs::current_path());
            if (!root.empty()) {
                if (auto found = find_file_in_repo(root, path.filename())) {
                    std::error_code ec;
                    auto rel = fs::relative(*found, root, ec);
                    if (!ec && !rel.empty()) {
                        return rel.generic_string();
                    }
                }
            }
        }

        const fs::path root = find_repository_root(path);
        if (!root.empty() && path.is_absolute()) {
            std::error_code ec;
            auto rel = fs::relative(path, root, ec);
            if (!ec && !rel.empty()) {
                return rel.generic_string();
            }
        }

        return path.generic_string();
    }

    /**
     * Generates a unique suggestion ID from a prefix and file path.
     *
     * Uses hash of the full path to avoid collisions when multiple files
     * have the same filename in different directories.
     *
     * @param prefix Suggestion type prefix (e.g., "pch", "fwd", "unused")
     * @param path File path to include in the ID
     * @param suffix Optional additional suffix for disambiguation
     * @return Unique suggestion ID string
     */
    [[nodiscard]] inline std::string generate_suggestion_id(
        const std::string_view prefix,
        const fs::path& path,
        const std::string_view suffix = ""
    ) {
        std::ostringstream oss;
        oss << prefix << "-";

        const std::uint64_t path_hash = std::hash<std::string>{}(path.string());
        oss << std::hex << std::setw(16) << std::setfill('0') << path_hash;

        // Readable filename for human identification
        oss << "-" << path.filename().string();

        if (!suffix.empty()) {
            oss << "-" << suffix;
        }

        return oss.str();
    }

    /**
     * Generates a unique suggestion ID from a prefix and counter.
     *
     * Use this variant when there's no natural file path (e.g., unity builds).
     *
     * @param prefix Suggestion type prefix
     * @param counter Unique counter value
     * @param name Optional descriptive name
     * @return Unique suggestion ID string
     */
    [[nodiscard]] inline std::string generate_suggestion_id(
        const std::string_view prefix,
        const std::size_t counter,
        const std::string_view name = ""
    ) {
        std::ostringstream oss;
        oss << prefix << "-" << counter;
        if (!name.empty()) {
            oss << "-" << name;
        }
        return oss.str();
    }

    struct IncludeDirective {
        std::size_t line = 0;
        std::size_t col_start = 0;
        std::size_t col_end = 0;
        std::string header_name;
        bool is_system = false;
    };

    [[nodiscard]] inline std::vector<IncludeDirective> find_include_directives(const fs::path& file) {
        std::vector<IncludeDirective> result;

        std::ifstream in(file);
        if (!in) return result;

        std::regex include_regex(R"(^\s*#\s*include\s*([<"])([^">]+)[">])", std::regex::ECMAScript);
        std::string line;
        std::size_t line_num = 0;

        while (std::getline(in, line)) {
            if (std::smatch match; std::regex_search(line, match, include_regex)) {
                IncludeDirective directive;
                directive.line = line_num;
                directive.col_start = static_cast<std::size_t>(match.position(0));
                directive.col_end = directive.col_start + static_cast<std::size_t>(match[0].length());
                directive.header_name = match[2].str();
                directive.is_system = (match[1].str() == "<");
                result.push_back(directive);
            }
            ++line_num;
        }

        return result;
    }

    [[nodiscard]] inline std::optional<IncludeDirective> find_include_for_header(
        const fs::path& file,
        const std::string& header_name
    ) {
        const fs::path target_path(header_name);
        const std::string target_generic = target_path.generic_string();
        for (const auto directives = find_include_directives(file); const auto& dir : directives) {
            const fs::path include_path(dir.header_name);
            const std::string include_generic = include_path.generic_string();
            if (include_generic == target_generic ||
                include_path.filename() == target_path.filename()) {
                return dir;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline TextEdit make_delete_line_edit(const fs::path& file, std::size_t line) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = line;
        edit.start_col = 0;
        edit.end_line = line + 1;
        edit.end_col = 0;
        edit.new_text = "";
        return edit;
    }

    [[nodiscard]] inline TextEdit make_replace_line_edit(
        const fs::path& file,
        const std::size_t line,
        const std::string& new_content
    ) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = line;
        edit.start_col = 0;
        edit.end_line = line + 1;
        edit.end_col = 0;
        edit.new_text = new_content + "\n";
        return edit;
    }

    [[nodiscard]] inline TextEdit make_insert_after_line_edit(
        const fs::path& file,
        const std::size_t line,
        const std::string& content
    ) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = line + 1;
        edit.start_col = 0;
        edit.end_line = line + 1;
        edit.end_col = 0;
        edit.new_text = content + "\n";
        return edit;
    }

    [[nodiscard]] inline TextEdit make_insert_at_start_edit(
        const fs::path& file,
        const std::string& content
    ) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = 0;
        edit.start_col = 0;
        edit.end_line = 0;
        edit.end_col = 0;
        edit.new_text = content + "\n";
        return edit;
    }

    [[nodiscard]] inline std::size_t find_first_include_line(const fs::path& file) {
        const auto directives = find_include_directives(file);
        if (directives.empty()) return 0;
        return directives.front().line;
    }

    [[nodiscard]] inline std::size_t find_last_include_line(const fs::path& file) {
        const auto directives = find_include_directives(file);
        if (directives.empty()) return 0;
        return directives.back().line;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_pragma_once_line(const fs::path& file) {
        std::ifstream in(file);
        if (!in) return std::nullopt;

        std::string line;
        std::size_t line_num = 0;
        std::regex pragma_regex(R"(^\s*#\s*pragma\s+once\b)");
        while (std::getline(in, line)) {
            if (std::regex_search(line, pragma_regex)) {
                return line_num;
            }
            ++line_num;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_include_insertion_line(const fs::path& file) {
        const auto directives = find_include_directives(file);
        if (!directives.empty()) {
            return directives.back().line;
        }
        return find_pragma_once_line(file);
    }

    [[nodiscard]] inline std::size_t end_of_file_insert_line(const std::string& content) {
        return static_cast<std::size_t>(std::count(content.begin(), content.end(), '\n')) + 1;
    }

    struct ClassMemberInfo {
        std::string name;
        std::string type;
        std::size_t line = 0;
        std::size_t col = 0;
        bool is_private = false;
        bool is_method = false;
        bool is_static = false;
        bool is_virtual = false;
    };

    struct ClassInfo {
        std::string name;
        std::string full_name;
        fs::path file;
        std::size_t class_start_line = 0;
        std::size_t class_end_line = 0;
        std::size_t private_section_line = 0;
        std::vector<ClassMemberInfo> members;
        std::vector<std::string> base_classes;
        bool has_destructor = false;
        bool has_copy_constructor = false;
        bool has_move_constructor = false;
    };

    [[nodiscard]] inline std::optional<ClassInfo> parse_class_with_clang(
        const fs::path& file,
        const std::string& class_name
    ) {
        static std::once_flag clang_once;
        static bool clang_available = false;
        std::call_once(clang_once, [] {
#ifdef _WIN32
            FILE* test = popen("where clang 2>NUL", "r");
#else
            FILE* test = popen("which clang 2>/dev/null", "r");
#endif
            if (test) {
                char buf[256];
                clang_available = (fgets(buf, sizeof(buf), test) != nullptr);
                pclose(test);
            }
        });
        if (!clang_available) return std::nullopt;

#ifdef _WIN32
        auto escape_path = [](const std::string& input) {
            std::string escaped;
            escaped.reserve(input.size());
            for (char c : input) {
                if (c == '"') {
                    escaped += "\\\"";
                } else {
                    escaped += c;
                }
            }
            return escaped;
        };
        std::string escaped_path = escape_path(file.string());
        std::string cmd = "clang -Xclang -ast-dump=json -fsyntax-only \"" + escaped_path + "\" 2>NUL";
#else
        auto escape_path = [](const std::string& input) {
            std::string escaped;
            escaped.reserve(input.size() + 2);
            escaped.push_back('\'');
            for (char c : input) {
                if (c == '\'') {
                    escaped += "'\\''";
                } else {
                    escaped.push_back(c);
                }
            }
            escaped.push_back('\'');
            return escaped;
        };
        std::string escaped_path = escape_path(file.string());
        std::string cmd = "clang -Xclang -ast-dump=json -fsyntax-only " + escaped_path + " 2>/dev/null";
#endif

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return std::nullopt;

        std::string output;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
            // Limit output to prevent hanging on large AST dumps
            if (output.size() > 10 * 1024 * 1024) {  // 10 MB limit
                pclose(pipe);
                return std::nullopt;
            }
        }
        if (int status = pclose(pipe); status != 0 || output.empty()) return std::nullopt;

        ClassInfo info;
        info.name = class_name;
        info.file = file;

        if (std::regex class_regex(R"("kind"\s*:\s*"CXXRecordDecl"[^}]*"name"\s*:\s*")" + class_name + R"(")"); !std::regex_search(output, class_regex)) {
            return std::nullopt;
        }

        std::regex line_regex(R"("line"\s*:\s*(\d+))");
        std::smatch match;
        if (std::regex_search(output, match, line_regex)) {
            info.class_start_line = std::stoul(match[1].str());
        }

        std::regex field_regex(R"re("kind"\s*:\s*"FieldDecl"[^}]*"name"\s*:\s*"([^"]+)"[^}]*"line"\s*:\s*(\d+))re");
        auto search_start = output.cbegin();
        while (std::regex_search(search_start, output.cend(), match, field_regex)) {
            ClassMemberInfo member;
            member.name = match[1].str();
            member.line = std::stoul(match[2].str());
            member.is_private = true;
            info.members.push_back(member);
            search_start = match.suffix().first;
        }

        return info;
    }

    [[nodiscard]] inline std::optional<ClassInfo> parse_class_simple(
        const fs::path& file,
        const std::string& class_name
    ) {
        std::ifstream in(file);
        if (!in) return std::nullopt;

        ClassInfo info;
        info.name = class_name;
        info.file = file;

        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

        std::regex class_regex(R"(\bclass\s+)" + class_name + R"(\s*(?::\s*[^{]+)?\s*\{)");
        std::smatch class_match;
        if (!std::regex_search(content, class_match, class_regex)) {
            return std::nullopt;
        }

        auto class_pos = static_cast<std::size_t>(class_match.position());
        std::size_t line_num = 1;
        for (std::size_t i = 0; i < class_pos; ++i) {
            if (content[i] == '\n') ++line_num;
        }
        info.class_start_line = line_num;

        std::size_t brace_pos = content.find('{', class_pos);
        if (brace_pos == std::string::npos) return std::nullopt;

        int brace_count = 1;
        std::size_t end_pos = brace_pos + 1;
        while (end_pos < content.size() && brace_count > 0) {
            if (content[end_pos] == '{') ++brace_count;
            else if (content[end_pos] == '}') --brace_count;
            ++end_pos;
        }

        std::string class_body = content.substr(brace_pos + 1, end_pos - brace_pos - 2);

        line_num = info.class_start_line;
        for (std::size_t i = class_pos; i < brace_pos; ++i) {
            if (content[i] == '\n') ++line_num;
        }

        bool in_private = false;
        std::size_t body_line = line_num;
        std::istringstream body_stream(class_body);
        std::string line;

        while (std::getline(body_stream, line)) {
            ++body_line;

            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

            if (trimmed.find("private:") == 0 || trimmed.find("private :") == 0) {
                in_private = true;
                if (info.private_section_line == 0) {
                    info.private_section_line = body_line;
                }
                continue;
            }
            if (trimmed.find("public:") == 0 || trimmed.find("protected:") == 0) {
                in_private = false;
                continue;
            }

            if (in_private && !trimmed.empty() && trimmed[0] != '/' && trimmed.find("//") != 0) {
                std::regex member_regex(R"(^\s*([a-zA-Z_][a-zA-Z0-9_:<>,\s\*&]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*[;=])");
                std::smatch member_match;
                if (std::regex_search(trimmed, member_match, member_regex)) {
                    ClassMemberInfo member;
                    member.type = member_match[1].str();
                    member.name = member_match[2].str();
                    member.line = body_line;
                    member.is_private = true;
                    member.is_method = false;
                    info.members.push_back(member);
                }

                if (std::regex method_regex(R"(^\s*(?:virtual\s+)?([a-zA-Z_][a-zA-Z0-9_:<>,\s\*&]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\()"); std::regex_search(trimmed, member_match, method_regex)) {
                    ClassMemberInfo member;
                    member.type = member_match[1].str();
                    member.name = member_match[2].str();
                    member.line = body_line;
                    member.is_private = true;
                    member.is_method = true;
                    info.members.push_back(member);
                }
            }

            if (trimmed.find("~" + class_name) != std::string::npos) {
                info.has_destructor = true;
            }
            if (trimmed.find(class_name + "(const " + class_name + "&") != std::string::npos ||
                trimmed.find(class_name + "( const " + class_name + " &") != std::string::npos) {
                info.has_copy_constructor = true;
            }
            if (trimmed.find(class_name + "(" + class_name + "&&") != std::string::npos ||
                trimmed.find(class_name + "( " + class_name + " &&") != std::string::npos) {
                info.has_move_constructor = true;
            }
        }

        info.class_end_line = body_line;

        return info;
    }

    [[nodiscard]] inline std::vector<TextEdit> generate_pimpl_edits(
        const ClassInfo& class_info,
        const fs::path& header_file,
        const fs::path& source_file
    ) {
        std::vector<TextEdit> edits;

        if (class_info.members.empty()) return edits;

        std::vector<ClassMemberInfo> private_data_members;
        std::vector<ClassMemberInfo> private_methods;

        for (const auto& member : class_info.members) {
            if (member.is_private && !member.is_static) {
                if (member.is_method) {
                    private_methods.push_back(member);
                } else {
                    private_data_members.push_back(member);
                }
            }
        }

        if (private_data_members.empty()) return edits;

        std::ostringstream pimpl_decl;
        pimpl_decl << "    struct Impl;\n";
        pimpl_decl << "    std::unique_ptr<Impl> pimpl_;\n";

        if (class_info.private_section_line > 0) {
            TextEdit replace_private;
            replace_private.file = header_file;
            replace_private.start_line = class_info.private_section_line - 1;
            replace_private.start_col = 0;

            std::size_t last_member_line = class_info.private_section_line;
            for (const auto& member : private_data_members) {
                if (member.line > last_member_line) {
                    last_member_line = member.line;
                }
            }

            replace_private.end_line = last_member_line;
            replace_private.end_col = 0;
            replace_private.new_text = "private:\n" + pimpl_decl.str();
            edits.push_back(replace_private);
        }

        std::size_t first_include = find_first_include_line(header_file);
        bool has_memory_include = false;

        for (auto includes = find_include_directives(header_file); const auto& inc : includes) {
            if (inc.header_name == "memory") {
                has_memory_include = true;
                break;
            }
        }

        if (!has_memory_include) {
            TextEdit add_memory;
            add_memory.file = header_file;
            add_memory.start_line = first_include;
            add_memory.start_col = 0;
            add_memory.end_line = first_include;
            add_memory.end_col = 0;
            add_memory.new_text = "#include <memory>\n";
            edits.push_back(add_memory);
        }

        std::ostringstream impl_definition;
        impl_definition << "\n// PIMPL Implementation\n";
        impl_definition << "struct " << class_info.name << "::Impl {\n";

        for (const auto& member : private_data_members) {
            impl_definition << "    " << member.type << " " << member.name << ";\n";
        }

        impl_definition << "};\n\n";

        impl_definition << class_info.name << "::" << class_info.name << "()\n";
        impl_definition << "    : pimpl_(std::make_unique<Impl>())\n";
        impl_definition << "{}\n\n";

        impl_definition << class_info.name << "::~" << class_info.name << "() = default;\n\n";

        impl_definition << class_info.name << "::" << class_info.name << "(" << class_info.name << "&&) noexcept = default;\n";
        impl_definition << class_info.name << "& " << class_info.name << "::operator=(" << class_info.name << "&&) noexcept = default;\n";

        if (fs::exists(source_file)) {
            std::ifstream src_in(source_file);
            std::string src_content((std::istreambuf_iterator<char>(src_in)),
                                    std::istreambuf_iterator<char>());
            src_in.close();

            std::size_t last_line = end_of_file_insert_line(src_content);

            TextEdit add_impl;
            add_impl.file = source_file;
            add_impl.start_line = last_line;
            add_impl.start_col = 0;
            add_impl.end_line = last_line;
            add_impl.end_col = 0;
            add_impl.new_text = impl_definition.str();
            edits.push_back(add_impl);
        } else {
            TextEdit create_source;
            create_source.file = source_file;
            create_source.start_line = 0;
            create_source.start_col = 0;
            create_source.end_line = 0;
            create_source.end_col = 0;

            std::ostringstream new_source;
            new_source << "#include \"" << header_file.filename().string() << "\"\n";
            new_source << impl_definition.str();
            create_source.new_text = new_source.str();
            edits.push_back(create_source);
        }

        return edits;
    }

    /**
     * Result of suggestion generation.
     */
    struct SuggestionResult {
        std::vector<Suggestion> suggestions{};
        Duration generation_time = Duration::zero();
        std::size_t items_analyzed = 0;
        std::size_t items_skipped = 0;
    };

    /**
     * Interface for suggestion generators.
     *
     * Each suggester produces a specific type of optimization suggestion.
     * Suggesters are stateless and thread-safe for concurrent use.
     */
    class ISuggester {
    public:
        virtual ~ISuggester() = default;

        /**
         * Returns the suggester's unique identifier.
         */
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;

        /**
         * Returns a human-readable description.
         */
        [[nodiscard]] virtual std::string_view description() const noexcept = 0;

        /**
         * Returns the type of suggestions this suggester produces.
         */
        [[nodiscard]] virtual SuggestionType suggestion_type() const noexcept = 0;

        /**
         * Returns all suggestion types this suggester can emit.
         *
         * By default suggesters emit a single type matching suggestion_type().
         */
        [[nodiscard]] virtual std::vector<SuggestionType> supported_types() const {
            return {suggestion_type()};
        }

        /**
         * Generates suggestions from the analysis context.
         *
         * @param context The analysis context with trace and results
         * @return Suggestions or an error
         */
        [[nodiscard]] virtual Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const = 0;
    };

    /**
     * Registry for all available suggesters.
     */
    class SuggesterRegistry {
    public:
        static SuggesterRegistry& instance();

        void register_suggester(std::unique_ptr<ISuggester> suggester);

        [[nodiscard]] const std::vector<std::unique_ptr<ISuggester>>& suggesters() const noexcept {
            return suggesters_;
        }

        [[nodiscard]] const ISuggester* find(std::string_view name) const;

    private:
        SuggesterRegistry() = default;
        std::vector<std::unique_ptr<ISuggester>> suggesters_;
    };

    /**
     * Runs all registered suggesters and collects results.
     *
     * @param trace The build trace data
     * @param analysis The analysis results
     * @param options Suggester configuration
     * @return All suggestions sorted by priority and impact
     */
    [[nodiscard]] Result<std::vector<Suggestion>, Error> generate_all_suggestions(
        const BuildTrace& trace,
        const analyzers::AnalysisResult& analysis,
        const SuggesterOptions& options,
        const fs::path& project_root = {}
    );

}  // namespace bha::suggestions

#endif //BHA_SUGGESTER_HPP
