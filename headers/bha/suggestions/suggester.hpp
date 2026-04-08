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
#include <cmath>
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

        std::optional<std::chrono::steady_clock::time_point> deadline;

        /// Optional filter for incremental analysis. When set, only analyze
        /// files in this list. Empty means analyze all files.
        std::vector<fs::path> target_files;
        std::unordered_set<std::string> target_files_lookup;

        SuggestionContext(
            const BuildTrace& trace_ref,
            const analyzers::AnalysisResult& analysis_ref,
            const SuggesterOptions& options_ref,
            fs::path root,
            std::atomic<bool>* cancelled_ptr = nullptr,
            std::optional<std::chrono::steady_clock::time_point> deadline_value = std::nullopt,
            std::vector<fs::path> files = {},
            std::unordered_set<std::string> lookup = {}
        )
            : trace(trace_ref),
              analysis(analysis_ref),
              options(options_ref),
              project_root(std::move(root)),
              cancelled(cancelled_ptr),
              deadline(std::move(deadline_value)),
              target_files(std::move(files)),
              target_files_lookup(std::move(lookup)) {}

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
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
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
        if (repo_root.empty() || filename.empty()) {
            return std::nullopt;
        }

        const std::string key = repo_root.string() + "|" + filename.string();
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second.empty() ? std::nullopt : std::optional<fs::path>(it->second);
        }

        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(repo_root, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
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
                if (ec) {
                    break;
                }
                if (!entry.is_directory()) {
                    continue;
                }
                if (++checked > 200) {
                    break;
                }
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
                    if (child_ec) {
                        break;
                    }
                    if (!child.is_directory()) {
                        continue;
                    }
                    if (++checked > 500) {
                        break;
                    }
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
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (++checked > 200) {
                    break;
                }
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
                    if (ec) {
                        break;
                    }
                    if (!entry.is_directory()) {
                        continue;
                    }
                    if (++checked_dirs > 300) {
                        break;
                    }
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

    [[nodiscard]] inline std::string trim_whitespace_copy(std::string value) {
        if (const auto first = value.find_first_not_of(" \t\r\n"); first != std::string::npos) {
            value.erase(0, first);
        } else {
            value.clear();
        }
        if (!value.empty()) {
            if (const auto last = value.find_last_not_of(" \t\r\n"); last != std::string::npos) {
                value.erase(last + 1);
            }
        }
        return value;
    }

    [[nodiscard]] inline std::string strip_comments_and_strings(
        const std::string& line,
        bool& in_block_comment
    ) {
        std::string cleaned;
        cleaned.reserve(line.size());

        bool in_string = false;
        char quote_char = '\0';
        bool escape_next = false;

        for (std::size_t i = 0; i < line.size(); ++i) {
            const char ch = line[i];
            const char next = (i + 1 < line.size()) ? line[i + 1] : '\0';

            if (in_block_comment) {
                if (ch == '*' && next == '/') {
                    in_block_comment = false;
                    ++i;
                }
                continue;
            }

            if (in_string) {
                if (escape_next) {
                    escape_next = false;
                } else if (ch == '\\') {
                    escape_next = true;
                } else if (ch == quote_char) {
                    in_string = false;
                }
                cleaned.push_back(' ');
                continue;
            }

            if (ch == '/' && next == '/') {
                break;
            }
            if (ch == '/' && next == '*') {
                in_block_comment = true;
                ++i;
                continue;
            }
            if (ch == '"' || ch == '\'') {
                in_string = true;
                quote_char = ch;
                cleaned.push_back(' ');
                continue;
            }

            cleaned.push_back(ch);
        }

        return cleaned;
    }

    [[nodiscard]] inline bool is_identifier_char(const char ch) {
        const unsigned char value = static_cast<unsigned char>(ch);
        return std::isalnum(value) || ch == '_';
    }

    [[nodiscard]] inline bool contains_identifier_token(
        const std::string& text,
        const std::string& symbol
    ) {
        if (text.empty() || symbol.empty()) {
            return false;
        }

        std::size_t pos = text.find(symbol);
        while (pos != std::string::npos) {
            const bool left_ok = (pos == 0) || !is_identifier_char(text[pos - 1]);
            const std::size_t end = pos + symbol.size();
            const bool right_ok = (end >= text.size()) || !is_identifier_char(text[end]);
            if (left_ok && right_ok) {
                return true;
            }
            pos = text.find(symbol, pos + 1);
        }
        return false;
    }

    enum class ExportedTypeSymbolKind {
        ForwardDeclarableType,
        AliasLike,
    };

    struct ExportedTypeSymbol {
        std::string name;
        ExportedTypeSymbolKind kind = ExportedTypeSymbolKind::ForwardDeclarableType;
    };

    [[nodiscard]] inline bool is_forward_declarable(const ExportedTypeSymbol& symbol) {
        return symbol.kind == ExportedTypeSymbolKind::ForwardDeclarableType;
    }

    [[nodiscard]] inline std::vector<ExportedTypeSymbol> extract_exported_type_symbols(
        const fs::path& header_path
    ) {
        auto lines_result = file_utils::read_lines(header_path);
        if (lines_result.is_err()) {
            return {};
        }

        static const std::regex class_or_struct_regex(
            R"(^\s*(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"
        );
        static const std::regex using_alias_regex(
            R"(^\s*using\s+([A-Za-z_][A-Za-z0-9_]*)\b)"
        );
        static const std::regex typedef_regex(
            R"(^\s*typedef\b.*\b([A-Za-z_][A-Za-z0-9_]*)\s*;)"
        );
        static const std::regex enum_regex(
            R"(^\s*enum(?:\s+class|\s+struct)?\s+([A-Za-z_][A-Za-z0-9_]*)\b)"
        );

        std::vector<ExportedTypeSymbol> symbols;
        std::unordered_map<std::string, ExportedTypeSymbolKind> kinds_by_name;
        bool in_block_comment = false;
        bool template_pending = false;

        const auto record_symbol = [&](const std::string& name, const ExportedTypeSymbolKind kind) {
            if (name.empty()) {
                return;
            }
            auto it = kinds_by_name.find(name);
            if (it == kinds_by_name.end()) {
                kinds_by_name.emplace(name, kind);
                symbols.push_back(ExportedTypeSymbol{name, kind});
                return;
            }
            if (it->second == kind) {
                return;
            }
            it->second = ExportedTypeSymbolKind::AliasLike;
            for (auto& symbol : symbols) {
                if (symbol.name == name) {
                    symbol.kind = ExportedTypeSymbolKind::AliasLike;
                    break;
                }
            }
        };

        for (const auto& raw_line : lines_result.value()) {
            const std::string line = strip_comments_and_strings(raw_line, in_block_comment);
            const std::string trimmed = trim_whitespace_copy(line);

            if (trimmed.empty()) {
                template_pending = false;
                continue;
            }
            if (trimmed == "template" || trimmed.starts_with("template<")) {
                template_pending = true;
                continue;
            }
            if (template_pending) {
                if (trimmed.find(';') != std::string::npos || trimmed.find('{') != std::string::npos) {
                    template_pending = false;
                }
                continue;
            }
            if (trimmed.starts_with("#") ||
                trimmed.starts_with("namespace ") ||
                trimmed == "{" ||
                trimmed == "}" ||
                trimmed == "};") {
                continue;
            }

            std::smatch match;
            if (std::regex_search(trimmed, match, class_or_struct_regex)) {
                if (trimmed.find('{') == std::string::npos && trimmed.find(';') == std::string::npos) {
                    continue;
                }
                record_symbol(match[2].str(), ExportedTypeSymbolKind::ForwardDeclarableType);
                continue;
            }
            if (std::regex_search(trimmed, match, using_alias_regex)) {
                if (trimmed.find('=') == std::string::npos) {
                    continue;
                }
                record_symbol(match[1].str(), ExportedTypeSymbolKind::AliasLike);
                continue;
            }
            if (std::regex_search(trimmed, match, typedef_regex)) {
                record_symbol(match[1].str(), ExportedTypeSymbolKind::AliasLike);
                continue;
            }
            if (std::regex_search(trimmed, match, enum_regex)) {
                record_symbol(match[1].str(), ExportedTypeSymbolKind::AliasLike);
            }
        }

        return symbols;
    }

    struct IncompleteTypeUsageSummary {
        bool has_mentions = false;
        bool requires_complete_type = false;
        std::size_t pointer_or_reference_mentions = 0;
    };

    [[nodiscard]] inline bool is_reference_or_pointer_context(
        const std::string& text,
        const std::size_t end_pos
    ) {
        auto skip_ws = [&](std::size_t pos) {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            return pos;
        };

        std::size_t pos = skip_ws(end_pos);
        while (pos < text.size()) {
            if (text.compare(pos, 5, "const") == 0 &&
                (pos + 5 >= text.size() || !is_identifier_char(text[pos + 5]))) {
                pos = skip_ws(pos + 5);
                continue;
            }
            if (text.compare(pos, 8, "volatile") == 0 &&
                (pos + 8 >= text.size() || !is_identifier_char(text[pos + 8]))) {
                pos = skip_ws(pos + 8);
                continue;
            }
            break;
        }
        return pos < text.size() && (text[pos] == '*' || text[pos] == '&');
    }

    [[nodiscard]] inline bool line_looks_like_forward_declaration(
        const std::string& text,
        const std::size_t start,
        const std::size_t end_pos
    ) {
        const auto line_start = text.rfind('\n', start);
        const auto line_end = text.find('\n', end_pos);
        const std::size_t begin = line_start == std::string::npos ? 0 : line_start + 1;
        const std::size_t end = line_end == std::string::npos ? text.size() : line_end;
        const std::string line = trim_whitespace_copy(text.substr(begin, end - begin));
        return line.starts_with("class ") || line.starts_with("struct ");
    }

    [[nodiscard]] inline std::vector<std::string> collect_pointer_or_reference_identifiers(
        const std::string& text,
        const std::vector<std::pair<std::size_t, std::size_t>>& mentions
    ) {
        std::vector<std::string> identifiers;
        std::unordered_set<std::string> seen;

        auto skip_ws = [&](std::size_t pos) {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            return pos;
        };

        for (const auto& [start, end_pos] : mentions) {
            std::size_t pos = skip_ws(end_pos);
            while (pos < text.size()) {
                if (text.compare(pos, 5, "const") == 0 &&
                    (pos + 5 >= text.size() || !is_identifier_char(text[pos + 5]))) {
                    pos = skip_ws(pos + 5);
                    continue;
                }
                if (text.compare(pos, 8, "volatile") == 0 &&
                    (pos + 8 >= text.size() || !is_identifier_char(text[pos + 8]))) {
                    pos = skip_ws(pos + 8);
                    continue;
                }
                break;
            }

            if (pos >= text.size() || (text[pos] != '*' && text[pos] != '&')) {
                continue;
            }
            while (pos < text.size() && (text[pos] == '*' || text[pos] == '&' ||
                   std::isspace(static_cast<unsigned char>(text[pos])))) {
                ++pos;
            }
            if (pos < text.size() && is_identifier_char(text[pos])) {
                const std::size_t id_start = pos;
                while (pos < text.size() && is_identifier_char(text[pos])) {
                    ++pos;
                }
                const std::string name = text.substr(id_start, pos - id_start);
                if (!name.empty() && seen.insert(name).second) {
                    identifiers.push_back(name);
                }
            }
        }

        return identifiers;
    }

    [[nodiscard]] inline bool has_member_access_on_identifiers(
        const std::string& text,
        const std::vector<std::string>& identifiers
    ) {
        for (const auto& identifier : identifiers) {
            const std::string pointer_access = identifier + "->";
            const std::string dot_access = identifier + ".";
            if (text.find(pointer_access) != std::string::npos ||
                text.find(dot_access) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline IncompleteTypeUsageSummary analyze_incomplete_type_usage(
        const std::string& sanitized_text,
        const std::vector<std::string>& type_spellings
    ) {
        IncompleteTypeUsageSummary result;
        if (sanitized_text.empty() || type_spellings.empty()) {
            return result;
        }

        std::vector<std::pair<std::size_t, std::size_t>> mentions;
        auto add_mention = [&mentions](const std::size_t start, const std::size_t end_pos) {
            const bool exists = std::ranges::any_of(
                mentions,
                [start, end_pos](const auto& span) { return span.first == start && span.second == end_pos; }
            );
            if (!exists) {
                mentions.emplace_back(start, end_pos);
            }
        };

        for (const auto& spelling : type_spellings) {
            if (spelling.empty()) {
                continue;
            }
            const std::string escaped = std::regex_replace(
                spelling,
                std::regex(R"([.^$|()\\[\]{}*+?])"),
                R"(\$&)"
            );

            const std::regex forbidden_constructs(
                "\\b(?:sizeof|alignof|typeid|new|delete|dynamic_cast|static_cast)\\s*(?:\\(|)\\s*" + escaped + "\\b|"
                "\\b" + escaped + "\\s*::|"
                "<\\s*" + escaped + "\\b"
            );
            if (std::regex_search(sanitized_text, forbidden_constructs)) {
                result.requires_complete_type = true;
                return result;
            }

            const std::regex inheritance_regex(
                "\\b(?:class|struct)\\s+[A-Za-z_][A-Za-z0-9_]*\\s*:[^\\{;]*\\b" + escaped + "\\b"
            );
            if (std::regex_search(sanitized_text, inheritance_regex)) {
                result.requires_complete_type = true;
                return result;
            }

            const std::regex symbol_regex("\\b" + escaped + "\\b");
            for (auto begin = std::sregex_iterator(sanitized_text.begin(), sanitized_text.end(), symbol_regex),
                      end = std::sregex_iterator();
                 begin != end;
                 ++begin) {
                const std::size_t start = static_cast<std::size_t>((*begin).position());
                const std::size_t symbol_end = start + static_cast<std::size_t>((*begin).length());
                add_mention(start, symbol_end);
            }
        }

        std::ranges::sort(mentions, [](const auto& lhs, const auto& rhs) {
            if (lhs.first != rhs.first) {
                return lhs.first < rhs.first;
            }
            return lhs.second < rhs.second;
        });

        const auto pointer_identifiers = collect_pointer_or_reference_identifiers(sanitized_text, mentions);
        if (has_member_access_on_identifiers(sanitized_text, pointer_identifiers)) {
            result.requires_complete_type = true;
            return result;
        }

        for (const auto& [start, end_pos] : mentions) {
            if (line_looks_like_forward_declaration(sanitized_text, start, end_pos)) {
                continue;
            }

            result.has_mentions = true;
            if (is_reference_or_pointer_context(sanitized_text, end_pos)) {
                ++result.pointer_or_reference_mentions;
                continue;
            }

            result.requires_complete_type = true;
            return result;
        }

        return result;
    }

    [[nodiscard]] inline std::vector<std::string> extract_qualified_names(const std::string& text) {
        static const std::regex qualified_name_regex(
            R"(\b([A-Za-z_]\w*(?:::[A-Za-z_]\w*)+)\b)"
        );

        std::vector<std::string> names;
        std::unordered_set<std::string> seen;
        for (std::sregex_iterator it(text.begin(), text.end(), qualified_name_regex),
             end; it != end; ++it) {
            const std::string candidate = (*it)[1].str();
            if (seen.insert(candidate).second) {
                names.push_back(candidate);
            }
        }
        return names;
    }

    [[nodiscard]] inline bool is_nested_qualified_name(const std::string& qualified_name) {
        std::size_t separators = 0;
        for (std::size_t pos = qualified_name.find("::");
             pos != std::string::npos;
             pos = qualified_name.find("::", pos + 2)) {
            ++separators;
        }
        return separators >= 2;
    }

    template <std::size_t N>
    [[nodiscard]] inline bool path_has_extension(
        const fs::path& path,
        const std::array<std::string_view, N>& extensions
    ) {
        const std::string ext = path.extension().string();
        return std::ranges::any_of(extensions, [&](std::string_view candidate) {
            return ext == candidate;
        });
    }

    [[nodiscard]] inline fs::path resolve_project_path(
        const fs::path& path,
        const fs::path& project_root
    ) {
        fs::path resolved = resolve_source_path(path);
        if (resolved.is_relative() && !project_root.empty()) {
            resolved = (project_root / resolved).lexically_normal();
        } else {
            resolved = resolved.lexically_normal();
        }
        return resolved;
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
        if (!in) {
            return result;
        }

        const std::regex include_regex(R"(^\s*#\s*include\s*([<"])([^">]+)[">])", std::regex::ECMAScript);
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

    [[nodiscard]] inline const std::vector<std::string>& default_protected_include_patterns() {
        static const std::vector<std::string> patterns = {
            R"((^|.*/)(port|platform|compat|abi|os|sys|config)/)",
            R"((^|.*/)(windows|winuser|winbase|windef|winnt|winsock|winsock2|pthread|unistd|malloc|intrin|io|direct)\.h$)"
        };
        return patterns;
    }

    [[nodiscard]] inline bool matches_protected_include_policy(
        const std::string& header_name,
        const std::optional<fs::path>& resolved_header,
        const std::vector<std::string>& protected_include_patterns
    ) {
        const auto& patterns = protected_include_patterns.empty()
            ? default_protected_include_patterns()
            : protected_include_patterns;
        if (patterns.empty()) {
            return false;
        }

        std::vector<std::string> candidates;
        candidates.push_back(fs::path(header_name).generic_string());
        candidates.push_back(fs::path(header_name).filename().generic_string());
        if (resolved_header.has_value()) {
            candidates.push_back(resolved_header->lexically_normal().generic_string());
            candidates.push_back(resolved_header->filename().generic_string());
        }

        for (const auto& pattern : patterns) {
            std::regex compiled_pattern;
            try {
                compiled_pattern = std::regex(pattern, std::regex::ECMAScript | std::regex::icase);
            } catch (const std::regex_error&) {
                continue;
            }

            for (const auto& candidate : candidates) {
                if (!candidate.empty() && std::regex_search(candidate, compiled_pattern)) {
                    return true;
                }
            }
        }

        return false;
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

    enum class IncludeInsertionKind {
        ExistingIncludeBlock,
        HeaderGuard,
        LeadingPreamble,
        StartOfFile
    };

    struct IncludeInsertionEdit {
        TextEdit edit;
        std::size_t inserted_line_one_based = 1;
        IncludeInsertionKind kind = IncludeInsertionKind::StartOfFile;
    };

    class GeneratedTextBuilder {
    public:
        void add_line(std::string line) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines_.push_back(std::move(line));
        }

        void add_blank_line() {
            if (lines_.empty() || lines_.back().empty()) {
                return;
            }
            lines_.emplace_back();
        }

        void add_block(std::string_view block) {
            std::size_t offset = 0;
            while (offset <= block.size()) {
                const auto next = block.find('\n', offset);
                const auto chunk = next == std::string_view::npos
                    ? block.substr(offset)
                    : block.substr(offset, next - offset);
                if (chunk.empty()) {
                    add_blank_line();
                } else {
                    add_line(std::string(chunk));
                }
                if (next == std::string_view::npos) {
                    break;
                }
                offset = next + 1;
            }
        }

        [[nodiscard]] bool empty() const {
            return lines_.empty();
        }

        [[nodiscard]] std::string str() const {
            std::ostringstream out;
            for (std::size_t i = 0; i < lines_.size(); ++i) {
                out << lines_[i];
                if (i + 1 < lines_.size() || !lines_.empty()) {
                    out << '\n';
                }
            }
            return out.str();
        }

    private:
        std::vector<std::string> lines_;
    };

    inline void append_include_block(
        GeneratedTextBuilder& builder,
        const std::vector<std::string>& include_lines
    ) {
        if (include_lines.empty()) {
            return;
        }
        for (const auto& include_line : include_lines) {
            builder.add_line(include_line);
        }
        builder.add_blank_line();
    }

    [[nodiscard]] inline std::string format_separated_block(std::string block) {
        block = trim_whitespace_copy(std::move(block));
        if (block.empty()) {
            return {};
        }
        return "\n" + block + "\n";
    }

    [[nodiscard]] inline std::string_view trim_whitespace_view(const std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return std::string_view{};
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    [[nodiscard]] inline std::string_view consume_leading_block_comments(
        std::string_view text,
        bool& in_block_comment
    ) {
        std::string_view current = text;
        while (true) {
            if (in_block_comment) {
                const auto end = current.find("*/");
                if (end == std::string_view::npos) {
                    return std::string_view{};
                }
                current.remove_prefix(end + 2);
                in_block_comment = false;
            }

            current = trim_whitespace_view(current);
            if (!current.starts_with("/*")) {
                return current;
            }

            current.remove_prefix(2);
            const auto end = current.find("*/");
            if (end == std::string_view::npos) {
                in_block_comment = true;
                return std::string_view{};
            }
            current.remove_prefix(end + 2);
        }
    }

    [[nodiscard]] inline std::size_t find_first_include_line(const fs::path& file) {
        const auto directives = find_include_directives(file);
        if (directives.empty()) {
            return 0;
        }
        return directives.front().line;
    }

    [[nodiscard]] inline std::size_t find_last_include_line(const fs::path& file) {
        const auto directives = find_include_directives(file);
        if (directives.empty()) {
            return 0;
        }
        return directives.back().line;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_pragma_once_line(const fs::path& file) {
        std::ifstream in(file);
        if (!in) {
            return std::nullopt;
        }

        std::string line;
        std::size_t line_num = 0;
        const std::regex pragma_regex(R"(^\s*#\s*pragma\s+once\b)");
        while (std::getline(in, line)) {
            if (std::regex_search(line, pragma_regex)) {
                return line_num;
            }
            ++line_num;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_include_insertion_line(const fs::path& file) {
        std::ifstream in(file);
        if (!in) {
            return std::nullopt;
        }

        const std::regex include_regex(R"(^#\s*include\s*([<"])([^">]+)[">])", std::regex::ECMAScript);
        const std::regex pragma_regex(R"(^#\s*pragma\s+once\b)", std::regex::ECMAScript);
        const std::regex guard_ifndef_regex(R"(^#\s*ifndef\b)", std::regex::ECMAScript);
        const std::regex guard_if_defined_regex(R"(^#\s*if\s*!?\s*defined\s*\()", std::regex::ECMAScript);
        const std::regex define_regex(R"(^#\s*define\b)", std::regex::ECMAScript);

        std::string line;
        std::size_t line_num = 0;
        bool in_block_comment = false;
        bool in_include_block = false;
        bool expecting_guard_define = false;
        std::optional<std::size_t> pragma_once_line;
        std::optional<std::size_t> last_include_line;

        while (std::getline(in, line)) {
            std::string_view trimmed = consume_leading_block_comments(line, in_block_comment);
            if (trimmed.empty() || trimmed.starts_with("//")) {
                ++line_num;
                continue;
            }

            if (std::regex_search(trimmed.begin(), trimmed.end(), pragma_regex) && !in_include_block &&
                !last_include_line.has_value()) {
                pragma_once_line = line_num;
                ++line_num;
                continue;
            }

            if (std::regex_search(trimmed.begin(), trimmed.end(), guard_ifndef_regex) ||
                std::regex_search(trimmed.begin(), trimmed.end(), guard_if_defined_regex)) {
                if (!in_include_block && !last_include_line.has_value()) {
                    expecting_guard_define = true;
                    ++line_num;
                    continue;
                }
                break;
            }

            if (expecting_guard_define && std::regex_search(trimmed.begin(), trimmed.end(), define_regex)) {
                expecting_guard_define = false;
                ++line_num;
                continue;
            }

            if (std::regex_search(trimmed.begin(), trimmed.end(), include_regex)) {
                in_include_block = true;
                last_include_line = line_num;
                ++line_num;
                continue;
            }

            if (in_include_block) {
                break;
            }

            if (trimmed.starts_with("#")) {
                break;
            }
            break;
        }

        if (last_include_line.has_value()) {
            return last_include_line;
        }
        return pragma_once_line;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_header_guard_define_line(const fs::path& file) {
        std::ifstream in(file);
        if (!in) {
            return std::nullopt;
        }

        const std::regex ifndef_regex(R"(^\s*#\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");
        const std::regex define_regex(R"(^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\b)");

        std::string line;
        std::size_t line_num = 0;
        std::string guard_macro;
        bool found_ifndef = false;
        bool in_block_comment = false;

        while (std::getline(in, line)) {
            const std::string_view trimmed = consume_leading_block_comments(line, in_block_comment);
            std::match_results<std::string_view::const_iterator> match;

            if (!found_ifndef) {
                if (trimmed.empty() || trimmed.starts_with("//")) {
                    ++line_num;
                    continue;
                }
                if (std::regex_match(trimmed.begin(), trimmed.end(), match, ifndef_regex)) {
                    guard_macro = match[1].str();
                    found_ifndef = true;
                } else if (!trimmed.empty() && !trimmed.starts_with("#pragma once")) {
                    break;
                }
                ++line_num;
                continue;
            }

            if (trimmed.empty() || trimmed.starts_with("//")) {
                ++line_num;
                continue;
            }
            if (std::regex_match(trimmed.begin(), trimmed.end(), match, define_regex)) {
                if (match[1].str() == guard_macro) {
                    return line_num;
                }
                break;
            }
            ++line_num;
        }

        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_preferred_include_insertion_line(const fs::path& file) {
        if (auto include_line = find_include_insertion_line(file)) {
            return include_line;
        }
        return find_header_guard_define_line(file);
    }

    [[nodiscard]] inline std::optional<std::size_t> find_leading_preamble_line(const fs::path& file) {
        std::ifstream in(file);
        if (!in) {
            return std::nullopt;
        }

        std::string line;
        std::size_t line_num = 0;
        bool in_block_comment = false;
        std::optional<std::size_t> last_preamble_line;

        while (std::getline(in, line)) {
            std::string_view trimmed = consume_leading_block_comments(line, in_block_comment);
            if (trimmed.empty() || trimmed.starts_with("//")) {
                last_preamble_line = line_num;
                ++line_num;
                continue;
            }
            break;
        }

        return last_preamble_line;
    }

    [[nodiscard]] inline IncludeInsertionEdit make_preferred_include_insertion_edit(
        const fs::path& file,
        const std::string& content
    ) {
        if (auto line = find_preferred_include_insertion_line(file)) {
            auto edit = make_insert_after_line_edit(file, *line, content);
            if (find_include_directives(file).empty()) {
                edit.new_text = "\n" + content + "\n";
                return {std::move(edit), *line + 3, IncludeInsertionKind::HeaderGuard};
            }
            return {std::move(edit), *line + 2, IncludeInsertionKind::ExistingIncludeBlock};
        }
        if (auto line = find_leading_preamble_line(file)) {
            auto edit = make_insert_after_line_edit(file, *line, content);
            edit.new_text = "\n" + content + "\n";
            return {std::move(edit), *line + 3, IncludeInsertionKind::LeadingPreamble};
        }
        return {make_insert_at_start_edit(file, content), 1, IncludeInsertionKind::StartOfFile};
    }

    [[nodiscard]] inline double saturating_count_factor(
        const std::size_t count,
        const std::size_t saturation_count
    ) {
        if (count == 0) {
            return 0.0;
        }
        if (saturation_count <= 1) {
            return 1.0;
        }

        const double numerator = std::log1p(static_cast<double>(count));
        const double denominator = std::log1p(static_cast<double>(saturation_count));
        if (denominator <= 0.0) {
            return 1.0;
        }
        return std::clamp(numerator / denominator, 0.0, 1.0);
    }

    [[nodiscard]] inline Duration scaled_duration(
        const Duration duration,
        const double factor
    ) {
        if (duration <= Duration::zero() || factor <= 0.0) {
            return Duration::zero();
        }

        const auto scaled = static_cast<Duration::rep>(
            static_cast<double>(duration.count()) * factor
        );
        return Duration(std::max<Duration::rep>(0, scaled));
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
        if (!clang_available) {
            return std::nullopt;
        }

#ifdef _WIN32
        auto escape_path = [](const std::string& input) {
            std::string escaped;
            escaped.reserve(input.size());
            for (const char c : input) {
                if (c == '"') {
                    escaped += "\\\"";
                } else {
                    escaped += c;
                }
            }
            return escaped;
        };
        const std::string escaped_path = escape_path(file.string());
        const std::string cmd = "clang -Xclang -ast-dump=json -fsyntax-only \"" + escaped_path + "\" 2>NUL";
#else
        auto escape_path = [](const std::string& input) {
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
        };
        const std::string escaped_path = escape_path(file.string());
        const std::string cmd = "clang -Xclang -ast-dump=json -fsyntax-only " + escaped_path + " 2>/dev/null";
#endif

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return std::nullopt;
        }

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
        if (const int status = pclose(pipe); status != 0 || output.empty()) {
            return std::nullopt;
        }

        ClassInfo info;
        info.name = class_name;
        info.file = file;

        if (const std::regex class_regex(
                R"("kind"\s*:\s*"CXXRecordDecl"[^}]*"name"\s*:\s*")" + class_name + R"(")"
            );
            !std::regex_search(output, class_regex)) {
            return std::nullopt;
        }

        const std::regex line_regex(R"("line"\s*:\s*(\d+))");
        std::smatch match;
        if (std::regex_search(output, match, line_regex)) {
            info.class_start_line = std::stoul(match[1].str());
        }

        const std::regex field_regex(R"re("kind"\s*:\s*"FieldDecl"[^}]*"name"\s*:\s*"([^"]+)"[^}]*"line"\s*:\s*(\d+))re");
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
        if (!in) {
            return std::nullopt;
        }

        ClassInfo info;
        info.name = class_name;
        info.file = file;

        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

        const std::regex class_regex(R"(\bclass\s+)" + class_name + R"(\s*(?::\s*[^{]+)?\s*\{)");
        std::smatch class_match;
        if (!std::regex_search(content, class_match, class_regex)) {
            return std::nullopt;
        }

        auto class_pos = static_cast<std::size_t>(class_match.position());
        std::size_t line_num = 1;
        for (std::size_t i = 0; i < class_pos; ++i) {
            if (content[i] == '\n') {
                ++line_num;
            }
        }
        info.class_start_line = line_num;

        const std::size_t brace_pos = content.find('{', class_pos);
        if (brace_pos == std::string::npos) {
            return std::nullopt;
        }

        int brace_count = 1;
        std::size_t end_pos = brace_pos + 1;
        while (end_pos < content.size() && brace_count > 0) {
            if (content[end_pos] == '{') {
                ++brace_count;
            } else if (content[end_pos] == '}') {
                --brace_count;
            }
            ++end_pos;
        }

        const std::string class_body = content.substr(brace_pos + 1, end_pos - brace_pos - 2);

        line_num = info.class_start_line;
        for (std::size_t i = class_pos; i < brace_pos; ++i) {
            if (content[i] == '\n') {
                ++line_num;
            }
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
                const std::regex member_regex(
                    R"(^\s*([a-zA-Z_][a-zA-Z0-9_:<>,\s\*&]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*[;=])"
                );
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

                const std::regex method_regex(
                    R"(^\s*(?:virtual\s+)?([a-zA-Z_][a-zA-Z0-9_:<>,\s\*&]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\()"
                );
                if (std::regex_search(trimmed, member_match, method_regex)) {
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

        if (class_info.members.empty()) {
            return edits;
        }

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

        if (private_data_members.empty()) {
            return edits;
        }

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

        const std::size_t first_include = find_first_include_line(header_file);
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
            const std::string src_content((std::istreambuf_iterator<char>(src_in)),
                                          std::istreambuf_iterator<char>());
            src_in.close();

            const std::size_t last_line = end_of_file_insert_line(src_content);

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
        std::vector<Suggestion> suggestions;
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
