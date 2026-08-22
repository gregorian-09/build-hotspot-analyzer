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
#include "bha/project_index.hpp"
#include "bha/suggestions/suggester_policy.hpp"
#include "bha/suggestions/suggester_interface.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/string_utils.hpp"
#include "bha/utils/regex_utils.hpp"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bha::suggestions {
    namespace fs = std::filesystem;

    using bha::utils::trim_whitespace_copy;
    using bha::utils::lowercase_ascii;
    using bha::utils::strip_comments_and_strings;
    using bha::utils::is_identifier_char;
    using bha::utils::is_header_file_path;
    using bha::utils::is_source_file_path;
    using bha::utils::looks_like_macro_identifier;
    using bha::utils::contains_identifier_token;
    using bha::utils::find_outer_paren_span;

    /**
     * Context passed to suggesters containing all analysis data.
     */
    struct SuggestionContext {
        const BuildTrace& trace;
        const analyzers::AnalysisResult& analysis;
        const SuggesterOptions& options;
        fs::path project_root;
        std::shared_ptr<ProjectIndex> project_index;

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
              project_index(std::make_shared<ProjectIndex>(project_root, options_ref.compile_commands_path)),
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

            fs::path normalized_file = project_index
                ? project_index->resolve(file)
                : file.lexically_normal();
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
                    const fs::path normalized_target = project_index
                        ? project_index->resolve(target)
                        : target.lexically_normal();
                    return normalized_file == normalized_target;
                }
            );
        }
    };
