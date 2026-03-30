//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/header_split_suggester.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {

        enum class ScopeFrameKind {
            Namespace,
            MacroWrapper
        };

        struct MacroWrapperScope {
            std::string open_name;
            std::string open_text;
            std::string close_name;
            std::string close_text;
        };

        struct ScopeFrame {
            ScopeFrameKind kind = ScopeFrameKind::Namespace;
            std::string name;
            std::size_t open_depth = 0;
            MacroWrapperScope macro;
        };

        std::string trim_copy(std::string value) {
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

        /**
         * Checks if a path is a C++ header file.
         */
        bool is_header(const fs::path& path) {
            const std::string ext = path.extension().string();
            return ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".H" ||
                   ext == ".hh" || ext == ".h++";
        }

        bool is_likely_system_header(const fs::path& path) {
            const std::string value = path.generic_string();
            return value.starts_with("/usr/include") ||
                   value.find("/include/c++/") != std::string::npos ||
                   value.find("/lib/clang/") != std::string::npos ||
                   value.rfind("<built-in>", 0) == 0;
        }

        /**
         * Header split pattern types.
         *
         * Different split patterns are appropriate for different header types:
         * - ForwardDecl: Create a _fwd.h with forward declarations
         * - TypesAndFwd: Split into types and forward declarations
         * - FunctionalGroups: Split by logical functionality groups
         * - PublicPrivate: Split into public API and internal details
         */
        enum class SplitPattern {
            ForwardDecl,
            TypesAndFwd,
            FunctionalGroups,
            PublicPrivate
        };

        /**
         * Analyzes a header to determine the best split pattern.
         *
         * Based on:
         * - Filename patterns (utils, types, core suggests different splits)
         * - Number of includers (high fanout suggests forward decl pattern)
         * - Parse time distribution (if available)
         */
        SplitPattern determine_split_pattern(
            const fs::path& header_path,
            const std::size_t includer_count
        ) {
            const std::string filename = header_path.filename().string();
            std::string lower_filename;
            lower_filename.reserve(filename.size());
            for (const char c : filename) {
                lower_filename += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }

            // Type-focused headers benefit from types/fwd split
            if (lower_filename.find("type") != std::string::npos ||
                lower_filename.find("struct") != std::string::npos ||
                lower_filename.find("enum") != std::string::npos) {
                return SplitPattern::TypesAndFwd;
                }

            // Utility headers often benefit from functional group splits
            if (lower_filename.find("util") != std::string::npos ||
                lower_filename.find("helper") != std::string::npos ||
                lower_filename.find("common") != std::string::npos) {
                return SplitPattern::FunctionalGroups;
                }

            // High-fanout headers (>20 includers) benefit most from forward decls
            if (includer_count > 20) {
                return SplitPattern::ForwardDecl;
            }

            // Core/main headers often benefit from public/private split
            if (lower_filename.find("core") != std::string::npos ||
                lower_filename.find("main") != std::string::npos ||
                lower_filename.find("api") != std::string::npos) {
                return SplitPattern::PublicPrivate;
                }

            return SplitPattern::ForwardDecl;
        }

        /**
         * Calculates priority for header splitting.
         *
         * Thresholds based on build impact:
         * - Critical: Headers that significantly impact overall build time
         * - High: Headers with high parse time and many includers
         * - Medium: Moderate impact headers
         * - Low: Minor improvements possible
         */
        Priority calculate_priority(const Duration parse_time, const std::size_t includer_count) {
            const auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                parse_time).count();

            // Total impact = parse_time * includer_count (amortized across build)
            const double total_impact_ms = static_cast<double>(parse_ms) *
                                     static_cast<double>(includer_count);

            if (parse_ms > 1000 && includer_count >= 50) {
                return Priority::Critical;
            }
            if (parse_ms > 500 && includer_count >= 20) {
                return Priority::High;
            }
            if ((parse_ms > 200 && includer_count >= 10) || total_impact_ms > 5000) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        /**
         * Calculates confidence that splitting will help.
         *
         * Based on:
         * - Parse time relative to typical headers
         * - Number of includers (more = more likely to benefit)
         * - Header size indicators
         *
         * Returns 0.0 to 1.0.
         */
        double calculate_confidence(
            const Duration parse_time,
            const std::size_t includer_count,
            const std::size_t inclusion_count  // Total inclusions across all TUs
        ) {
            const auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                parse_time).count();

            // Base confidence from parse time (headers over 200ms benefit)
            double time_confidence = 0.0;
            if (parse_ms > 1000) {
                time_confidence = 0.9;
            } else if (parse_ms > 500) {
                time_confidence = 0.75;
            } else if (parse_ms > 200) {
                time_confidence = 0.6;
            } else {
                time_confidence = 0.4;
            }

            // Adjust for includer count (more files = more benefit from split)
            const double includer_confidence = std::min(1.0,
                std::log(static_cast<double>(includer_count) + 1) / std::log(50.0));

            // Repeated inclusions per file indicate potential for subsets
            double repetition_factor = 1.0;
            if (includer_count > 0 && inclusion_count > includer_count) {
                const double avg_inclusions = static_cast<double>(inclusion_count) /
                                        static_cast<double>(includer_count);
                if (avg_inclusions > 2.0) {
                    repetition_factor = 1.1;  // Slight boost for highly reused headers
                }
            }

            const double confidence = (time_confidence * 0.6 + includer_confidence * 0.4)
                                * repetition_factor;

            return std::max(0.3, std::min(0.95, confidence));
        }

        /**
         * Estimates savings from splitting a header.
         *
         * Assumes:
         * - Forward decl headers are ~90% smaller than full headers
         * - Types headers are ~60% smaller
         * - On average, 30-50% of includers only need forward decls
         *
         * Savings = parse_time * includer_fraction * reduction_factor
         */
        Duration estimate_savings(
            const Duration parse_time,
            const std::size_t includer_count,
            const SplitPattern pattern
        ) {
            // Base forward-declaration-only ratio: 30% of includers typically only need fwd decls
            constexpr double fwd_only_ratio = 0.30;
            double reduction_factor = fwd_only_ratio;
            switch (pattern) {
                case SplitPattern::ForwardDecl:
                    // 30-40% of includers might only need forward decls
                    reduction_factor = fwd_only_ratio;
                    break;
                case SplitPattern::TypesAndFwd:
                    // Types+fwd gives more options
                    reduction_factor = 0.25;
                    break;
                case SplitPattern::FunctionalGroups:
                    // Groups allow targeted includes
                    reduction_factor = 0.20;
                    break;
                case SplitPattern::PublicPrivate:
                    // Public/private split helps internal vs external
                    reduction_factor = 0.15;
                    break;
            }

            const auto parse_ns = parse_time.count();
            const double includer_factor = std::log(static_cast<double>(includer_count) + 1); // Log scale for includers (diminishing returns)

            const auto savings_ns = static_cast<Duration::rep>(
                static_cast<double>(parse_ns) * reduction_factor * includer_factor
            );

            return Duration(savings_ns);
        }

        /**
         * Generates split header name based on pattern.
         */
        std::string suggest_split_name(const fs::path& header, const std::string& suffix) {
            const std::string stem = header.stem().string();
            const std::string ext = header.extension().string();
            return stem + "_" + suffix + ext;
        }

        /**
         * Generates implementation steps based on split pattern.
         */
        std::vector<std::string> generate_implementation_steps(
            const fs::path& header_path,
            const SplitPattern pattern
        ) {
            const std::string filename = header_path.filename().string();
            const std::string fwd_header = suggest_split_name(header_path, "fwd");
            const std::string types_header = suggest_split_name(header_path, "types");

            std::vector<std::string> steps;

            switch (pattern) {
            case SplitPattern::ForwardDecl:
                steps = {
                "Identify classes and structs that can be forward-declared",
                "Create " + fwd_header + " with forward declarations",
                "Update " + filename + " to include " + fwd_header,
                "Audit includers: replace #include with forward decl where possible",
                "Run include-what-you-use (IWYU) to validate minimal includes",
                "Verify compilation and run tests"
            };
                break;

            case SplitPattern::TypesAndFwd:
                steps = {
                "Separate type definitions from function declarations",
                "Create " + fwd_header + " with forward declarations",
                "Create " + types_header + " with type definitions",
                "Update " + filename + " to include both split headers",
                "Update includers to use minimal required header",
                "Verify compilation and run tests"
            };
                break;

            case SplitPattern::FunctionalGroups:
                steps = {
                "Identify logical groups of related functions/classes",
                "Create separate headers for each functional group",
                "Move declarations to appropriate group headers",
                "Update " + filename + " to include all group headers",
                "Update includers to use specific group headers",
                "Consider deprecating the umbrella header",
                "Verify compilation and run tests"
            };
                break;

            case SplitPattern::PublicPrivate:
                steps = {
                "Identify public API vs internal implementation details",
                "Create " + suggest_split_name(header_path, "internal") + " for internals",
                "Keep " + filename + " as the public API header",
                "Move internal details to the internal header",
                "Update internal code to use the internal header",
                "Document that " + filename + " is the public interface",
                "Verify compilation and run tests"
            };
                break;
            }

            return steps;
        }

        std::string strip_comments_and_strings(const std::string& line, bool& in_block_comment) {
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

        std::vector<std::string> split_namespace_path(const std::string& ns_path) {
            std::vector<std::string> parts;
            std::size_t start = 0;
            while (start < ns_path.size()) {
                const auto sep = ns_path.find("::", start);
                if (sep == std::string::npos) {
                    parts.push_back(ns_path.substr(start));
                    break;
                }
                parts.push_back(ns_path.substr(start, sep - start));
                start = sep + 2;
            }
            return parts;
        }

        std::optional<std::size_t> find_guard_define_line(const fs::path& header_path) {
            auto lines_result = file_utils::read_lines(header_path);
            if (lines_result.is_err()) {
                return std::nullopt;
            }

            const auto& lines = lines_result.value();
            const std::regex ifndef_regex(R"(^\s*#\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");
            const std::regex define_regex(R"(^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\b)");
            std::smatch match;

            std::string guard_macro;
            bool found_ifndef = false;

            const std::size_t scan_limit = std::min<std::size_t>(lines.size(), 80);
            for (std::size_t i = 0; i < scan_limit; ++i) {
                if (!found_ifndef) {
                    if (std::regex_match(lines[i], match, ifndef_regex)) {
                        guard_macro = match[1].str();
                        found_ifndef = true;
                    } else if (!lines[i].empty() && lines[i].find("#pragma once") == std::string::npos) {
                        break;
                    }
                    continue;
                }

                if (std::regex_match(lines[i], match, define_regex)) {
                    if (match[1].str() == guard_macro) {
                        return i;
                    }
                    break;
                }
            }

            return std::nullopt;
        }

        std::optional<std::size_t> find_safe_include_insertion_line(const fs::path& header_path) {
            if (auto include_line = find_include_insertion_line(header_path)) {
                return include_line;
            }
            if (const auto guard_line = find_guard_define_line(header_path)) {
                return *guard_line;
            }
            return std::nullopt;
        }

        std::optional<fs::path> resolve_header_path_for_edits(
            const fs::path& header_path,
            const fs::path& project_root
        ) {
            if (header_path.empty()) {
                return std::nullopt;
            }

            fs::path candidate = header_path;
            if (candidate.is_relative() && !project_root.empty()) {
                candidate = (project_root / candidate).lexically_normal();
            } else {
                candidate = candidate.lexically_normal();
            }

            if (fs::exists(candidate)) {
                return candidate;
            }

            const fs::path resolved = resolve_source_path(header_path).lexically_normal();
            if (fs::exists(resolved)) {
                return resolved;
            }

            if (!project_root.empty()) {
                if (auto found = find_file_in_repo(project_root, header_path.filename())) {
                    return found->lexically_normal();
                }
            }

            const fs::path repo_root = find_repository_root(header_path);
            if (!repo_root.empty()) {
                if (auto found = find_file_in_repo(repo_root, header_path.filename())) {
                    return found->lexically_normal();
                }
            }

            return std::nullopt;
        }

        struct ForwardDeclSymbol {
            std::vector<std::string> namespaces;
            std::vector<ScopeFrame> scopes;
            std::string kind;
            std::string name;
        };

        std::vector<std::string> collect_active_namespaces(const std::vector<ScopeFrame>& scope_stack) {
            std::vector<std::string> result;
            for (const auto& scope : scope_stack) {
                if (scope.kind == ScopeFrameKind::Namespace) {
                    result.push_back(scope.name);
                }
            }
            return result;
        }

        std::optional<std::string> derive_matching_close_macro(std::string name) {
            if (name.ends_with("_BEGIN")) {
                name.replace(name.size() - 6, 6, "_END");
                return name;
            }
            if (name.ends_with("_OPEN")) {
                name.replace(name.size() - 5, 5, "_CLOSE");
                return name;
            }
            if (name.ends_with("_PUSH")) {
                name.replace(name.size() - 5, 5, "_POP");
                return name;
            }
            if (name.starts_with("BEGIN_")) {
                return "END_" + name.substr(6);
            }
            if (name.starts_with("OPEN_")) {
                return "CLOSE_" + name.substr(5);
            }
            if (name.starts_with("PUSH_")) {
                return "POP_" + name.substr(5);
            }
            return std::nullopt;
        }

        std::optional<std::string> parse_scope_macro_name(const std::string& line) {
            static const std::regex macro_regex(
                R"(^\s*([A-Z][A-Z0-9_]*|(?:BEGIN|OPEN|PUSH)_[A-Z0-9_]+)\s*(\([^;{}]*\))?\s*$)"
            );

            std::smatch match;
            if (!std::regex_match(line, match, macro_regex)) {
                return std::nullopt;
            }
            return match[1].str();
        }

        std::optional<MacroWrapperScope> parse_scope_macro_open(const std::string& line) {
            const auto macro_name = parse_scope_macro_name(line);
            if (!macro_name.has_value()) {
                return std::nullopt;
            }
            const auto close_name = derive_matching_close_macro(*macro_name);
            if (!close_name.has_value()) {
                return std::nullopt;
            }

            MacroWrapperScope scope;
            scope.open_name = *macro_name;
            scope.open_text = trim_copy(line);
            scope.close_name = *close_name;
            scope.close_text = *close_name;
            return scope;
        }

        std::optional<std::string> parse_scope_macro_close(const std::string& line) {
            const auto macro_name = parse_scope_macro_name(line);
            if (!macro_name.has_value()) {
                return std::nullopt;
            }
            if (macro_name->ends_with("_END") ||
                macro_name->ends_with("_CLOSE") ||
                macro_name->ends_with("_POP") ||
                macro_name->starts_with("END_") ||
                macro_name->starts_with("CLOSE_") ||
                macro_name->starts_with("POP_")) {
                return *macro_name;
            }
            return std::nullopt;
        }

        std::string make_symbol_key(const ForwardDeclSymbol& symbol) {
            std::ostringstream key;
            for (const auto& ns : symbol.namespaces) {
                key << ns << "::";
            }
            key << symbol.kind << " " << symbol.name;
            return key.str();
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

        std::vector<ForwardDeclSymbol> extract_forward_decl_symbols(const fs::path& header_path) {
            auto lines_result = file_utils::read_lines(header_path);
            if (lines_result.is_err()) {
                return {};
            }

            static const std::regex namespace_open_regex(
                R"(^\s*(?:inline\s+)?namespace\s+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*\{)"
            );
            static const std::regex class_or_struct_regex(
                R"(^\s*(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"
            );

            std::vector<ForwardDeclSymbol> symbols;
            std::unordered_set<std::string> seen;
            std::vector<ScopeFrame> scope_stack;
            bool in_block_comment = false;
            std::size_t brace_depth = 0;
            bool template_pending = false;
            bool skip_next_class_decl = false;

            for (const auto& raw_line : lines_result.value()) {
                const std::string line = strip_comments_and_strings(raw_line, in_block_comment);

                std::size_t namespace_depth = 0;
                for (auto it = scope_stack.rbegin(); it != scope_stack.rend(); ++it) {
                    if (it->kind != ScopeFrameKind::Namespace) {
                        continue;
                    }
                    namespace_depth = it->open_depth;
                    break;
                }

                const std::string trimmed = [&]() {
                    const auto start = line.find_first_not_of(" \t\r\n");
                    if (start == std::string::npos) {
                        return std::string{};
                    }
                    const auto end = line.find_last_not_of(" \t\r\n");
                    return line.substr(start, end - start + 1);
                }();

                std::vector<ScopeFrame> pending_namespaces;
                if (std::smatch namespace_match;
                    std::regex_search(trimmed, namespace_match, namespace_open_regex) &&
                    brace_depth == namespace_depth) {
                    const auto parts = split_namespace_path(namespace_match[1].str());
                    for (const auto& part : parts) {
                        ScopeFrame scope;
                        scope.kind = ScopeFrameKind::Namespace;
                        scope.name = part;
                        scope.open_depth = brace_depth + 1;
                        pending_namespaces.push_back(std::move(scope));
                    }
                }

                if (const auto macro_scope = parse_scope_macro_open(trimmed); macro_scope.has_value()) {
                    ScopeFrame frame;
                    frame.kind = ScopeFrameKind::MacroWrapper;
                    frame.macro = *macro_scope;
                    scope_stack.push_back(std::move(frame));
                } else if (const auto close_macro = parse_scope_macro_close(trimmed); close_macro.has_value()) {
                    for (auto it = scope_stack.rbegin(); it != scope_stack.rend(); ++it) {
                        if (it->kind != ScopeFrameKind::MacroWrapper) {
                            continue;
                        }
                        if (it->macro.close_name != *close_macro) {
                            continue;
                        }
                        it->macro.close_text = trimmed;
                        scope_stack.erase(std::next(it).base());
                        break;
                    }
                }

                if (trimmed.rfind("template", 0) == 0) {
                    template_pending = true;
                    if (trimmed.find('>') != std::string::npos) {
                        template_pending = false;
                        skip_next_class_decl = true;
                    }
                }

                const bool can_scan_class_decl =
                    !trimmed.empty() &&
                    brace_depth == namespace_depth &&
                    !template_pending;
                if (can_scan_class_decl) {
                    std::smatch class_match;
                    if (std::regex_search(trimmed, class_match, class_or_struct_regex)) {
                        if (!skip_next_class_decl &&
                            (trimmed.find('{') != std::string::npos || trimmed.find(';') != std::string::npos)) {
                            const auto name = extract_declared_type_name(
                                trimmed.substr(static_cast<std::size_t>(class_match.position()))
                            );
                            if (!name.has_value()) {
                                skip_next_class_decl = false;
                                continue;
                            }
                            ForwardDeclSymbol symbol;
                            symbol.scopes = scope_stack;
                            symbol.namespaces = collect_active_namespaces(scope_stack);
                            symbol.kind = class_match[1].str();
                            symbol.name = *name;
                            if (seen.insert(make_symbol_key(symbol)).second) {
                                symbols.push_back(std::move(symbol));
                            }
                        }
                        skip_next_class_decl = false;
                    }
                }

                for (const char ch : line) {
                    if (ch == '{') {
                        ++brace_depth;
                    } else if (ch == '}') {
                        if (brace_depth > 0) {
                            --brace_depth;
                        }
                    }
                }

                if (!pending_namespaces.empty()) {
                    for (auto& pending_namespace : pending_namespaces) {
                        if (brace_depth >= pending_namespace.open_depth) {
                            scope_stack.push_back(std::move(pending_namespace));
                        }
                    }
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
            }

            return symbols;
        }

        bool file_defines_macro(const fs::path& file, const std::string& macro_name) {
            std::ifstream in(file);
            if (!in) {
                return false;
            }

            const std::regex define_regex(
                "^\\s*#\\s*define\\s+" + std::regex_replace(
                    macro_name,
                    std::regex(R"([.^$|()\\[\]{}*+?])"),
                    R"(\$&)"
                ) + R"((?:\b|\s*\())"
            );
            std::string line;
            while (std::getline(in, line)) {
                if (std::regex_search(line, define_regex)) {
                    return true;
                }
            }
            return false;
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

        std::optional<std::vector<IncludeDirective>> resolve_support_includes(
            const fs::path& header_path,
            const fs::path& project_root,
            const std::vector<ForwardDeclSymbol>& symbols
        ) {
            std::unordered_set<std::string> unresolved_macros;
            for (const auto& symbol : symbols) {
                for (const auto& scope : symbol.scopes) {
                    if (scope.kind != ScopeFrameKind::MacroWrapper) {
                        continue;
                    }
                    unresolved_macros.insert(scope.macro.open_name);
                    unresolved_macros.insert(scope.macro.close_name);
                }
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

        std::string include_directive_text(const IncludeDirective& include) {
            if (include.is_system) {
                return "#include <" + include.header_name + ">";
            }
            return "#include \"" + include.header_name + "\"";
        }

        std::optional<std::string> generate_forward_header_content(
            const fs::path& header_path,
            const fs::path& project_root
        ) {
            auto symbols = extract_forward_decl_symbols(header_path);
            if (symbols.empty()) {
                return std::nullopt;
            }

            const auto support_includes = resolve_support_includes(header_path, project_root, symbols);
            if (!support_includes.has_value()) {
                return std::nullopt;
            }

            std::ranges::sort(symbols, [](const ForwardDeclSymbol& a, const ForwardDeclSymbol& b) {
                if (a.scopes.size() != b.scopes.size()) {
                    return a.scopes.size() < b.scopes.size();
                }
                if (a.namespaces != b.namespaces) {
                    return a.namespaces < b.namespaces;
                }
                if (a.kind != b.kind) {
                    return a.kind < b.kind;
                }
                return a.name < b.name;
            });

            std::ostringstream out;
            out << "#pragma once\n\n";
            out << "#ifdef __cplusplus\n";
            for (const auto& include : *support_includes) {
                out << include_directive_text(include) << "\n";
            }
            if (!support_includes->empty()) {
                out << "\n";
            }

            std::vector<ScopeFrame> opened_scopes;
            auto render_scope_key = [](const ScopeFrame& scope) {
                if (scope.kind == ScopeFrameKind::Namespace) {
                    return "namespace:" + scope.name;
                }
                return "macro:" + scope.macro.open_text;
            };
            auto close_scope = [&](const ScopeFrame& scope) {
                if (scope.kind == ScopeFrameKind::Namespace) {
                    out << "}  // namespace " << scope.name << "\n";
                } else {
                    out << scope.macro.close_text << "\n";
                }
            };
            auto open_scope = [&](const ScopeFrame& scope) {
                if (scope.kind == ScopeFrameKind::Namespace) {
                    out << "namespace " << scope.name << " {\n";
                } else {
                    out << scope.macro.open_text << "\n";
                }
            };

            auto adjust_scope_stack = [&](const std::vector<ScopeFrame>& target) {
                std::size_t common_prefix = 0;
                while (common_prefix < opened_scopes.size() &&
                       common_prefix < target.size() &&
                       render_scope_key(opened_scopes[common_prefix]) == render_scope_key(target[common_prefix])) {
                    ++common_prefix;
                }

                for (std::size_t i = opened_scopes.size(); i > common_prefix; --i) {
                    close_scope(opened_scopes[i - 1]);
                }
                opened_scopes.resize(common_prefix);

                for (std::size_t i = common_prefix; i < target.size(); ++i) {
                    open_scope(target[i]);
                    opened_scopes.push_back(target[i]);
                }
            };

            for (const auto& symbol : symbols) {
                adjust_scope_stack(symbol.scopes);
                out << symbol.kind << " " << symbol.name << ";\n";
            }

            for (std::size_t i = opened_scopes.size(); i > 0; --i) {
                close_scope(opened_scopes[i - 1]);
            }

            out << "#endif  // __cplusplus\n";

            return out.str();
        }

    }  // namespace

    Result<SuggestionResult, Error> HeaderSplitSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& deps = context.analysis.dependencies;

        const auto min_parse_time = context.options.heuristics.headers.min_parse_time;
        const auto min_includer_count = context.options.heuristics.headers.min_includers_for_split;

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& header : deps.headers) {
            if (context.is_cancelled()) {
                break;
            }
            ++analyzed;

            if (!context.target_files_lookup.empty()) {
                bool any_target = false;
                for (const auto& includer : header.included_by) {
                    if (context.should_analyze(includer)) {
                        any_target = true;
                        break;
                    }
                }
                if (!any_target) {
                    ++skipped;
                    continue;
                }
            }

            if (!is_header(header.path)) {
                ++skipped;
                continue;
            }
            if (is_likely_system_header(header.path)) {
                ++skipped;
                continue;
            }

            if (header.total_parse_time < min_parse_time) {
                ++skipped;
                continue;
            }

            if (header.including_files < min_includer_count) {
                ++skipped;
                continue;
            }

            const auto resolved_header_path = resolve_header_path_for_edits(header.path, context.project_root);
            const std::string fwd_header_name = suggest_split_name(header.path, "fwd");

            // Check if already split
            const std::string filename = header.path.filename().string();
            std::string lower_filename;
            lower_filename.reserve(filename.size());
            for (const char c : filename) {
                lower_filename += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }

            bool already_split = lower_filename.find("_fwd") != std::string::npos ||
                                 lower_filename.find("_types") != std::string::npos ||
                                 lower_filename.find("_decl") != std::string::npos ||
                                 lower_filename.find("_impl") != std::string::npos ||
                                 lower_filename.find("_internal") != std::string::npos ||
                                 lower_filename.find("_detail") != std::string::npos;
            if (!already_split && resolved_header_path.has_value()) {
                if (find_include_for_header(*resolved_header_path, fwd_header_name).has_value()) {
                    already_split = true;
                }
            }
            if (already_split) {
                ++skipped;
                continue;
            }

            const SplitPattern pattern = determine_split_pattern(header.path, header.including_files);

            const double confidence = calculate_confidence(
                header.total_parse_time,
                header.including_files,
                header.inclusion_count
            );

            const Priority priority = calculate_priority(
                header.total_parse_time,
                header.including_files
            );

            const Duration savings = estimate_savings(
                header.total_parse_time,
                header.including_files,
                pattern
            );

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("split", header.path);
            suggestion.type = SuggestionType::HeaderSplit;
            suggestion.priority = priority;
            suggestion.confidence = confidence;

            std::ostringstream title;
            title << "Consider splitting " << filename;
            suggestion.title = title.str();

            auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                header.total_parse_time).count();

            const std::string types_header_name = suggest_split_name(header.path, "types");

            std::ostringstream desc;
            desc << "Header '" << make_repo_relative(header.path) << "' takes "
                 << parse_ms << "ms to parse and is included by "
                 << header.including_files << " files";
            if (header.inclusion_count > header.including_files) {
                desc << " (" << header.inclusion_count << " total inclusions)";
            }
            desc << ".\n\n";

            desc << "**Suggested split pattern:**\n";
            switch (pattern) {
                case SplitPattern::ForwardDecl:
                    desc << "- Create `" << fwd_header_name << "` for forward declarations only.\n";
                    desc << "- Keep full definitions in `" << filename << "` and include `" << fwd_header_name << "` there.\n";
                    desc << "- Update includers that only need pointers/references to include `" << fwd_header_name << "`.\n";
                    break;
                case SplitPattern::TypesAndFwd:
                    desc << "- Create `" << fwd_header_name << "` for forward declarations.\n";
                    desc << "- Create `" << types_header_name << "` for shared lightweight type definitions.\n";
                    desc << "- Keep `" << filename << "` as the full interface that includes the smaller headers.\n";
                    break;
                case SplitPattern::FunctionalGroups:
                    desc << "- Split `" << filename << "` into focused headers such as `"
                         << suggest_split_name(header.path, "group1") << "` and `"
                         << suggest_split_name(header.path, "group2") << "`.\n";
                    desc << "- Keep unrelated declarations out of translation units that do not need them.\n";
                    break;
                case SplitPattern::PublicPrivate:
                    desc << "- Keep the public API in `" << filename << "`.\n";
                    desc << "- Move internal details into `" << suggest_split_name(header.path, "internal") << "`.\n";
                    desc << "- Limit internal includes to implementation files.\n";
                    break;
            }
            desc << "\n";
            desc << "Splitting into smaller, focused headers can reduce compile times when files only need a subset of declarations.";
            suggestion.description = desc.str();

            std::ostringstream rationale;
            rationale << "Large, frequently-included headers cause unnecessary "
                      << "parsing overhead. ";

            switch (pattern) {
            case SplitPattern::ForwardDecl:
                rationale << "This header would benefit from a forward declaration "
                          << "header (_fwd.h) since many includers likely only need "
                          << "to reference types without seeing their full definition.";
                break;
            case SplitPattern::TypesAndFwd:
                rationale << "Separating type definitions from forward declarations "
                          << "allows includers to choose the minimal header they need.";
                break;
            case SplitPattern::FunctionalGroups:
                rationale << "This utility-style header contains multiple unrelated "
                          << "groups that could be split into focused headers.";
                break;
            case SplitPattern::PublicPrivate:
                rationale << "Separating public API from internal details prevents "
                          << "external code from depending on implementation.";
                break;
            }
            suggestion.rationale = rationale.str();

            suggestion.estimated_savings = savings;

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            suggestion.target_file.path = header.path;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Split into smaller, focused headers";

            std::ostringstream before;
            before << "// " << make_repo_relative(header.path) << "\n"
                   << "#pragma once\n\n"
                   << "// Current: Single monolithic header\n"
                   << "// Parse time: " << parse_ms << "ms\n"
                   << "// Included by " << header.including_files << " files";
            if (header.inclusion_count > header.including_files) {
                before << " (" << header.inclusion_count << " total inclusions)";
            }
            before << "\n\n"
                   << "// All " << header.including_files << " files must parse:\n"
                   << "// - All type definitions\n"
                   << "// - All function declarations\n"
                   << "// - All inline implementations\n"
                   << "// - All template instantiations\n"
                   << "//\n"
                   << "// Even if they only need forward declarations!\n"
                   << "// Total wasted time: ~"
                   << (static_cast<std::size_t>(parse_ms) * header.including_files / 3) << "ms";
            suggestion.before_code.file = header.path;
            suggestion.before_code.code = before.str();

            std::ostringstream after;
            switch (pattern) {
                case SplitPattern::ForwardDecl:
                    after << "// " << fwd_header_name << " - Forward declarations only\n"
                          << "#pragma once\n\n"
                          << "// Fast to parse - just forward decls\n"
                          << "// Use when you only need pointers/references\n"
                          << "// Files using forward decls: ~" << (header.including_files * 30 / 100) << "\n\n"
                          << "// Original header\n"
                          << "// " << make_repo_relative(header.path) << "\n"
                          << "#pragma once\n"
                          << "#include \"" << fwd_header_name << "\"\n\n"
                          << "// Full definitions for files that need them\n"
                          << "// Files needing full header: ~" << (header.including_files * 70 / 100) << "\n\n"
                          << "// Estimated savings: ~"
                          << std::chrono::duration_cast<std::chrono::milliseconds>(savings).count() << "ms";
                    break;

                case SplitPattern::TypesAndFwd:
                    after << "// " << fwd_header_name << " - Forward declarations\n"
                          << "#pragma once\n\n"
                          << "// " << types_header_name << " - Type definitions only\n"
                          << "#pragma once\n"
                          << "#include \"" << fwd_header_name << "\"\n\n"
                          << "// " << make_repo_relative(header.path) << " - Full header\n"
                          << "#pragma once\n"
                          << "#include \"" << types_header_name << "\"\n\n"
                          << "// Includers can now choose:\n"
                          << "// - " << fwd_header_name << " (fastest, forward decls only)\n"
                          << "// - " << types_header_name << " (type defs, no functions)\n"
                          << "// - " << header.path.filename().string() << " (complete interface)\n\n"
                          << "// Estimated savings: ~"
                          << std::chrono::duration_cast<std::chrono::milliseconds>(savings).count() << "ms";
                    break;

                case SplitPattern::FunctionalGroups:
                    after << "// Split " << make_repo_relative(header.path) << " into focused headers:\n\n"
                          << "// " << suggest_split_name(header.path, "group1") << " - Specific functionality\n"
                          << "// " << suggest_split_name(header.path, "group2") << " - Another functionality\n\n"
                          << "// Benefits:\n"
                          << "// - Files only include what they use\n"
                          << "// - Reduces transitive dependencies\n"
                          << "// - Faster incremental builds\n\n"
                          << "// Estimated savings: ~"
                          << std::chrono::duration_cast<std::chrono::milliseconds>(savings).count() << "ms";
                    break;

                case SplitPattern::PublicPrivate:
                    after << "// " << header.path.filename().string() << " - Public API only\n"
                          << "#pragma once\n\n"
                          << "// " << suggest_split_name(header.path, "internal") << " - Internal details\n"
                          << "#pragma once\n"
                          << "#include \"" << header.path.filename().string() << "\"\n\n"
                          << "// External code uses: " << header.path.filename().string() << "\n"
                          << "// Internal code uses: " << suggest_split_name(header.path, "internal") << "\n\n"
                          << "// Estimated savings: ~"
                          << std::chrono::duration_cast<std::chrono::milliseconds>(savings).count() << "ms";
                    break;
            }
            suggestion.after_code.file = "Proposed split";
            suggestion.after_code.code = after.str();

            suggestion.implementation_steps = generate_implementation_steps(header.path, pattern);

            suggestion.impact.total_files_affected = header.including_files;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;

            suggestion.caveats = {
                "Requires understanding of symbol dependencies between declarations",
                "May require updating include statements in many files",
                "Forward declarations cannot be used when full type is needed (sizeof, members)",
                "Split headers need to be kept in sync with main header",
                "IDE/tooling support may need reconfiguration"
            };

            suggestion.verification =
                "1. Create split headers incrementally, verifying compilation at each step\n"
                "2. Use include-what-you-use (IWYU) to optimize includes in client code\n"
                "3. Measure compile times before and after to verify improvement\n"
                "4. Run full test suite to ensure no functionality changes";

            const bool supports_direct_edits =
                pattern == SplitPattern::ForwardDecl && resolved_header_path.has_value();
            bool created_fwd_header = false;

            if (supports_direct_edits) {
                const fs::path fwd_header_path = resolved_header_path->parent_path() / fwd_header_name;
                const bool fwd_exists = fs::exists(fwd_header_path);
                const bool include_exists =
                    find_include_for_header(*resolved_header_path, fwd_header_name).has_value();

                if (!fwd_exists) {
                    if (auto fwd_content = generate_forward_header_content(
                            *resolved_header_path,
                            context.project_root
                        )) {
                        TextEdit create_fwd;
                        create_fwd.file = fwd_header_path;
                        create_fwd.start_line = 0;
                        create_fwd.start_col = 0;
                        create_fwd.end_line = 0;
                        create_fwd.end_col = 0;
                        create_fwd.new_text = *fwd_content;
                        if (!create_fwd.new_text.empty() && create_fwd.new_text.back() != '\n') {
                            create_fwd.new_text.push_back('\n');
                        }
                        suggestion.edits.push_back(std::move(create_fwd));
                        created_fwd_header = true;

                        FileTarget create_target;
                        create_target.path = fwd_header_path;
                        create_target.action = FileAction::Create;
                        create_target.note = "Create generated forward-declaration companion header";
                        suggestion.secondary_files.push_back(std::move(create_target));
                    }
                }

                if (!include_exists && (fwd_exists || created_fwd_header)) {
                    const std::string include_line = "#include \"" + fwd_header_name + "\"";
                    if (auto insert_line = find_safe_include_insertion_line(*resolved_header_path)) {
                        suggestion.edits.push_back(make_insert_after_line_edit(
                            *resolved_header_path,
                            *insert_line,
                            include_line
                        ));
                    } else {
                        suggestion.edits.push_back(make_insert_at_start_edit(
                            *resolved_header_path,
                            include_line
                        ));
                    }
                }
            }

            suggestion.is_safe = !suggestion.edits.empty();
            if (suggestion.is_safe) {
                suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
                suggestion.application_summary = "Auto-apply via direct text edits";
                suggestion.application_guidance =
                    "BHA can create the _fwd header and wire it into the original header. Rebuild and validate before broader include pruning.";
            } else {
                suggestion.application_mode = SuggestionApplicationMode::Advisory;
                suggestion.application_summary = "Manual refactor recommended";
                suggestion.application_guidance =
                    "This split shape requires manual symbol partitioning. Keep headers self-contained and compile-validated after each step.";
            }

            result.suggestions.push_back(std::move(suggestion));
        }

        result.items_analyzed = analyzed;
        result.items_skipped = skipped;

        std::ranges::sort(result.suggestions,
                          [](const Suggestion& a, const Suggestion& b) {
                              return a.estimated_savings > b.estimated_savings;
                          });

        auto end_time = std::chrono::steady_clock::now();
        result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);

        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_header_split_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<HeaderSplitSuggester>()
        );
    }
}  // namespace bha::suggestions
