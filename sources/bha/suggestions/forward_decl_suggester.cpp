//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/forward_decl_suggester.hpp"
#include "bha/suggestions/scope_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bha/heuristics/config.hpp"

namespace bha::suggestions
{
    namespace {

        struct ForwardDeclType {
            std::string name;
            std::vector<std::string> namespaces;
            std::vector<ScopeFrame> scopes;
            std::vector<IncludeDirective> support_includes;
        };

        struct UsageAnalysis {
            bool eligible = false;
            std::size_t pointer_or_reference_mentions = 0;
        };

        using IncludeIndex = std::unordered_map<std::string, std::vector<fs::path>>;

        bool should_skip_index_directory(const fs::path& dir);

        std::string strip_line_comments(const std::string& line) {
            bool in_single_quote = false;
            bool in_double_quote = false;
            for (std::size_t i = 0; i < line.size(); ++i) {
                const char ch = line[i];
                if (ch == '\\') {
                    ++i;
                    continue;
                }
                if (!in_double_quote && ch == '\'') {
                    in_single_quote = !in_single_quote;
                    continue;
                }
                if (!in_single_quote && ch == '"') {
                    in_double_quote = !in_double_quote;
                    continue;
                }
                if (!in_single_quote && !in_double_quote && ch == '/' &&
                    i + 1 < line.size() && line[i + 1] == '/') {
                    return line.substr(0, i);
                }
            }
            return line;
        }

        bool is_macro_like_identifier(const std::string& token) {
            bool saw_alpha = false;
            for (const char ch : token) {
                if (std::isalpha(static_cast<unsigned char>(ch))) {
                    saw_alpha = true;
                    if (!std::isupper(static_cast<unsigned char>(ch)) && ch != '_') {
                        return false;
                    }
                }
            }
            return saw_alpha;
        }

        std::string strip_attribute_sequences(std::string text) {
            const auto erase_balanced = [&](const std::string& open, const std::string& close) {
                std::size_t pos = 0;
                while ((pos = text.find(open, pos)) != std::string::npos) {
                    const std::size_t end = text.find(close, pos + open.size());
                    if (end == std::string::npos) {
                        text.erase(pos);
                        break;
                    }
                    text.erase(pos, end + close.size() - pos);
                }
            };

            erase_balanced("[[", "]]");

            static const std::regex attribute_regex(R"(\b(?:__attribute__|alignas)\s*\([^)]*\))");
            text = std::regex_replace(text, attribute_regex, " ");
            return text;
        }

        std::optional<std::string> extract_declared_type_name(const std::string& declaration_tail) {
            const std::string sanitized = strip_attribute_sequences(declaration_tail);
            static const std::regex identifier_regex(R"([A-Za-z_][A-Za-z0-9_]*)");
            std::vector<std::string> tokens;
            for (auto begin = std::sregex_iterator(sanitized.begin(), sanitized.end(), identifier_regex),
                      end = std::sregex_iterator();
                 begin != end;
                 ++begin) {
                tokens.push_back((*begin).str());
            }

            auto is_non_name_token = [](const std::string& token) {
                static const std::unordered_set<std::string> blocked{
                    "class",
                    "struct",
                    "final",
                    "override",
                    "alignas",
                    "__attribute__",
                    "__declspec",
                    "declspec",
                    "nodiscard",
                    "maybe_unused"
                };
                return blocked.contains(token);
            };

            tokens.erase(
                std::remove_if(tokens.begin(), tokens.end(), is_non_name_token),
                tokens.end()
            );

            while (tokens.size() > 1 && is_macro_like_identifier(tokens.front())) {
                tokens.erase(tokens.begin());
            }
            while (tokens.size() > 1 && is_macro_like_identifier(tokens.back())) {
                tokens.pop_back();
            }

            if (tokens.empty()) {
                return std::nullopt;
            }
            return tokens.front();
        }

