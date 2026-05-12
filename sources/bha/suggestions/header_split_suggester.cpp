//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/header_split_suggester.hpp"
#include "bha/suggestions/scope_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {

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
            constexpr double fwd_only_ratio = 0.30;
            double reduction_factor = fwd_only_ratio;
            switch (pattern) {
                case SplitPattern::ForwardDecl:
                    reduction_factor = fwd_only_ratio;
                    break;
                case SplitPattern::TypesAndFwd:
                    reduction_factor = 0.25;
                    break;
                case SplitPattern::FunctionalGroups:
                    reduction_factor = 0.20;
                    break;
                case SplitPattern::PublicPrivate:
                    reduction_factor = 0.15;
                    break;
            }

            // Aggregate parse time already captures repeated parsing work, so only
            // apply a bounded fanout factor here to avoid over-crediting wide but
            // shallow include graphs.
            constexpr std::size_t kHeaderSplitFanoutSaturation = 48;
            const double fanout_factor = saturating_count_factor(
                includer_count,
                kHeaderSplitFanoutSaturation
            );

            return scaled_duration(parse_time, reduction_factor * fanout_factor);
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

        std::string make_canonical_symbol_key(const ForwardDeclSymbol& symbol) {
            std::ostringstream key;
            for (const auto& name : symbol.namespaces) {
                key << name << "::";
            }
            key << symbol.name;
            return key.str();
        }

        std::string normalize_class_key(const std::string& class_key) {
            return class_key == "struct" ? "struct" : "class";
        }

        std::string merge_class_key_preference(
            const std::string& existing,
            const std::string& observed
        ) {
            const std::string existing_normalized = normalize_class_key(existing);
            const std::string observed_normalized = normalize_class_key(observed);
            // Prefer `class` when conflicting declarations exist to avoid emitting
            // `struct` forward declarations for class definitions (MSVC ABI warning risk).
            if (existing_normalized == "class" || observed_normalized == "class") {
                return "class";
            }
            return "struct";
        }

        std::string make_symbol_identity_key(const ForwardDeclSymbol& symbol) {
            std::ostringstream key;
            for (const auto& scope : symbol.scopes) {
                if (scope.kind == ScopeFrameKind::Namespace) {
                    key << "namespace:" << scope.name << "::";
                } else {
                    key << "macro:" << scope.macro.open_text << "::";
                }
            }
            key << symbol.name;
            return key.str();
        }

        bool has_nested_name_qualifier_after_match(
            const std::string& text,
            const std::smatch& match,
            const std::size_t capture_index
        ) {
            std::size_t pos = static_cast<std::size_t>(match.position(capture_index)) +
                              static_cast<std::size_t>(match.length(capture_index));
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            return pos + 1 < text.size() && text[pos] == ':' && text[pos + 1] == ':';
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
            std::unordered_map<std::string, ForwardDeclSymbol> symbols_by_identity;
            std::unordered_map<std::string, std::string> preferred_class_key_by_canonical_symbol;
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
                        if (has_nested_name_qualifier_after_match(trimmed, class_match, 2)) {
                            skip_next_class_decl = false;
                            continue;
                        }
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
                            symbol.kind = normalize_class_key(class_match[1].str());
                            symbol.name = *name;
                            const std::string canonical_symbol = make_canonical_symbol_key(symbol);
                            auto preferred_kind =
                                preferred_class_key_by_canonical_symbol.find(canonical_symbol);
                            if (preferred_kind == preferred_class_key_by_canonical_symbol.end()) {
                                preferred_class_key_by_canonical_symbol.emplace(canonical_symbol, symbol.kind);
                            } else {
                                preferred_kind->second = merge_class_key_preference(
                                    preferred_kind->second,
                                    symbol.kind
                                );
                            }

                            const std::string identity = make_symbol_identity_key(symbol);
                            if (auto existing = symbols_by_identity.find(identity);
                                existing != symbols_by_identity.end()) {
                                existing->second.kind = merge_class_key_preference(
                                    existing->second.kind,
                                    symbol.kind
                                );
                            } else {
                                symbols_by_identity.emplace(identity, std::move(symbol));
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

            symbols.reserve(symbols_by_identity.size());
            for (auto& [_, symbol] : symbols_by_identity) {
                const auto preferred_kind =
                    preferred_class_key_by_canonical_symbol.find(make_canonical_symbol_key(symbol));
                if (preferred_kind != preferred_class_key_by_canonical_symbol.end()) {
                    symbol.kind = preferred_kind->second;
                }
                symbols.push_back(std::move(symbol));
            }
            return symbols;
        }

        bool should_skip_support_scan_directory(const fs::path& path) {
            const std::string name = path.filename().string();
            return name == ".git" ||
                   name == ".svn" ||
                   name == ".hg" ||
                   name == "build" ||
                   name == "cmake-build-debug" ||
                   name == "cmake-build-release" ||
                   name == ".lsp-optimization-backup";
        }

        std::optional<fs::path> find_macro_definition_header(
            const fs::path& repo_root,
            const fs::path& excluded_header,
            const std::string& macro_name
        ) {
            if (repo_root.empty() || macro_name.empty()) {
                return std::nullopt;
            }
            const fs::path normalized_excluded_header =
                resolve_source_path(excluded_header).lexically_normal();

            std::error_code ec;
            fs::recursive_directory_iterator it(
                repo_root,
                fs::directory_options::skip_permission_denied,
                ec
            );
            const fs::recursive_directory_iterator end;
            while (!ec && it != end) {
                const fs::path path = it->path();
                if (it->is_directory(ec)) {
                    if (should_skip_support_scan_directory(path)) {
                        it.disable_recursion_pending();
                    }
                    it.increment(ec);
                    continue;
                }

                const fs::path normalized_path = path.lexically_normal();
                if (normalized_path != normalized_excluded_header &&
                    is_header_file_path(normalized_path) &&
                    file_defines_macro(normalized_path, macro_name)) {
                    return normalized_path;
                }
                it.increment(ec);
            }

            return std::nullopt;
        }

        std::string support_include_name_for_header(
            const fs::path& support_header,
            const fs::path& project_root
        ) {
            if (!project_root.empty()) {
                const fs::path normalized_root = project_root.lexically_normal();
                const fs::path normalized_header = support_header.lexically_normal();
                const fs::path public_include_root = normalized_root / "include";

                std::error_code ec;
                fs::path rel = fs::relative(normalized_header, public_include_root, ec);
                if (!ec && !rel.empty() && rel.native().rfind("..", 0) != 0) {
                    return rel.generic_string();
                }

                ec.clear();
                rel = fs::relative(normalized_header, normalized_root, ec);
                if (!ec && !rel.empty() && rel.native().rfind("..", 0) != 0) {
                    return rel.generic_string();
                }
            }

            return support_header.filename().generic_string();
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
                    if (scope.kind == ScopeFrameKind::MacroWrapper) {
                        unresolved_macros.insert(scope.macro.open_name);
                        unresolved_macros.insert(scope.macro.close_name);
                    } else if (scope.kind == ScopeFrameKind::Namespace &&
                               is_macro_like_identifier(scope.name)) {
                        unresolved_macros.insert(scope.name);
                    }
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
                const fs::path repo_root = !project_root.empty()
                    ? project_root.lexically_normal()
                    : find_repository_root(header_path);

                std::vector<std::string> remaining_macros(
                    unresolved_macros.begin(),
                    unresolved_macros.end()
                );
                for (const auto& macro_name : remaining_macros) {
                    const auto support_header = find_macro_definition_header(
                        repo_root,
                        header_path,
                        macro_name
                    );
                    if (!support_header.has_value()) {
                        continue;
                    }

                    const std::string include_name =
                        support_include_name_for_header(*support_header, repo_root);
                    if (include_name.empty()) {
                        continue;
                    }

                    if (seen_headers.insert(include_name).second) {
                        IncludeDirective support;
                        support.header_name = include_name;
                        support.is_system = false;
                        includes.push_back(std::move(support));
                    }
                    unresolved_macros.erase(macro_name);
                }
            }

            if (!unresolved_macros.empty()) {
                return std::nullopt;
            }
            return includes;
        }

        std::string extract_leading_comment_preamble(const fs::path& header_path) {
            std::ifstream in(header_path);
            if (!in) {
                return {};
            }

            std::vector<std::string> lines;
            std::string line;
            while (std::getline(in, line)) {
                lines.push_back(line);
            }
            if (lines.empty()) {
                return {};
            }

            std::size_t index = 0;
            bool saw_comment = false;
            bool in_block_comment = false;
            std::vector<std::string> preamble;

            while (index < lines.size()) {
                const std::string& original = lines[index];
                const auto first_non_ws = original.find_first_not_of(" \t\r");
                const std::string trimmed = first_non_ws == std::string::npos
                    ? std::string{}
                    : original.substr(first_non_ws);

                if (in_block_comment) {
                    preamble.push_back(original);
                    saw_comment = true;
                    if (trimmed.find("*/") != std::string::npos) {
                        in_block_comment = false;
                    }
                    ++index;
                    continue;
                }

                if (trimmed.empty()) {
                    if (!saw_comment) {
                        ++index;
                        continue;
                    }
                    preamble.push_back(original);
                    ++index;
                    continue;
                }

                if (trimmed.rfind("//", 0) == 0) {
                    saw_comment = true;
                    preamble.push_back(original);
                    ++index;
                    continue;
                }

                if (trimmed.rfind("/*", 0) == 0) {
                    saw_comment = true;
                    in_block_comment = trimmed.find("*/") == std::string::npos;
                    preamble.push_back(original);
                    ++index;
                    continue;
                }

                break;
            }

            while (!preamble.empty()) {
                const auto non_ws = preamble.back().find_first_not_of(" \t\r");
                if (non_ws == std::string::npos) {
                    preamble.pop_back();
                } else {
                    break;
                }
            }

            if (!saw_comment || preamble.empty()) {
                return {};
            }

            std::ostringstream out;
            for (std::size_t i = 0; i < preamble.size(); ++i) {
                out << preamble[i];
                if (i + 1 < preamble.size()) {
                    out << '\n';
                }
            }
            return out.str();
        }

        std::vector<std::size_t> collect_redundant_forward_decl_lines(
            const fs::path& header_path,
            const std::vector<ForwardDeclSymbol>& symbols
        ) {
            auto lines_result = file_utils::read_lines(header_path);
            if (lines_result.is_err()) {
                return {};
            }

            std::unordered_set<std::string> known_symbols;
            known_symbols.reserve(symbols.size());
            for (const auto& symbol : symbols) {
                known_symbols.insert(make_symbol_identity_key(symbol));
            }

            static const std::regex namespace_open_regex(
                R"(^\s*(?:inline\s+)?namespace\s+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*\{)"
            );
            static const std::regex class_or_struct_decl_regex(
                R"(^\s*(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;\s*$)"
            );

            std::vector<std::size_t> redundant_lines;
            std::vector<ScopeFrame> scope_stack;
            bool in_block_comment = false;
            bool template_pending = false;
            std::size_t brace_depth = 0;

            const auto& lines = lines_result.value();
            for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
                const std::string line = strip_comments_and_strings(lines[line_index], in_block_comment);

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
                    template_pending = trimmed.find('>') == std::string::npos;
                } else if (!template_pending && brace_depth == namespace_depth) {
                    std::smatch match;
                    if (std::regex_match(trimmed, match, class_or_struct_decl_regex)) {
                        ForwardDeclSymbol candidate;
                        candidate.scopes = scope_stack;
                        candidate.namespaces = collect_active_namespaces(scope_stack);
                        candidate.kind = normalize_class_key(match[1].str());
                        candidate.name = match[2].str();
                        if (known_symbols.contains(make_symbol_identity_key(candidate))) {
                            redundant_lines.push_back(line_index);
                        }
                    }
                } else if (template_pending && trimmed.find('>') != std::string::npos) {
                    template_pending = false;
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

            return redundant_lines;
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

            GeneratedTextBuilder out;
            const std::string source_preamble = extract_leading_comment_preamble(header_path);
            if (!source_preamble.empty()) {
                std::istringstream preamble_stream(source_preamble);
                std::string preamble_line;
                while (std::getline(preamble_stream, preamble_line)) {
                    out.add_line(preamble_line);
                }
                out.add_blank_line();
            }
            out.add_line("#pragma once");
            out.add_blank_line();
            out.add_line("#ifdef __cplusplus");
            std::vector<std::string> include_lines;
            include_lines.reserve(support_includes->size());
            for (const auto& include : *support_includes) {
                include_lines.push_back(include_directive_text(include));
            }
            append_include_block(out, include_lines);

            std::vector<ScopeFrame> opened_scopes;
            auto render_scope_key = [](const ScopeFrame& scope) {
                if (scope.kind == ScopeFrameKind::Namespace) {
                    return "namespace:" + scope.name;
                }
                return "macro:" + scope.macro.open_text;
            };
            auto close_scope = [&](const ScopeFrame& scope) {
                if (scope.kind == ScopeFrameKind::Namespace) {
                    out.add_line("}  // namespace " + scope.name);
                } else {
                    out.add_line(scope.macro.close_text);
                }
            };
            auto open_scope = [&](const ScopeFrame& scope) {
                if (scope.kind == ScopeFrameKind::Namespace) {
                    out.add_line("namespace " + scope.name + " {");
                } else {
                    out.add_line(scope.macro.open_text);
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
                out.add_line(symbol.kind + " " + symbol.name + ";");
            }

            for (std::size_t i = opened_scopes.size(); i > 0; --i) {
                close_scope(opened_scopes[i - 1]);
            }

            out.add_line("#endif  // __cplusplus");

            return out.str();
        }

        struct ForwardReplacementOpportunity {
            fs::path includer_path;
            IncludeDirective include_directive;
            std::string replacement_include;
            std::size_t pointer_or_reference_mentions = 0;
            std::vector<std::string> referenced_symbols;
        };

        using IdentifierSet = std::unordered_set<std::string>;

        bool looks_like_macro_identifier(const std::string& identifier) {
            bool saw_alpha = false;
            for (const char ch : identifier) {
                const auto value = static_cast<unsigned char>(ch);
                if (std::isalpha(value)) {
                    saw_alpha = true;
                    if (!std::isupper(value)) {
                        return false;
                    }
                } else if (!std::isdigit(value) && ch != '_') {
                    return false;
                }
            }
            return saw_alpha;
        }

        std::optional<std::pair<std::size_t, std::size_t>> find_outer_paren_span(
            const std::string& text
        ) {
            std::size_t template_depth = 0;
            std::size_t paren_depth = 0;
            std::optional<std::size_t> open_paren;

            for (std::size_t i = 0; i < text.size(); ++i) {
                const char ch = text[i];
                if (ch == '<') {
                    ++template_depth;
                    continue;
                }
                if (ch == '>' && template_depth > 0) {
                    --template_depth;
                    continue;
                }
                if (template_depth > 0) {
                    continue;
                }
                if (ch == '(') {
                    if (paren_depth == 0) {
                        open_paren = i;
                    }
                    ++paren_depth;
                    continue;
                }
                if (ch == ')' && paren_depth > 0) {
                    --paren_depth;
                    if (paren_depth == 0 && open_paren.has_value()) {
                        return std::pair{*open_paren, i};
                    }
                }
            }

            return std::nullopt;
        }

        bool callable_tail_looks_valid(std::string tail) {
            tail = trim_whitespace_copy(tail);
            while (!tail.empty()) {
                if (tail[0] == ';' || tail[0] == '{') {
                    return true;
                }
                if (tail.rfind("noexcept", 0) == 0) {
                    tail.erase(0, std::string("noexcept").size());
                    tail = trim_whitespace_copy(tail);
                    if (!tail.empty() && tail[0] == '(') {
                        if (const auto span = find_outer_paren_span(tail)) {
                            tail.erase(0, span->second + 1);
                            tail = trim_whitespace_copy(tail);
                            continue;
                        }
                        return false;
                    }
                    continue;
                }
                if (tail.rfind("[[", 0) == 0) {
                    const auto close = tail.find("]]");
                    if (close == std::string::npos) {
                        return false;
                    }
                    tail.erase(0, close + 2);
                    tail = trim_whitespace_copy(tail);
                    continue;
                }
                if (tail.rfind("__attribute__", 0) == 0) {
                    tail.erase(0, std::string("__attribute__").size());
                    tail = trim_whitespace_copy(tail);
                    if (!tail.empty() && tail[0] == '(') {
                        if (const auto span = find_outer_paren_span(tail)) {
                            tail.erase(0, span->second + 1);
                            tail = trim_whitespace_copy(tail);
                            continue;
                        }
                    }
                    return false;
                }

                std::size_t token_end = 0;
                while (token_end < tail.size() &&
                       (std::isalnum(static_cast<unsigned char>(tail[token_end])) ||
                        tail[token_end] == '_')) {
                    ++token_end;
                }
                if (token_end == 0) {
                    return false;
                }

                const std::string token = tail.substr(0, token_end);
                if (!looks_like_macro_identifier(token)) {
                    return false;
                }
                tail.erase(0, token_end);
                tail = trim_whitespace_copy(tail);
            }

            return false;
        }

        std::optional<std::string> extract_callable_name_from_declaration(
            const std::string& declaration
        ) {
            static const std::array<std::string_view, 12> kRejectedPrefixes{
                "#",       "if",       "for",      "while",    "switch",   "return",
                "class ",  "struct ",  "enum ",    "using ",   "typedef ", "static_assert"
            };

            const std::string trimmed = trim_whitespace_copy(declaration);
            if (trimmed.empty()) {
                return std::nullopt;
            }
            for (const auto prefix : kRejectedPrefixes) {
                if (trimmed.rfind(prefix, 0) == 0) {
                    return std::nullopt;
                }
            }

            const auto paren_span = find_outer_paren_span(trimmed);
            if (!paren_span.has_value()) {
                return std::nullopt;
            }
            if (!callable_tail_looks_valid(trim_whitespace_copy(trimmed.substr(paren_span->second + 1)))) {
                return std::nullopt;
            }

            static const std::regex callable_name_regex(
                R"((?:[A-Za-z_][A-Za-z0-9_]*::)*([A-Za-z_][A-Za-z0-9_]*)\s*$)"
            );

            const std::string head = trimmed.substr(0, paren_span->first);
            std::smatch match;
            if (!std::regex_search(head, match, callable_name_regex)) {
                return std::nullopt;
            }

            const std::string name = match[1].str();
            if (name == "operator" || looks_like_macro_identifier(name)) {
                return std::nullopt;
            }
            return name;
        }

        std::vector<std::string> extract_declared_callable_names(const fs::path& header_path) {
            auto lines_result = file_utils::read_lines(header_path);
            if (lines_result.is_err()) {
                return {};
            }

            std::vector<std::string> names;
            std::unordered_set<std::string> seen;
            bool in_block_comment = false;
            bool declaration_pending = false;
            std::string declaration;

            for (const auto& raw_line : lines_result.value()) {
                const std::string line = strip_comments_and_strings(raw_line, in_block_comment);
                const std::string trimmed = trim_whitespace_copy(line);
                if (trimmed.empty()) {
                    continue;
                }

                const bool starts_candidate =
                    !declaration_pending &&
                    trimmed.find('(') != std::string::npos;
                if (!declaration_pending && !starts_candidate) {
                    continue;
                }

                declaration_pending = true;
                if (!declaration.empty()) {
                    declaration.push_back(' ');
                }
                declaration += trimmed;

                if (trimmed.find(';') == std::string::npos && trimmed.find('{') == std::string::npos) {
                    continue;
                }

                if (auto callable_name = extract_callable_name_from_declaration(declaration);
                    callable_name.has_value() && seen.insert(*callable_name).second) {
                    names.push_back(*callable_name);
                }
                declaration_pending = false;
                declaration.clear();
            }

            return names;
        }

        std::string sanitize_source_file(const fs::path& file_path) {
            std::ifstream input(file_path);
            if (!input) {
                return {};
            }

            std::ostringstream buffer;
            bool in_block_comment = false;
            std::string line;
            while (std::getline(input, line)) {
                buffer << strip_comments_and_strings(line, in_block_comment) << '\n';
            }
            return buffer.str();
        }

        IdentifierSet collect_identifier_tokens(const std::string& sanitized_text) {
            IdentifierSet tokens;
            std::string current;
            current.reserve(32);

            auto flush = [&]() {
                if (!current.empty()) {
                    tokens.insert(current);
                    current.clear();
                }
            };

            for (const char ch : sanitized_text) {
                if (is_identifier_char(ch)) {
                    current.push_back(ch);
                    continue;
                }
                flush();
            }
            flush();
            return tokens;
        }

        bool references_any_identifier(
            const std::string& sanitized_text,
            const std::vector<std::string>& identifiers
        ) {
            for (const auto& identifier : identifiers) {
                if (contains_identifier_token(sanitized_text, identifier)) {
                    return true;
                }
            }
            return false;
        }

        std::string derive_forward_include_name(
            const IncludeDirective& include,
            const fs::path& target_header,
            const std::string& fwd_header_name
        ) {
            fs::path include_path(include.header_name);
            if (include_path.filename() == target_header.filename()) {
                include_path.replace_filename(fwd_header_name);
                return include_path.generic_string();
            }
            return fwd_header_name;
        }

        std::optional<ForwardReplacementOpportunity> analyze_forward_replacement_opportunity(
            const fs::path& includer_path,
            const fs::path& target_header,
            const std::vector<ForwardDeclSymbol>& symbols,
            const std::vector<std::string>& callable_names,
            const std::string& fwd_header_name,
            std::unordered_map<std::string, std::string>& sanitized_source_cache,
            std::unordered_map<std::string, IdentifierSet>& identifier_token_cache
        ) {
            auto include_directive = find_include_for_header(
                includer_path,
                target_header.generic_string()
            );
            if (!include_directive.has_value()) {
                include_directive = find_include_for_header(
                    includer_path,
                    target_header.filename().string()
                );
            }
            if (!include_directive.has_value()) {
                return std::nullopt;
            }

            const std::string includer_key = includer_path.generic_string();
            auto sanitized_it = sanitized_source_cache.find(includer_key);
            if (sanitized_it == sanitized_source_cache.end()) {
                const std::string sanitized = sanitize_source_file(includer_path);
                sanitized_it = sanitized_source_cache.emplace(includer_key, sanitized).first;
            }
            const std::string& sanitized_text = sanitized_it->second;
            if (sanitized_text.empty()) {
                return std::nullopt;
            }

            if (references_any_identifier(sanitized_text, callable_names)) {
                return std::nullopt;
            }

            auto token_it = identifier_token_cache.find(includer_key);
            if (token_it == identifier_token_cache.end()) {
                token_it = identifier_token_cache.emplace(
                    includer_key,
                    collect_identifier_tokens(sanitized_text)
                ).first;
            }
            const IdentifierSet& identifiers = token_it->second;

            std::vector<std::string> mentioned_symbols;
            std::unordered_set<std::string> seen_symbols;
            for (const auto& symbol : symbols) {
                if (!seen_symbols.insert(symbol.name).second) {
                    continue;
                }
                if (!identifiers.contains(symbol.name)) {
                    continue;
                }
                mentioned_symbols.push_back(symbol.name);
            }

            if (mentioned_symbols.empty()) {
                return std::nullopt;
            }

            const auto usage = analyze_incomplete_type_usage(sanitized_text, mentioned_symbols);
            if (usage.requires_complete_type ||
                !usage.has_mentions ||
                usage.pointer_or_reference_mentions == 0) {
                return std::nullopt;
            }

            ForwardReplacementOpportunity opportunity;
            opportunity.includer_path = includer_path;
            opportunity.include_directive = *include_directive;
            opportunity.replacement_include = derive_forward_include_name(
                *include_directive,
                target_header,
                fwd_header_name
            );
            opportunity.pointer_or_reference_mentions = usage.pointer_or_reference_mentions;
            opportunity.referenced_symbols = std::move(mentioned_symbols);
            return opportunity;
        }

        std::vector<ForwardReplacementOpportunity> collect_forward_replacement_opportunities(
            const analyzers::DependencyAnalysisResult::HeaderInfo& header,
            const fs::path& resolved_header_path,
            const fs::path& project_root,
            const std::vector<ForwardDeclSymbol>& symbols,
            const std::vector<std::string>& callable_names,
            const std::string& fwd_header_name
        ) {
            constexpr std::size_t kMaxIncludersToInspect = 96;
            constexpr std::size_t kMaxForwardReplacementOpportunities = 24;

            std::vector<ForwardReplacementOpportunity> opportunities;
            std::unordered_set<std::string> seen_includers;
            std::unordered_map<std::string, std::string> sanitized_source_cache;
            std::unordered_map<std::string, IdentifierSet> identifier_token_cache;
            opportunities.reserve(header.included_by.size());
            std::size_t inspected_includers = 0;

            for (const auto& includer : header.included_by) {
                if (inspected_includers >= kMaxIncludersToInspect ||
                    opportunities.size() >= kMaxForwardReplacementOpportunities) {
                    break;
                }
                auto resolved_includer_path = resolve_header_path_for_edits(includer, project_root);
                if (!resolved_includer_path.has_value() || !fs::exists(*resolved_includer_path)) {
                    continue;
                }
                if (*resolved_includer_path == resolved_header_path) {
                    continue;
                }
                if (!seen_includers.insert(resolved_includer_path->generic_string()).second) {
                    continue;
                }
                ++inspected_includers;

                auto opportunity = analyze_forward_replacement_opportunity(
                    *resolved_includer_path,
                    resolved_header_path,
                    symbols,
                    callable_names,
                    fwd_header_name,
                    sanitized_source_cache,
                    identifier_token_cache
                );
                if (!opportunity.has_value()) {
                    continue;
                }
                opportunities.push_back(std::move(*opportunity));
            }

            std::ranges::sort(opportunities, [](const ForwardReplacementOpportunity& lhs,
                                                const ForwardReplacementOpportunity& rhs) {
                if (lhs.pointer_or_reference_mentions != rhs.pointer_or_reference_mentions) {
                    return lhs.pointer_or_reference_mentions > rhs.pointer_or_reference_mentions;
                }
                return lhs.includer_path.generic_string() < rhs.includer_path.generic_string();
            });
            return opportunities;
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

            if (!is_header_file_path(header.path)) {
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
            std::vector<ForwardDeclSymbol> forward_decl_symbols;
            std::vector<std::string> callable_names;
            std::vector<ForwardReplacementOpportunity> replacement_opportunities;
            if (pattern == SplitPattern::ForwardDecl && resolved_header_path.has_value()) {
                forward_decl_symbols = extract_forward_decl_symbols(*resolved_header_path);
                if (forward_decl_symbols.empty()) {
                    ++skipped;
                    continue;
                }
                callable_names = extract_declared_callable_names(*resolved_header_path);
                replacement_opportunities = collect_forward_replacement_opportunities(
                    header,
                    *resolved_header_path,
                    context.project_root,
                    forward_decl_symbols,
                    callable_names,
                    fwd_header_name
                );
                if (replacement_opportunities.empty()) {
                    ++skipped;
                    continue;
                }
            }

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
                    desc << "- Replace proven includers that only need incomplete types with `" << fwd_header_name
                         << "` (" << replacement_opportunities.size() << " files).\n";
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
                          << "header (_fwd.h). BHA only emits this when it can prove "
                          << replacement_opportunities.size()
                          << " includer file(s) use only incomplete-type-safe references.";
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

            if (pattern == SplitPattern::ForwardDecl && !replacement_opportunities.empty()) {
                suggestion.impact.total_files_affected = replacement_opportunities.size() + 1;
            } else {
                suggestion.impact.total_files_affected = header.including_files;
            }
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
                pattern == SplitPattern::ForwardDecl &&
                resolved_header_path.has_value() &&
                !replacement_opportunities.empty();
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
                    const auto insertion = make_preferred_include_insertion_edit(
                        *resolved_header_path,
                        include_line
                    );
                    suggestion.edits.push_back(insertion.edit);
                }

                for (const auto line : collect_redundant_forward_decl_lines(
                         *resolved_header_path,
                         forward_decl_symbols
                     )) {
                    suggestion.edits.push_back(make_delete_line_edit(*resolved_header_path, line));
                }

                std::unordered_set<std::string> edited_includers;
                for (const auto& opportunity : replacement_opportunities) {
                    const std::string replacement_line =
                        "#include \"" + opportunity.replacement_include + "\"";

                    TextEdit replace_include;
                    replace_include.file = opportunity.includer_path;
                    replace_include.start_line = opportunity.include_directive.line;
                    replace_include.start_col = opportunity.include_directive.col_start;
                    replace_include.end_line = opportunity.include_directive.line;
                    replace_include.end_col = opportunity.include_directive.col_end;
                    replace_include.new_text = replacement_line;
                    suggestion.edits.push_back(std::move(replace_include));

                    if (edited_includers.insert(opportunity.includer_path.generic_string()).second) {
                        FileTarget includer_target;
                        includer_target.path = opportunity.includer_path;
                        includer_target.action = FileAction::Modify;
                        includer_target.note =
                            "Replace full header include with proven forward companion include";
                        suggestion.secondary_files.push_back(std::move(includer_target));
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