#include "suggester_path_helpers.inc"
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
        static const std::regex forward_decl_regex(
            R"(^(class|struct|union)\s+[A-Za-z_][A-Za-z0-9_]*(?:\s+final)?\s*;\s*$)"
        );
        return std::regex_match(line, forward_decl_regex);
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
            const std::string escaped = bha::utils::regex_escape(spelling);

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

    [[nodiscard]] inline std::string include_directive_text(const IncludeDirective& include) {
        if (include.is_system) {
            return "#include <" + include.header_name + ">";
        }
        return "#include \"" + include.header_name + "\"";
    }

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

    [[nodiscard]] inline std::optional<fs::path> resolve_project_include_path(
        const std::string& include_name,
        const fs::path& including_file,
        const fs::path& project_root
    ) {
        const fs::path include_path(include_name);
        if (include_path.is_absolute()) {
            if (fs::exists(include_path)) {
                return include_path.lexically_normal();
            }
            return std::nullopt;
        }

        std::vector<fs::path> candidates;
        candidates.reserve(4);
        candidates.push_back((including_file.parent_path() / include_path).lexically_normal());
        if (!project_root.empty()) {
            candidates.push_back((project_root / include_path).lexically_normal());
            candidates.push_back((project_root / "include" / include_path).lexically_normal());
            candidates.push_back((project_root / "src" / include_path).lexically_normal());
        }

        for (const auto& candidate : candidates) {
            if (fs::exists(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    struct ExportedTypeSurface {
        std::vector<ExportedTypeSymbol> direct_symbols;
        std::vector<ExportedTypeSymbol> transitive_symbols;
    };

    [[nodiscard]] inline ExportedTypeSurface collect_exported_type_surface(
        const fs::path& header_path,
        const fs::path& project_root,
        const std::size_t max_depth = 8
    ) {
        ExportedTypeSurface surface;
        surface.direct_symbols = extract_exported_type_symbols(header_path);

        std::unordered_map<std::string, ExportedTypeSymbolKind> direct_kinds;
        for (const auto& symbol : surface.direct_symbols) {
            if (!symbol.name.empty()) {
                direct_kinds.emplace(symbol.name, symbol.kind);
            }
        }

        std::unordered_map<std::string, ExportedTypeSymbolKind> transitive_kinds;
        std::unordered_set<std::string> visited;

        const auto visit = [&](const auto& self, const fs::path& current, const std::size_t depth) -> void {
            if (depth >= max_depth) {
                return;
            }

            for (const auto& include_dir : find_include_directives(current)) {
                if (include_dir.is_system) {
                    continue;
                }

                const auto resolved = resolve_project_include_path(
                    include_dir.header_name,
                    current,
                    project_root
                );
                if (!resolved.has_value()) {
                    continue;
                }

                const fs::path normalized = resolved->lexically_normal();
                if (!visited.insert(normalized.generic_string()).second) {
                    continue;
                }

                const auto exported = extract_exported_type_symbols(normalized);
                for (const auto& symbol : exported) {
                    if (direct_kinds.contains(symbol.name)) {
                        continue;
                    }
                    merge_exported_type_symbols(
                        surface.transitive_symbols,
                        transitive_kinds,
                        {symbol}
                    );
                }

                self(self, normalized, depth + 1);
            }
        };

        visited.insert(header_path.lexically_normal().generic_string());
        visit(visit, header_path.lexically_normal(), 0);
        return surface;
    }

    [[nodiscard]] inline const std::vector<std::string>& default_protected_include_patterns() {
        static const std::vector<std::string> patterns = {
            R"((^|.*/)(port|platform|compat|abi|os|sys|config)/)",
            R"((^|.*/)(windows|winuser|winbase|windef|winnt|winsock|winsock2|pthread|unistd|malloc|intrin|io|direct)\.h$)"
        };
        return patterns;
    }

    [[nodiscard]] inline std::optional<std::string> direct_include_guard_umbrella_header(
        const fs::path& header_path
    ) {
        if (header_path.empty() || !fs::exists(header_path)) {
            return std::nullopt;
        }

        std::ifstream in(header_path);
        if (!in) {
            return std::nullopt;
        }

        static const std::regex direct_include_guard_regex(
            R"(Never use\s+[<"]([^>"]+)[>"]\s+directly;\s+include\s+[<"]([^>"]+)[>"]\s+instead\.?)",
            std::regex::ECMAScript | std::regex::icase
        );

        std::string line;
        std::size_t bytes_scanned = 0;
        while (std::getline(in, line) && bytes_scanned < 16 * 1024) {
            bytes_scanned += line.size();
            std::smatch match;
            if (std::regex_search(line, match, direct_include_guard_regex) && match.size() >= 3) {
                return match[2].str();
            }
        }

        return std::nullopt;
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

        if (resolved_header.has_value() &&
            direct_include_guard_umbrella_header(*resolved_header).has_value()) {
            return true;
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

    [[nodiscard]] inline bool is_blank_or_whitespace_line(std::string_view line) {
        return line.find_first_not_of(" \t\r\n") == std::string_view::npos;
    }

    [[nodiscard]] inline std::string make_separated_statement_insertion_text(
        const std::string& file_content,
        const std::size_t insert_line,
        const std::string& statement
    ) {
        std::istringstream input(file_content);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line)) {
            lines.push_back(line);
        }

        const bool has_prev_line = insert_line > 0 && insert_line - 1 < lines.size();
        const bool prev_is_blank = has_prev_line && is_blank_or_whitespace_line(lines[insert_line - 1]);

        std::string text;
        if (!prev_is_blank) {
            text.push_back('\n');
        }
        text += statement;
        text.push_back('\n');
        return text;
    }

    [[nodiscard]] inline TextEdit make_delete_include_edit(const fs::path& file, std::size_t line) {
        TextEdit edit = make_delete_line_edit(file, line);

        std::ifstream in(file);
        if (!in) {
            return edit;
        }

        std::vector<std::string> lines;
        std::string current_line;
        while (std::getline(in, current_line)) {
            lines.push_back(current_line);
        }

        if (line >= lines.size()) {
            return edit;
        }

        const auto directives = find_include_directives(file);
        const auto target_it = std::find_if(
            directives.begin(),
            directives.end(),
            [&](const IncludeDirective& directive) {
                return directive.line == line;
            }
        );
        if (target_it == directives.end()) {
            return edit;
        }

        const bool has_following_include = std::any_of(
            directives.begin(),
            directives.end(),
            [&](const IncludeDirective& directive) {
                return directive.line > line;
            }
        );
        const bool next_is_blank = line + 1 < lines.size() && is_blank_or_whitespace_line(lines[line + 1]);
        if (!has_following_include && next_is_blank) {
            edit.end_line = line + 2;
        }

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

#include "suggester_edit_helpers.inc"

}  // namespace bha::suggestions

#endif //BHA_SUGGESTER_HPP