        std::string sanitize_source_for_usage(std::string content) {
            std::string output;
            output.reserve(content.size());
            bool in_single_quote = false;
            bool in_double_quote = false;
            bool in_line_comment = false;
            bool in_block_comment = false;

            for (std::size_t i = 0; i < content.size(); ++i) {
                const char ch = content[i];
                const char next = i + 1 < content.size() ? content[i + 1] : '\0';

                if (in_line_comment) {
                    if (ch == '\n') {
                        in_line_comment = false;
                        output.push_back('\n');
                    } else {
                        output.push_back(' ');
                    }
                    continue;
                }
                if (in_block_comment) {
                    if (ch == '*' && next == '/') {
                        output.push_back(' ');
                        output.push_back(' ');
                        ++i;
                        in_block_comment = false;
                    } else if (ch == '\n') {
                        output.push_back('\n');
                    } else {
                        output.push_back(' ');
                    }
                    continue;
                }
                if (in_single_quote) {
                    output.push_back(ch == '\n' ? '\n' : ' ');
                    if (ch == '\\' && next != '\0') {
                        output.push_back(next == '\n' ? '\n' : ' ');
                        ++i;
                        continue;
                    }
                    if (ch == '\'') {
                        in_single_quote = false;
                    }
                    continue;
                }
                if (in_double_quote) {
                    output.push_back(ch == '\n' ? '\n' : ' ');
                    if (ch == '\\' && next != '\0') {
                        output.push_back(next == '\n' ? '\n' : ' ');
                        ++i;
                        continue;
                    }
                    if (ch == '"') {
                        in_double_quote = false;
                    }
                    continue;
                }
                if (ch == '/' && next == '/') {
                    output.push_back(' ');
                    output.push_back(' ');
                    ++i;
                    in_line_comment = true;
                    continue;
                }
                if (ch == '/' && next == '*') {
                    output.push_back(' ');
                    output.push_back(' ');
                    ++i;
                    in_block_comment = true;
                    continue;
                }
                if (ch == '\'') {
                    output.push_back(' ');
                    in_single_quote = true;
                    continue;
                }
                if (ch == '"') {
                    output.push_back(' ');
                    in_double_quote = true;
                    continue;
                }
                output.push_back(ch);
            }

            return output;
        }

        std::string escape_regex(const std::string& text) {
            std::string escaped;
            escaped.reserve(text.size() * 2);
            for (const char ch : text) {
                switch (ch) {
                    case '\\':
                    case '.':
                    case '^':
                    case '$':
                    case '|':
                    case '(':
                    case ')':
                    case '[':
                    case ']':
                    case '{':
                    case '}':
                    case '*':
                    case '+':
                    case '?':
                        escaped.push_back('\\');
                        escaped.push_back(ch);
                        break;
                    default:
                        escaped.push_back(ch);
                        break;
                }
            }
            return escaped;
        }

        bool is_header_file(const fs::path& path) {
            static constexpr std::array<std::string_view, 4> kHeaderExts = {
                ".h", ".hpp", ".hxx", ".H"
            };
            return path_has_extension(path, kHeaderExts);
        }

        std::string join_namespace(const std::vector<std::string>& parts) {
            std::string joined;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    joined += "::";
                }
                joined += parts[i];
            }
            return joined;
        }

        std::string qualified_type_name(const ForwardDeclType& type) {
            if (type.namespaces.empty()) {
                return type.name;
            }
            return join_namespace(type.namespaces) + "::" + type.name;
        }

        bool has_macro_wrappers(const ForwardDeclType& type) {
            return std::ranges::any_of(type.scopes, [](const ScopeFrame& scope) {
                return scope.kind == ScopeFrameKind::MacroWrapper;
            });
        }

        std::string include_directive_text(const IncludeDirective& include) {
            if (include.is_system) {
                return "#include <" + include.header_name + ">";
            }
            return "#include \"" + include.header_name + "\"";
        }

        std::vector<IncludeDirective> missing_support_includes(
            const ForwardDeclType& type,
            const fs::path& includer_path
        ) {
            std::vector<IncludeDirective> filtered;
            filtered.reserve(type.support_includes.size());
            for (const auto& include : type.support_includes) {
                if (!find_include_for_header(includer_path, include.header_name).has_value()) {
                    filtered.push_back(include);
                }
            }
            return filtered;
        }

        std::string forward_declaration_text(
            const ForwardDeclType& type,
            const std::vector<IncludeDirective>& support_includes
        ) {
            std::ostringstream out;
            for (const auto& include : support_includes) {
                out << include_directive_text(include) << "\n";
            }
            if (!support_includes.empty()) {
                out << "\n";
            }

            if (type.scopes.empty()) {
                out << "class " << type.name << ";";
                return trim_whitespace_copy(out.str());
            }

            for (const auto& scope : type.scopes) {
                if (scope.kind == ScopeFrameKind::Namespace) {
                    out << "namespace " << scope.name << " {\n";
                } else {
                    out << scope.macro.open_text << "\n";
                }
            }
            out << "class " << type.name << ";\n";
            for (std::size_t i = type.scopes.size(); i > 0; --i) {
                const auto& scope = type.scopes[i - 1];
                if (scope.kind == ScopeFrameKind::Namespace) {
                    out << "}  // namespace " << scope.name << "\n";
                } else {
                    out << scope.macro.close_text << "\n";
                }
            }
            return trim_whitespace_copy(out.str());
        }

        std::vector<ForwardDeclType> parse_forward_declarable_types_from_header(const fs::path& header_path) {
            std::ifstream in(header_path);
            if (!in) {
                return {};
            }

            std::vector<ScopeFrame> scope_stack;
            std::vector<ForwardDeclType> types;
            std::unordered_set<std::string> seen_types;
            std::size_t brace_depth = 0;
            bool pending_template = false;

            std::string raw_line;
            while (std::getline(in, raw_line)) {
                const std::string no_comment = strip_line_comments(raw_line);
                const std::string line = trim_whitespace_copy(no_comment);

                if (line.starts_with("template<") || line.starts_with("template <")) {
                    pending_template = true;
                }

                const std::regex namespace_regex(R"(\bnamespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{)");
                for (auto begin = std::sregex_iterator(no_comment.begin(), no_comment.end(), namespace_regex),
                          end = std::sregex_iterator();
                     begin != end;
                     ++begin) {
                    const auto parts = split_namespace_path((*begin)[1].str());
                    for (const auto& part : parts) {
                        ScopeFrame frame;
                        frame.kind = ScopeFrameKind::Namespace;
                        frame.name = part;
                        frame.open_depth = brace_depth + 1;
                        scope_stack.push_back(std::move(frame));
                    }
                }

                if (const auto macro_scope = parse_scope_macro_open(line); macro_scope.has_value()) {
                    ScopeFrame frame;
                    frame.kind = ScopeFrameKind::MacroWrapper;
                    frame.macro = *macro_scope;
                    scope_stack.push_back(std::move(frame));
                } else if (const auto close_macro = parse_scope_macro_close(line); close_macro.has_value()) {
                    for (auto it = scope_stack.rbegin(); it != scope_stack.rend(); ++it) {
                        if (it->kind != ScopeFrameKind::MacroWrapper) {
                            continue;
                        }
                        if (it->macro.close_name != *close_macro) {
                            continue;
                        }
                        it->macro.close_text = line;
                        scope_stack.erase(std::next(it).base());
                        break;
                    }
                }

                if (!line.empty()) {
                    std::smatch class_match;
                    const std::regex class_regex(R"(^\s*(class|struct)\b(.*)$)");
                    if (std::regex_search(line, class_match, class_regex)) {
                        if (!pending_template) {
                            const auto type_name = extract_declared_type_name(class_match[2].str());
                            if (!type_name.has_value()) {
                                pending_template = false;
                                continue;
                            }
                            ForwardDeclType type;
                            type.name = *type_name;
                            for (const auto& scope : scope_stack) {
                                type.scopes.push_back(scope);
                                if (scope.kind == ScopeFrameKind::Namespace) {
                                    type.namespaces.push_back(scope.name);
                                }
                            }
                            const std::string key = qualified_type_name(type);
                            if (seen_types.insert(key).second) {
                                types.push_back(std::move(type));
                            }
                        }
                        pending_template = false;
                    }
                }

                const auto opens = static_cast<std::size_t>(std::count(no_comment.begin(), no_comment.end(), '{'));
                const auto closes = static_cast<std::size_t>(std::count(no_comment.begin(), no_comment.end(), '}'));
                brace_depth += opens;
                if (closes >= brace_depth) {
                    brace_depth = 0;
                } else {
                    brace_depth -= closes;
                }

                while (!scope_stack.empty()) {
                    const auto& scope = scope_stack.back();
                    if (scope.kind != ScopeFrameKind::Namespace) {
                        break;
                    }
                    if (brace_depth >= scope.open_depth) {
                        break;
                    }
                    scope_stack.pop_back();
                }

                if (!line.empty() && line.back() == ';' && !line.starts_with("template")) {
                    pending_template = false;
                }
            }

            return types;
        }

        std::optional<fs::path> resolve_include_target(
            const fs::path& header_path,
            const fs::path& project_root,
            const IncludeDirective& include
        ) {
            const fs::path include_path(include.header_name);
            const fs::path normalized_header = resolve_source_path(header_path).lexically_normal();
            const fs::path repo_root = !project_root.empty()
                ? project_root.lexically_normal()
                : find_repository_root(normalized_header);

            std::vector<fs::path> candidates;
            if (!normalized_header.empty()) {
                candidates.push_back((normalized_header.parent_path() / include_path).lexically_normal());
            }
            if (!repo_root.empty()) {
                candidates.push_back((repo_root / include_path).lexically_normal());
                if (auto found = find_file_in_repo(repo_root, include_path.filename()); found.has_value()) {
                    candidates.push_back(found->lexically_normal());
                }
            }

            for (const auto& candidate : candidates) {
                if (!candidate.empty() && fs::exists(candidate)) {
                    return candidate;
                }
            }
            return std::nullopt;
        }

        bool file_defines_macro(const fs::path& file, const std::string& macro_name) {
            std::ifstream in(file);
            if (!in) {
                return false;
            }

            const std::regex define_regex(
                "^\\s*#\\s*define\\s+" + escape_regex(macro_name) + R"((?:\b|\s*\())"
            );
            std::string line;
            while (std::getline(in, line)) {
                if (std::regex_search(line, define_regex)) {
                    return true;
                }
            }
            return false;
        }

        std::optional<std::vector<IncludeDirective>> resolve_support_includes(
            const fs::path& header_path,
            const fs::path& project_root,
            const std::vector<ScopeFrame>& scopes
        ) {
            std::unordered_set<std::string> unresolved_macros;
            for (const auto& scope : scopes) {
                if (scope.kind != ScopeFrameKind::MacroWrapper) {
                    continue;
                }
                unresolved_macros.insert(scope.macro.open_name);
                unresolved_macros.insert(scope.macro.close_name);
            }
            if (unresolved_macros.empty()) {
                return std::vector<IncludeDirective>{};
            }

            std::vector<IncludeDirective> includes;
            std::unordered_set<std::string> seen_headers;
            for (const auto& include : find_include_directives(header_path)) {
                const auto resolved = resolve_include_target(header_path, project_root, include);
                if (!resolved.has_value()) {
                    continue;
                }

                bool needed = false;
                std::vector<std::string> satisfied;
                for (const auto& macro_name : unresolved_macros) {
                    if (!file_defines_macro(*resolved, macro_name)) {
                        continue;
                    }
                    satisfied.push_back(macro_name);
                    needed = true;
                }

                if (!needed) {
                    continue;
                }

                if (seen_headers.insert(include.header_name).second) {
                    IncludeDirective support;
                    support.header_name = include.header_name;
                    support.is_system = include.is_system;
                    includes.push_back(std::move(support));
                }

                for (const auto& macro_name : satisfied) {
                    unresolved_macros.erase(macro_name);
                }
                if (unresolved_macros.empty()) {
                    break;
                }
            }

            if (!unresolved_macros.empty()) {
                return std::nullopt;
            }
            return includes;
        }

        bool is_reference_or_pointer_context(
            const std::string& text,
            const std::size_t match_begin,
            const std::size_t match_end
        ) {
            auto find_prev_non_space = [&text](std::size_t index) -> std::optional<std::size_t> {
                while (index > 0) {
                    --index;
                    if (!std::isspace(static_cast<unsigned char>(text[index]))) {
                        return index;
                    }
                }
                return std::nullopt;
            };
            auto find_next_non_space = [&text](std::size_t index) -> std::optional<std::size_t> {
                while (index < text.size()) {
                    if (!std::isspace(static_cast<unsigned char>(text[index]))) {
                        return index;
                    }
                    ++index;
                }
                return std::nullopt;
            };

            if (const auto right = find_next_non_space(match_end); right.has_value()) {
                if (text[*right] == '*' || text[*right] == '&') {
                    return true;
                }
                if (text.compare(*right, 5, "const") == 0) {
                    if (const auto after_const = find_next_non_space(*right + 5); after_const.has_value()) {
                        if (text[*after_const] == '*' || text[*after_const] == '&') {
                            return true;
                        }
                    }
                }
            }

            if (const auto left = find_prev_non_space(match_begin); left.has_value()) {
                if (text[*left] == '*' || text[*left] == '&') {
                    return true;
                }
            }

            return false;
        }

        bool line_looks_like_forward_declaration(
            const std::string& text,
            const std::size_t match_begin,
            const std::size_t match_end
        ) {
            const auto line_start = text.rfind('\n', match_begin);
            const auto line_end = text.find('\n', match_end);
            const std::size_t begin = line_start == std::string::npos ? 0 : line_start + 1;
            const std::size_t end = line_end == std::string::npos ? text.size() : line_end;
            const std::string line = trim_whitespace_copy(text.substr(begin, end - begin));
            return line.starts_with("class ") || line.starts_with("struct ");
        }

        UsageAnalysis analyze_includer_usage(
            const std::string& sanitized_text,
            const ForwardDeclType& type
        ) {
            UsageAnalysis result;
            const std::string qualified = qualified_type_name(type);
            const std::string escaped_qualified = escape_regex(qualified);
            const std::string escaped_unqualified = escape_regex(type.name);
            const std::vector<std::string> type_patterns = type.namespaces.empty()
                ? std::vector<std::string>{escaped_qualified}
                : std::vector<std::string>{escaped_qualified, escaped_unqualified};

            for (const auto& pattern : type_patterns) {
                const std::regex forbidden_constructs(
                    "\\b(?:sizeof|alignof|typeid|new|delete)\\s*(?:\\(|)\\s*" + pattern + "\\b|"
                    "\\b" + pattern + "\\s*::"
                );
                if (std::regex_search(sanitized_text, forbidden_constructs)) {
                    return result;
                }
                const std::regex inheritance_regex(
                    "\\b(?:class|struct)\\s+[A-Za-z_][A-Za-z0-9_]*\\s*:[^\\{;]*\\b" + pattern + "\\b"
                );
                if (std::regex_search(sanitized_text, inheritance_regex)) {
                    return result;
                }
            }

            std::vector<std::pair<std::size_t, std::size_t>> mentions;
            auto add_mention = [&mentions](const std::size_t start, const std::size_t end_pos) {
                const auto exists = std::ranges::any_of(
                    mentions,
                    [start, end_pos](const auto& span) { return span.first == start && span.second == end_pos; }
                );
                if (!exists) {
                    mentions.emplace_back(start, end_pos);
                }
            };

            const std::regex qualified_regex("\\b" + escaped_qualified + "\\b");
            for (auto begin = std::sregex_iterator(sanitized_text.begin(), sanitized_text.end(), qualified_regex),
                      end = std::sregex_iterator();
                 begin != end;
                 ++begin) {
                const std::size_t start = static_cast<std::size_t>((*begin).position());
                const std::size_t end_pos = start + static_cast<std::size_t>((*begin).length());
                add_mention(start, end_pos);
            }

            if (!type.namespaces.empty()) {
                const std::regex unqualified_regex("\\b" + escaped_unqualified + "\\b");
                for (auto begin = std::sregex_iterator(sanitized_text.begin(), sanitized_text.end(), unqualified_regex),
                          end = std::sregex_iterator();
                     begin != end;
                     ++begin) {
                    const std::size_t start = static_cast<std::size_t>((*begin).position());
                    if (start >= 2 &&
                        sanitized_text[start - 1] == ':' &&
                        sanitized_text[start - 2] == ':') {
                        continue;
                    }
                    const std::size_t end_pos = start + static_cast<std::size_t>((*begin).length());
                    add_mention(start, end_pos);
                }
            }

            std::ranges::sort(mentions, [](const auto& lhs, const auto& rhs) {
                if (lhs.first != rhs.first) {
                    return lhs.first < rhs.first;
                }
                return lhs.second < rhs.second;
            });

            for (const auto& [start, end_pos] : mentions) {
                if (line_looks_like_forward_declaration(sanitized_text, start, end_pos)) {
                    continue;
                }

                if (is_reference_or_pointer_context(sanitized_text, start, end_pos)) {
                    ++result.pointer_or_reference_mentions;
                    continue;
                }
                return result;
            }

            result.eligible = result.pointer_or_reference_mentions > 0;
            return result;
        }

        bool is_source_file(const fs::path& path) {
            static constexpr std::array<std::string_view, 5> kSourceExts = {
                ".cpp", ".cc", ".cxx", ".c++", ".C"
            };
            return path_has_extension(path, kSourceExts);
        }

        std::optional<fs::path> find_matching_source_file(const fs::path& header_path, const fs::path& project_root) {
            static constexpr std::array<std::string_view, 5> kSourceExts = {
                ".cpp", ".cc", ".cxx", ".c++", ".C"
            };
            for (const auto ext : kSourceExts) {
                fs::path candidate = header_path;
                candidate.replace_extension(ext);
                if (fs::exists(candidate)) {
                    return candidate;
                }
            }

            if (!project_root.empty() && fs::exists(project_root) && fs::is_directory(project_root)) {
                const fs::path normalized_root = project_root.lexically_normal();
                const fs::path normalized_header = header_path.lexically_normal();
                if (header_path.is_absolute()) {
                    const fs::path rel = normalized_header.lexically_relative(normalized_root);
                    if (!rel.empty() && rel != "." && rel != "..") {
                        for (const auto ext : kSourceExts) {
                            fs::path direct_candidate = normalized_root / rel;
                            direct_candidate.replace_extension(ext);
                            if (fs::exists(direct_candidate)) {
                                return direct_candidate;
                            }
                        }

                        auto rel_it = rel.begin();
                        if (rel_it != rel.end() && rel_it->string() == "include") {
                            fs::path rel_without_include;
                            ++rel_it;
                            for (; rel_it != rel.end(); ++rel_it) {
                                rel_without_include /= *rel_it;
                            }
                            for (const auto ext : kSourceExts) {
                                fs::path src_candidate = normalized_root / "src" / rel_without_include;
                                src_candidate.replace_extension(ext);
                                if (fs::exists(src_candidate)) {
                                    return src_candidate;
                                }
                            }
                        }
                    }
                }

                std::vector<fs::path> candidates;
                std::error_code ec;
                fs::recursive_directory_iterator it(
                    normalized_root,
                    fs::directory_options::skip_permission_denied,
                    ec
                );
                const fs::recursive_directory_iterator end;
                for (; it != end; it.increment(ec)) {
                    if (ec) {
                        continue;
                    }
                    const auto& entry = *it;
                    if (entry.is_directory()) {
                        if (should_skip_index_directory(entry.path())) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    const fs::path path = entry.path();
                    if (!is_source_file(path)) {
                        continue;
                    }
                    if (path.stem() != header_path.stem()) {
                        continue;
                    }
                    candidates.push_back(path);
                }

                if (!candidates.empty()) {
                    std::ranges::sort(candidates, [](const fs::path& lhs, const fs::path& rhs) {
                        const bool lhs_src = lhs.parent_path().filename() == "src";
                        const bool rhs_src = rhs.parent_path().filename() == "src";
                        if (lhs_src != rhs_src) {
                            return lhs_src > rhs_src;
                        }
                        return lhs.generic_string() < rhs.generic_string();
                    });
                    return candidates.front();
                }
            }
            return std::nullopt;
        }

        bool should_skip_index_directory(const fs::path& dir) {
            const std::string name = dir.filename().string();
            return name == ".git" ||
                   name == ".hg" ||
                   name == ".svn" ||
                   name == "build" ||
                   name == "cmake-build-debug" ||
                   name == "cmake-build-release" ||
                   name == ".bha_traces" ||
                   name == ".lsp-optimization-backup" ||
                   name == "traces" ||
                   name == "output";
        }

        IncludeIndex build_header_include_index(const fs::path& root) {
            IncludeIndex index;
            if (root.empty() || !fs::exists(root) || !fs::is_directory(root)) {
                return index;
            }

            std::error_code ec;
            fs::recursive_directory_iterator it(
                root,
                fs::directory_options::skip_permission_denied,
                ec
            );
            const fs::recursive_directory_iterator end;
            for (; it != end; it.increment(ec)) {
                if (ec) {
                    continue;
                }
                const auto& entry = *it;
                if (entry.is_directory()) {
                    if (should_skip_index_directory(entry.path())) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                const fs::path& file = entry.path();
                if (!is_header_file(file)) {
                    continue;
                }

                for (const auto& directive : find_include_directives(file)) {
                    if (directive.header_name.empty()) {
                        continue;
                    }
                    index[directive.header_name].push_back(file);
                    const std::string include_filename = fs::path(directive.header_name).filename().string();
                    if (!include_filename.empty() && include_filename != directive.header_name) {
                        index[include_filename].push_back(file);
                    }
                }
            }
            return index;
        }

        Priority calculate_priority(
            const Duration parse_time,
            const std::size_t includer_count,
            const heuristics::ForwardDeclConfig& config
        ) {
            const auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(parse_time);

            if (parse_ms > std::chrono::milliseconds(500) && includer_count >= 10) {
                return Priority::Critical;
            }
            if (parse_ms > std::chrono::milliseconds(200) && includer_count >= 5) {
                return Priority::High;
            }
            if (parse_ms > config.min_parse_time) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        std::string generate_before_code(
            const fs::path& header_path,
            const fs::path& includer_path,
            const std::string& type_name
        ) {
            std::ostringstream oss;
            oss << "// " << includer_path.filename().string() << "\n";
            oss << "#pragma once\n\n";
            oss << "#include \"" << header_path.filename().string() << "\"\n";
            oss << "class Consumer {\n";
            oss << "    " << type_name << "* ptr;\n";
            oss << "    void process(" << type_name << "& ref);\n";
            oss << "};";
            return oss.str();
        }

        std::string generate_after_code(
            const fs::path& header_path,
            const fs::path& includer_path,
            const ForwardDeclType& type
        ) {
            const std::string type_name = qualified_type_name(type);
            const std::string declaration = forward_declaration_text(type, type.support_includes);

            std::ostringstream oss;
            oss << "// " << includer_path.filename().string() << " (header)\n";
            oss << "#pragma once\n\n";
            oss << declaration << "\n\n";
            oss << "class Consumer {\n";
            oss << "    " << type_name << "* ptr;\n";
            oss << "    void process(" << type_name << "& ref);\n";
            oss << "};\n\n";
            oss << "// " << includer_path.stem().string() << ".cpp (implementation)\n";
            oss << "#include \"" << includer_path.filename().string() << "\"\n";
            oss << "#include \"" << header_path.filename().string() << "\"\n";
            return oss.str();
        }

    }  // namespace

    Result<SuggestionResult, Error> ForwardDeclSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& deps = context.analysis.dependencies;
        const auto& config = context.options.heuristics.forward_decl;

        fs::path include_scan_root = context.project_root;
        if (include_scan_root.empty() && !deps.headers.empty()) {
            include_scan_root = find_repository_root(deps.headers.front().path);
        }
        std::optional<IncludeIndex> include_index;
        auto ensure_include_index = [&]() -> const IncludeIndex& {
            if (!include_index.has_value()) {
                include_index = build_header_include_index(include_scan_root);
            }
            return *include_index;
        };

        std::unordered_set<std::string> processed;
        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& header : deps.headers) {
            if (context.is_cancelled()) {
                break;
            }
            ++analyzed;

            if (!is_header_file(header.path)) {
                ++skipped;
                continue;
            }
            if (header.total_parse_time < config.min_parse_time) {
                ++skipped;
                continue;
            }
            if (header.included_by.empty()) {
                ++skipped;
                continue;
            }
            if (processed.contains(header.path.string())) {
                ++skipped;
                continue;
            }
            processed.insert(header.path.string());

            auto forward_types = parse_forward_declarable_types_from_header(header.path);
            if (forward_types.size() != 1) {
                ++skipped;
                continue;
            }
            ForwardDeclType target_type = std::move(forward_types.front());
            if (!target_type.namespaces.empty() && target_type.namespaces.front() == "std") {
                ++skipped;
                continue;
            }
            if (has_macro_wrappers(target_type)) {
                const auto support_includes = resolve_support_includes(
                    header.path,
                    include_scan_root,
                    target_type.scopes
                );
                if (!support_includes.has_value()) {
                    ++skipped;
                    continue;
                }
                target_type.support_includes = *support_includes;
            }

            std::vector<fs::path> candidate_includers;
            std::unordered_set<std::string> seen_includers;
            auto add_includer_candidate = [&](const fs::path& candidate) {
                if (!is_header_file(candidate)) {
                    return;
                }
                if (candidate.lexically_normal() == header.path.lexically_normal()) {
                    return;
                }
                const std::string key = candidate.lexically_normal().generic_string();
                if (!seen_includers.insert(key).second) {
                    return;
                }
                candidate_includers.push_back(candidate);
            };

            for (const auto& includer : header.included_by) {
                add_includer_candidate(includer);
            }

            if (!include_scan_root.empty() && fs::exists(include_scan_root)) {
                const auto& index = ensure_include_index();
                const std::string header_filename = header.path.filename().string();
                if (const auto it = index.find(header_filename); it != index.end()) {
                    for (const auto& includer : it->second) {
                        add_includer_candidate(includer);
                    }
                }
                if (!header.path.empty()) {
                    fs::path relative_header = header.path;
                    if (header.path.is_absolute()) {
                        relative_header = header.path.lexically_relative(include_scan_root);
                    }
                    const std::string relative_key = relative_header.generic_string();
                    if (!relative_key.empty() && relative_key != "." && relative_key != "..") {
                        if (const auto it = index.find(relative_key); it != index.end()) {
                            for (const auto& includer : it->second) {
                                add_includer_candidate(includer);
                            }
                        }
                    }
                }
            }

            for (const auto& includer_path : candidate_includers) {
                if (context.is_cancelled()) {
                    break;
                }
                if (!context.should_analyze(includer_path)) {
                    continue;
                }
                if (!is_header_file(includer_path)) {
                    continue;
                }

                std::ifstream header_in(includer_path);
                if (!header_in) {
                    continue;
                }
                const std::string includer_content(
                    (std::istreambuf_iterator<char>(header_in)),
                    std::istreambuf_iterator<char>()
                );
                const std::string sanitized = sanitize_source_for_usage(includer_content);

                const std::string header_filename = header.path.filename().string();
                auto include_dir = find_include_for_header(includer_path, header_filename);
                if (!include_dir.has_value()) {
                    continue;
                }

                const auto usage = analyze_includer_usage(sanitized, target_type);
                if (!usage.eligible || usage.pointer_or_reference_mentions < config.min_usage_sites) {
                    continue;
                }

                Suggestion suggestion;
                suggestion.id = generate_suggestion_id(
                    "fwd",
                    header.path,
                    includer_path.filename().string() + "-" + qualified_type_name(target_type)
                );
                suggestion.type = SuggestionType::ForwardDeclaration;
                suggestion.priority = calculate_priority(
                    header.total_parse_time,
                    header.inclusion_count,
                    config
                );
                suggestion.confidence = 0.85;

                std::ostringstream title;
                title << "Use forward declaration for '" << header.path.filename().string()
                      << "' in " << includer_path.filename().string();
                suggestion.title = title.str();

                const auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    header.total_parse_time).count();

                std::ostringstream desc;
                desc << "Header '" << header.path.filename().string() << "' takes " << parse_ms
                     << "ms to parse and is included in " << includer_path.filename().string()
                     << ". Detected " << usage.pointer_or_reference_mentions
                     << " pointer/reference-only use sites for " << qualified_type_name(target_type)
                     << " in the header, so this include can be narrowed to a forward declaration.";
                suggestion.description = desc.str();

                suggestion.rationale =
                    "Forward declarations reduce transitive include dependencies when a type is only "
                    "used by pointer/reference in a header. This suggestion is generated only after "
                    "excluding common unsafe contexts (by-value usage, inheritance, sizeof/new/delete, "
                    "and qualified member access).";

                const Duration savings_per_file = header.total_parse_time /
                    std::max<std::size_t>(1, header.inclusion_count);
                suggestion.estimated_savings = savings_per_file;
                if (context.trace.total_time.count() > 0) {
                    suggestion.estimated_savings_percent =
                        100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                        static_cast<double>(context.trace.total_time.count());
                }

                suggestion.target_file.path = includer_path;
                suggestion.target_file.action = FileAction::Modify;
                suggestion.target_file.note = "Replace #include with a safe forward declaration";
                suggestion.target_file.line_start = include_dir->line + 1;
                suggestion.target_file.line_end = include_dir->line + 1;
                suggestion.target_file.col_start = include_dir->col_start + 1;
                suggestion.target_file.col_end = include_dir->col_end + 1;

                suggestion.before_code.file = includer_path;
                suggestion.before_code.code = generate_before_code(
                    header.path,
                    includer_path,
                    qualified_type_name(target_type)
                );
                suggestion.after_code.file = includer_path;
                suggestion.after_code.code = generate_after_code(
                    header.path,
                    includer_path,
                    target_type
                );

                suggestion.implementation_steps = {
                    "1. Replace the include with the generated forward declaration",
                    "2. Keep the full include in the implementation file",
                    "3. Rebuild and run tests to validate no hidden complete-type dependency exists"
                };
                suggestion.caveats = {
                    "Forward declaring symbols from namespace std is undefined behavior; this suggester skips them",
                    "By-value type usage, inheritance, and sizeof/new/delete contexts require full definitions",
                    "If the corresponding source file is missing, add the include manually where full type use occurs"
                };
                suggestion.documentation_link =
                    "https://google.github.io/styleguide/cppguide.html#Include_What_You_Use";
                suggestion.verification =
                    "Compile the project and run tests after applying edits.";
                suggestion.impact.total_files_affected = 1;
                suggestion.impact.cumulative_savings = savings_per_file;
                suggestion.is_safe = true;

                suggestion.edits.push_back(make_replace_line_edit(
                    includer_path,
                    include_dir->line,
                    forward_declaration_text(
                        target_type,
                        missing_support_includes(target_type, includer_path)
                    )
                ));

                if (auto source_file = find_matching_source_file(includer_path, include_scan_root)) {
                    if (!find_include_for_header(*source_file, header_filename).has_value()) {
                        const auto insertion = make_preferred_include_insertion_edit(
                            *source_file,
                            "#include \"" + header_filename + "\""
                        );
                        suggestion.edits.push_back(insertion.edit);

                        FileTarget source_target;
                        source_target.path = *source_file;
                        source_target.action = FileAction::AddInclude;
                        source_target.line_start = insertion.inserted_line_one_based;
                        source_target.line_end = insertion.inserted_line_one_based;
                        source_target.note = "Add full include for complete type usage";
                        suggestion.secondary_files.push_back(source_target);
                    }
                }

                result.suggestions.push_back(std::move(suggestion));
            }
        }

        result.items_analyzed = analyzed;
        result.items_skipped = skipped;

        std::ranges::sort(result.suggestions,
                          [](const Suggestion& a, const Suggestion& b) {
                              return a.estimated_savings > b.estimated_savings;
                          });

        const auto end_time = std::chrono::steady_clock::now();
        result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);

        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_forward_decl_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<ForwardDeclSuggester>()
        );
    }
}  // namespace bha::suggestions
