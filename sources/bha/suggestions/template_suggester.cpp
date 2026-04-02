//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/template_suggester.hpp"
#include "bha/utils/path_utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {
        namespace fs = std::filesystem;

        struct CMakeTargetInfo {
            std::string name;
            std::size_t line = 0;
            std::size_t end_line = 0;
        };

        struct MesonTargetSpan {
            std::size_t start_line = 0;
            std::size_t end_line = 0;
            bool single_line = false;
        };

        struct TemplateDeclInfo {
            fs::path header_path;
            std::string class_key = "class";
            bool is_function_template = false;
        };

        struct TemplateHeaderCandidate {
            fs::path declaration_header;
            fs::path insertion_header;
            std::string class_key = "class";
            bool is_function_template = false;
        };

        std::optional<CMakeTargetInfo> find_first_cmake_target(const std::string& content) {
            const std::regex target_regex(
                R"(^\s*add_(executable|library)\s*\(\s*([A-Za-z0-9_\-\.]+))",
                std::regex::icase
            );

            std::vector<std::string> lines;
            lines.reserve(128);
            std::istringstream input(content);
            std::string line;
            while (std::getline(input, line)) {
                lines.push_back(line);
            }

            for (std::size_t line_num = 0; line_num < lines.size(); ++line_num) {
                const std::string& target_line = lines[line_num];
                std::smatch match;
                if (!std::regex_search(target_line, match, target_regex) || match.size() < 3) {
                    continue;
                }

                int depth = 0;
                bool started = false;
                std::size_t end_line = line_num;
                for (std::size_t i = line_num; i < lines.size(); ++i) {
                    for (const char c : lines[i]) {
                        if (c == '(') {
                            ++depth;
                            started = true;
                        } else if (c == ')' && started) {
                            --depth;
                        }
                    }
                    if (started && depth <= 0) {
                        end_line = i;
                        break;
                    }
                }

                return CMakeTargetInfo{match[2].str(), line_num, end_line};
            }

            return std::nullopt;
        }

        std::optional<MesonTargetSpan> find_first_meson_target(const std::string& content) {
            const std::regex target_regex(
                R"(^\s*(executable|library|shared_library|static_library)\s*\()",
                std::regex::icase
            );

            std::vector<std::string> lines;
            lines.reserve(128);
            std::istringstream input(content);
            std::string line;
            while (std::getline(input, line)) {
                lines.push_back(line);
            }

            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (!std::regex_search(lines[i], target_regex)) {
                    continue;
                }

                const std::size_t open_pos = lines[i].find('(');
                if (open_pos == std::string::npos) {
                    continue;
                }

                int paren_depth = 0;
                for (std::size_t j = open_pos; j < lines[i].size(); ++j) {
                    if (lines[i][j] == '(') {
                        ++paren_depth;
                    }
                    if (lines[i][j] == ')') {
                        --paren_depth;
                    }
                }

                MesonTargetSpan span;
                span.start_line = i;

                if (paren_depth <= 0) {
                    span.end_line = i;
                    span.single_line = true;
                } else {
                    for (std::size_t k = i + 1; k < lines.size(); ++k) {
                        for (const char c : lines[k]) {
                            if (c == '(') {
                                ++paren_depth;
                            }
                            if (c == ')') {
                                --paren_depth;
                            }
                        }
                        if (paren_depth <= 0) {
                            span.end_line = k;
                            break;
                        }
                    }
                }

                return span;
            }

            return std::nullopt;
        }

        fs::path find_project_root_for_templates(const fs::path& path) {
            fs::path current = path;
            if ((fs::exists(current) && fs::is_regular_file(current)) ||
                (!fs::exists(current) && current.has_parent_path())) {
                current = current.parent_path();
            }

            fs::path candidate;
            while (!current.empty() && current.has_parent_path() && current != current.parent_path()) {
                if (fs::exists(current / "CMakeLists.txt") || fs::exists(current / "meson.build")) {
                    return current;
                }
                if (candidate.empty() &&
                    (fs::exists(current / "Makefile") || fs::exists(current / "makefile") || fs::exists(current / "GNUmakefile"))) {
                    candidate = current;
                }
                current = current.parent_path();
            }

            return candidate;
        }

        std::size_t count_lines_until(const std::string& content, std::size_t pos) {
            std::size_t count = 0;
            const std::size_t end = std::min(pos, content.size());
            for (std::size_t i = 0; i < end; ++i) {
                if (content[i] == '\n') {
                    ++count;
                }
            }
            return count;
        }

        bool is_source_file(const fs::path& path) {
            static constexpr std::array<std::string_view, 8> kSourceExts = {
                ".cpp", ".cc", ".cxx", ".c++", ".C", ".cp", ".mm", ".m"
            };
            return path_has_extension(path, kSourceExts);
        }

        std::optional<fs::path> resolve_existing_instantiation_source(
            const analyzers::TemplateAnalysisResult::TemplateStats& tmpl,
            const fs::path& project_root
        ) {
            for (const auto& file : tmpl.files_using) {
                fs::path source_path = resolve_source_path(file).lexically_normal();
                if (!fs::exists(source_path)) {
                    const fs::path fallback_name = fs::path(file).filename();
                    if (auto fallback = find_file_in_repo(project_root, fallback_name); fallback.has_value()) {
                        source_path = fallback->lexically_normal();
                    }
                }
                if (fs::exists(source_path) && is_source_file(source_path)) {
                    return source_path;
                }
            }
            return std::nullopt;
        }

        std::string include_path_for_source(const fs::path& source_file, const fs::path& header_path) {
            std::error_code ec;
            if (auto rel = fs::relative(header_path, source_file.parent_path(), ec); !ec) {
                return rel.generic_string();
            }
            return header_path.filename().generic_string();
        }

        Priority calculate_priority(const analyzers::TemplateAnalysisResult::TemplateStats& tmpl,
                                    const Duration total_build_time) {
            const auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                tmpl.total_time
            ).count();

            double time_ratio = 0.0;
            if (total_build_time.count() > 0) {
                time_ratio = static_cast<double>(tmpl.total_time.count()) /
                             static_cast<double>(total_build_time.count());
            }

            if (time_ms > 5000 && tmpl.instantiation_count >= 50) {
                return Priority::Critical;
            }
            if (time_ms > 1000 && tmpl.instantiation_count >= 20) {
                return Priority::High;
            }
            if (time_ratio > 0.01) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        std::string generate_extern_template(const std::string& class_key, const std::string& name) {
            return "extern template " + class_key + " " + name + ";";
        }

        std::string generate_explicit_instantiation(const std::string& class_key, const std::string& name) {
            return "template " + class_key + " " + name + ";";
        }

        std::string generate_extern_function_instantiation(const std::string& signature) {
            return "extern template " + signature + ";";
        }

        std::string generate_explicit_function_instantiation(const std::string& signature) {
            return "template " + signature + ";";
        }

        std::string strip_leading_keywords(std::string name) {
            constexpr std::string_view class_kw = "class ";
            constexpr std::string_view struct_kw = "struct ";
            if (name.rfind(class_kw, 0) == 0) {
                name.erase(0, class_kw.size());
            } else if (name.rfind(struct_kw, 0) == 0) {
                name.erase(0, struct_kw.size());
            }
            return name;
        }

        std::string base_template_name(const std::string& name) {
            std::string base = strip_leading_keywords(name);
            if (auto angle = base.find('<'); angle != std::string::npos) {
                base = base.substr(0, angle);
            }
            if (auto last_colon = base.rfind("::"); last_colon != std::string::npos) {
                base = base.substr(last_colon + 2);
            }
            return base;
        }

        std::string base_function_name(const std::string& signature) {
            if (signature.empty()) {
                return {};
            }
            std::string base = signature;
            if (const auto angle = base.find('<'); angle != std::string::npos) {
                base = base.substr(0, angle);
            }
            if (const auto paren = base.find('('); paren != std::string::npos) {
                base = base.substr(0, paren);
            }
            if (const auto last_colon = base.rfind("::"); last_colon != std::string::npos) {
                base = base.substr(last_colon + 2);
            }
            const auto last_space = base.find_last_of(" \t");
            if (last_space != std::string::npos) {
                base = base.substr(last_space + 1);
            }
            return base;
        }

        std::size_t count_top_level_template_args(const std::string& signature) {
            const auto open = signature.find('<');
            if (open == std::string::npos) {
                return 0;
            }
            std::size_t depth = 0;
            std::size_t commas = 0;
            for (std::size_t i = open; i < signature.size(); ++i) {
                const char ch = signature[i];
                if (ch == '<') {
                    ++depth;
                    continue;
                }
                if (ch == '>') {
                    if (depth > 0) {
                        --depth;
                        if (depth == 0) {
                            return commas + 1;
                        }
                    }
                    continue;
                }
                if (ch == ',' && depth == 1) {
                    ++commas;
                }
            }
            return 0;
        }

        std::string regex_escape(const std::string& input) {
            std::string escaped;
            escaped.reserve(input.size() * 2);
            for (const char ch : input) {
                switch (ch) {
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
                    case '\\':
                        escaped.push_back('\\');
                        break;
                    default:
                        break;
                }
                escaped.push_back(ch);
            }
            return escaped;
        }

        bool is_blacklisted_template(const std::string& name) {
            std::string trimmed = strip_leading_keywords(name);
            if (trimmed.rfind("::", 0) == 0) {
                trimmed.erase(0, 2);
            }
            if (trimmed.rfind("std::", 0) == 0 ||
                trimmed.rfind("std::__", 0) == 0 ||
                trimmed.rfind("__gnu_cxx::", 0) == 0 ||
                trimmed.rfind("testing::", 0) == 0 ||
                trimmed.rfind("__", 0) == 0) {
                return true;
            }
            const std::string base = base_template_name(trimmed);
            return base.rfind("__", 0) == 0;
        }

        bool has_unspellable_instantiation_component(const std::string& name) {
            static constexpr std::array<std::string_view, 8> markers{
                "(lambda at ",
                "{lambda(",
                "<lambda",
                "(anonymous namespace)",
                "{anonymous}",
                "(unnamed ",
                "(anonymous ",
                "<unnamed "
            };
            return std::ranges::any_of(markers, [&](const std::string_view marker) {
                return name.find(marker) != std::string::npos;
            });
        }

        Suggestion make_unspellable_template_advisory(
            const analyzers::TemplateAnalysisResult::TemplateStats& tmpl,
            const std::string& template_name,
            const fs::path& target_path,
            const BuildTrace& trace
        ) {
            Suggestion suggestion;
            const std::uint64_t signature_hash = std::hash<std::string>{}(template_name);
            std::ostringstream id_suffix;
            id_suffix << "unspellable-" << std::hex << signature_hash;
            suggestion.id = generate_suggestion_id("template", target_path, id_suffix.str());
            suggestion.type = SuggestionType::ExplicitTemplate;
            suggestion.priority = calculate_priority(tmpl, trace.total_time);
            suggestion.confidence = 0.6;
            suggestion.title = "Refactor lambda-based template argument into a named callable";
            suggestion.description =
                "This expensive template instantiation uses a lambda closure type as a template argument. "
                "Closure types are unnamed, so BHA cannot generate a valid extern/explicit template instantiation for it.";
            suggestion.rationale =
                "Explicit instantiation requires a spellable specialization name. Lambda closure types are unnamed, "
                "so the callable must first be extracted into a named function object or another stable type.";
            suggestion.target_file.path = target_path;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Refactor the lambda into a named callable before explicit instantiation";
            suggestion.application_mode = SuggestionApplicationMode::Advisory;
            suggestion.application_summary = "Manual refactor required before instantiation";
            suggestion.application_guidance =
                "Extract the lambda into a named functor or inline constexpr function object in a header, replace the lambda-based template argument, "
                "then rerun BHA to generate explicit-instantiation edits for the now-spellable specialization.";
            suggestion.auto_apply_blocked_reason =
                "Template argument list contains an unnamed lambda closure type, which cannot be emitted as a valid explicit instantiation declaration.";
            suggestion.caveats = {
                "Requires a source refactor, not just a generated extern template declaration",
                "The replacement callable type must be visible in every translation unit that uses the specialization",
                "After refactoring, rerun analysis so BHA can propose explicit-instantiation edits for the named specialization"
            };
            suggestion.estimated_savings = tmpl.total_time * (tmpl.instantiation_count - 1) /
                                           tmpl.instantiation_count;
            if (trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(trace.total_time.count());
            }
            suggestion.impact.total_files_affected = tmpl.files_using.size();
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;
            suggestion.verification =
                "After replacing the lambda with a named callable, rerun BHA and verify that an explicit-instantiation suggestion is emitted for the specialization.";
            suggestion.is_safe = false;
            return suggestion;
        }

        std::optional<std::size_t> find_top_level_paren(const std::string& text) {
            std::size_t angle_depth = 0;
            for (std::size_t i = 0; i < text.size(); ++i) {
                const char ch = text[i];
                if (ch == '<') {
                    ++angle_depth;
                    continue;
                }
                if (ch == '>') {
                    if (angle_depth > 0) {
                        --angle_depth;
                    }
                    continue;
                }
                if (ch == '(' && angle_depth == 0) {
                    return i;
                }
            }
            return std::nullopt;
        }

        bool looks_like_function_template(const std::string& name) {
            return find_top_level_paren(name).has_value();
        }

        bool header_declares_function_template(
            const fs::path& header_path,
            const std::string& function_name
        ) {
            auto lines_result = file_utils::read_lines(header_path);
            if (lines_result.is_err()) {
                return false;
            }

            int template_window = 0;
            for (const auto& line : lines_result.value()) {
                const auto start = line.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) {
                    if (template_window > 0) {
                        --template_window;
                    }
                    continue;
                }
                const std::string trimmed = line.substr(start);
                if (trimmed.rfind("template", 0) == 0) {
                    template_window = 8;
                    continue;
                }
                if (template_window > 0 &&
                    trimmed.find(function_name) != std::string::npos &&
                    trimmed.find('(') != std::string::npos) {
                    return true;
                }
                if (template_window > 0) {
                    --template_window;
                }
            }
            return false;
        }

        std::optional<std::string> header_template_kind(
            const fs::path& header_path,
            const std::string& base_name
        ) {
            std::ifstream in(header_path);
            if (!in) {
                return std::nullopt;
            }
            const std::string content((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            const std::string escaped_name = regex_escape(base_name);
            const std::regex tmpl_regex(
                "\\btemplate\\s*<[^>]*>\\s*(class|struct)\\s+" + escaped_name + "\\b",
                std::regex::icase
            );
            std::smatch match;
            if (!std::regex_search(content, match, tmpl_regex) || match.size() < 2) {
                return std::nullopt;
            }
            std::string kind = match[1].str();
            if (kind != "struct" && kind != "class") {
                kind = "class";
            }
            return kind;
        }

        std::optional<fs::path> resolve_template_header(const analyzers::TemplateAnalysisResult::TemplateInfo& tmpl) {
            for (const auto& loc : tmpl.locations) {
                if (!loc.has_location()) {
                    continue;
                }
                fs::path path = resolve_source_path(loc.file);
                if (!fs::exists(path)) {
                    continue;
                }
                const auto ext = path.extension().string();
                if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
                    return path;
                }
                for (const auto& candidate_ext : {".hpp", ".h", ".hh", ".hxx"}) {
                    fs::path candidate = path;
                    candidate.replace_extension(candidate_ext);
                    if (fs::exists(candidate)) {
                        return candidate;
                    }
                }
            }
            return std::nullopt;
        }

        std::optional<fs::path> resolve_include_path_for_source(
            const fs::path& source_file,
            const std::string& include_name,
            const fs::path& project_root
        ) {
            const fs::path include_path(include_name);
            if (include_path.is_absolute()) {
                if (fs::exists(include_path)) {
                    return include_path.lexically_normal();
                }
                return std::nullopt;
            }

            const std::array candidates{
                (source_file.parent_path() / include_path).lexically_normal(),
                (project_root / include_path).lexically_normal(),
                (project_root / "include" / include_path).lexically_normal(),
                (project_root / "src" / include_path).lexically_normal()
            };
            for (const auto& candidate : candidates) {
                if (!candidate.empty() && fs::exists(candidate)) {
                    return candidate;
                }
            }
            return std::nullopt;
        }

        std::optional<TemplateDeclInfo> resolve_template_declaration(
            const analyzers::TemplateAnalysisResult::TemplateInfo& tmpl,
            const fs::path& project_root,
            const std::string& base_name,
            const bool is_function_template
        ) {
            if (auto header_path = resolve_template_header(tmpl); header_path.has_value()) {
                if (is_function_template) {
                    if (header_declares_function_template(*header_path, base_name)) {
                        return TemplateDeclInfo{*header_path, "class", true};
                    }
                } else if (auto class_key = header_template_kind(*header_path, base_name); class_key.has_value()) {
                    return TemplateDeclInfo{*header_path, *class_key, false};
                }
            }

            std::unordered_set<std::string> seen_headers;
            for (const auto& file : tmpl.files_using) {
                fs::path source_path = resolve_source_path(file).lexically_normal();
                if (!fs::exists(source_path)) {
                    const fs::path fallback_name = fs::path(file).filename();
                    if (auto fallback = find_file_in_repo(project_root, fallback_name); fallback.has_value()) {
                        source_path = fallback->lexically_normal();
                    }
                }
                if (!fs::exists(source_path)) {
                    continue;
                }
                for (const auto& include_dir : find_include_directives(source_path)) {
                    if (include_dir.is_system) {
                        continue;
                    }
                    const auto include_path =
                        resolve_include_path_for_source(source_path, include_dir.header_name, project_root);
                    if (!include_path.has_value()) {
                        continue;
                    }
                    const std::string key = include_path->generic_string();
                    if (!seen_headers.insert(key).second) {
                        continue;
                    }
                    if (is_function_template) {
                        if (header_declares_function_template(*include_path, base_name)) {
                            return TemplateDeclInfo{*include_path, "class", true};
                        }
                    } else if (auto class_key = header_template_kind(*include_path, base_name); class_key.has_value()) {
                        return TemplateDeclInfo{*include_path, *class_key, false};
                    }
                }
            }

            return std::nullopt;
        }

        std::vector<std::string> extract_qualified_names(const std::string& text) {
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

        std::vector<fs::path> collect_header_closure(const fs::path& header_path, const fs::path& project_root) {
            std::vector<fs::path> closure;
            std::vector<fs::path> stack{header_path.lexically_normal()};
            std::unordered_set<std::string> visited;

            while (!stack.empty()) {
                fs::path current = std::move(stack.back());
                stack.pop_back();
                current = current.lexically_normal();

                if (!fs::exists(current)) {
                    continue;
                }

                const std::string key = current.generic_string();
                if (!visited.insert(key).second) {
                    continue;
                }

                closure.push_back(current);
                for (const auto& include_dir : find_include_directives(current)) {
                    if (include_dir.is_system) {
                        continue;
                    }
                    if (auto include_path =
                            resolve_include_path_for_source(current, include_dir.header_name, project_root);
                        include_path.has_value()) {
                        stack.push_back(include_path->lexically_normal());
                    }
                }
            }

            return closure;
        }

        bool header_closure_exposes_template(
            const fs::path& header_path,
            const fs::path& project_root,
            const std::string& base_name,
            const bool is_function_template
        ) {
            for (const auto& candidate : collect_header_closure(header_path, project_root)) {
                if (is_function_template) {
                    if (header_declares_function_template(candidate, base_name)) {
                        return true;
                    }
                    continue;
                }

                if (header_template_kind(candidate, base_name).has_value()) {
                    return true;
                }
            }

            return false;
        }

        bool header_closure_exposes_symbol(
            const fs::path& header_path,
            const fs::path& project_root,
            const std::string& qualified_name
        ) {
            const auto last_scope = qualified_name.rfind("::");
            const std::string terminal_name =
                last_scope == std::string::npos ? qualified_name : qualified_name.substr(last_scope + 2);
            const std::regex exact_regex("\\b" + regex_escape(qualified_name) + "\\b");
            const std::regex terminal_regex("\\b" + regex_escape(terminal_name) + "\\b");

            for (const auto& candidate : collect_header_closure(header_path, project_root)) {
                std::ifstream in(candidate);
                if (!in) {
                    continue;
                }
                const std::string content((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
                if (std::regex_search(content, exact_regex) || std::regex_search(content, terminal_regex)) {
                    return true;
                }
            }

            return false;
        }

        std::optional<TemplateHeaderCandidate> select_template_header_candidate(
            const analyzers::TemplateAnalysisResult::TemplateInfo& tmpl,
            const fs::path& project_root,
            const std::string& normalized_template_name,
            const std::string& base_name,
            const bool is_function_template
        ) {
            const auto declaration = resolve_template_declaration(
                tmpl,
                project_root,
                base_name,
                is_function_template
            );
            if (!declaration.has_value()) {
                return std::nullopt;
            }

            std::vector<fs::path> candidates{declaration->header_path.lexically_normal()};
            std::unordered_set<std::string> seen{declaration->header_path.lexically_normal().generic_string()};

            for (const auto& file : tmpl.files_using) {
                fs::path source_path = resolve_source_path(file).lexically_normal();
                if (!fs::exists(source_path)) {
                    const fs::path fallback_name = fs::path(file).filename();
                    if (auto fallback = find_file_in_repo(project_root, fallback_name); fallback.has_value()) {
                        source_path = fallback->lexically_normal();
                    }
                }
                if (!fs::exists(source_path)) {
                    continue;
                }

                for (const auto& include_dir : find_include_directives(source_path)) {
                    if (include_dir.is_system) {
                        continue;
                    }

                    const auto include_path =
                        resolve_include_path_for_source(source_path, include_dir.header_name, project_root);
                    if (!include_path.has_value()) {
                        continue;
                    }

                    const fs::path normalized = include_path->lexically_normal();
                    if (seen.insert(normalized.generic_string()).second) {
                        candidates.push_back(normalized);
                    }
                }
            }

            const std::string primary_template_name = strip_leading_keywords(normalized_template_name).substr(
                0,
                strip_leading_keywords(normalized_template_name).find('<')
            );
            const std::string extern_declaration = is_function_template
                ? generate_extern_function_instantiation(normalized_template_name)
                : generate_extern_template(declaration->class_key, normalized_template_name);
            auto required_symbols = extract_qualified_names(extern_declaration);
            std::erase(required_symbols, primary_template_name);

            for (const auto& candidate : candidates) {
                if (!header_closure_exposes_template(candidate, project_root, base_name, is_function_template)) {
                    continue;
                }

                bool dependencies_visible = true;
                for (const auto& required_symbol : required_symbols) {
                    if (!header_closure_exposes_symbol(candidate, project_root, required_symbol)) {
                        dependencies_visible = false;
                        break;
                    }
                }

                if (dependencies_visible) {
                    return TemplateHeaderCandidate{
                        declaration->header_path,
                        candidate,
                        declaration->class_key,
                        declaration->is_function_template
                    };
                }
            }

            return TemplateHeaderCandidate{
                declaration->header_path,
                fs::path{},
                declaration->class_key,
                declaration->is_function_template
            };
        }

        Suggestion make_context_visibility_advisory(
            const analyzers::TemplateAnalysisResult::TemplateStats& tmpl,
            const std::string& template_name,
            const fs::path& declaration_header,
            const BuildTrace& trace
        ) {
            Suggestion suggestion;
            const std::uint64_t signature_hash = std::hash<std::string>{}(template_name);
            std::ostringstream id_suffix;
            id_suffix << "visibility-" << std::hex << signature_hash;
            suggestion.id = generate_suggestion_id("template", declaration_header, id_suffix.str());
            suggestion.type = SuggestionType::ExplicitTemplate;
            suggestion.priority = calculate_priority(tmpl, trace.total_time);
            suggestion.confidence = 0.6;
            suggestion.title = "Place explicit instantiation where dependent types are visible";
            suggestion.description =
                "This template specialization references additional types that are not visible from the template declaration header. "
                "BHA cannot safely generate auto-apply edits for extern/explicit instantiation without choosing a compile-valid header context.";
            suggestion.rationale =
                "Extern template declarations and the explicit-instantiation translation unit must both see the primary template and all referenced dependent types. "
                "If the declaration header does not expose those types, generated edits can fail to compile.";
            suggestion.target_file.path = declaration_header;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Choose a header context that already includes the dependent types";
            suggestion.application_mode = SuggestionApplicationMode::Advisory;
            suggestion.application_summary = "Manual placement required";
            suggestion.application_guidance =
                "Move the extern template declaration to a header that already sees all dependent types, or add the required declarations/includes if that is architecturally safe. "
                "Then place the explicit instantiation in a .cpp that includes that same header and rerun BHA.";
            suggestion.auto_apply_blocked_reason =
                "No safe destination header exposes both the template declaration and all dependent types referenced by the specialization.";
            suggestion.caveats = {
                "Adding includes to the declaration header may create layering or cycle issues",
                "The explicit-instantiation .cpp must include a header where every dependent type is visible",
                "Rerun BHA after moving the declaration or introducing a safe umbrella header"
            };
            suggestion.estimated_savings = tmpl.total_time * (tmpl.instantiation_count - 1) /
                                           tmpl.instantiation_count;
            if (trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(trace.total_time.count());
            }
            suggestion.impact.total_files_affected = tmpl.files_using.size();
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;
            suggestion.verification =
                "After moving the declaration to a compile-valid header context, rerun BHA and verify that the generated explicit-instantiation edits build cleanly.";
            suggestion.is_safe = false;
            return suggestion;
        }

        std::size_t find_extern_template_insertion_line(const fs::path& header_path) {
            auto lines_result = file_utils::read_lines(header_path);
            if (lines_result.is_err()) {
                return 0;
            }
            const auto& lines = lines_result.value();
            for (std::size_t idx = lines.size(); idx > 0; --idx) {
                const std::string& line = lines[idx - 1];
                const auto first = line.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) {
                    continue;
                }
                const std::string trimmed = line.substr(first);
                if (!trimmed.empty() && trimmed.front() == '}') {
                    return idx - 1;
                }
            }
            return lines.size();
        }

        struct TemplateRenderInfo {
            std::string template_name;
            std::string normalized_template_name;
            std::string base_name;
            std::string short_name;
            std::string class_key = "class";
            bool is_function_template = false;
            std::string instantiation_line;
            std::string extern_line;
        };

        std::string make_short_template_name(const std::string& template_name) {
            std::string short_name = template_name;
            if (const auto angle_pos = template_name.find('<'); angle_pos != std::string::npos) {
                if (const auto last_colon = template_name.rfind("::", angle_pos); last_colon != std::string::npos) {
                    short_name = template_name.substr(last_colon + 2, angle_pos - last_colon - 2);
                } else {
                    short_name = template_name.substr(0, angle_pos);
                }
            }
            return short_name;
        }

        TemplateRenderInfo make_template_render_info(
            const std::string& template_name,
            const std::string& normalized_template_name,
            const std::string& base_name,
            const std::string& class_key,
            const bool is_function_template
        ) {
            TemplateRenderInfo render;
            render.template_name = template_name;
            render.normalized_template_name = normalized_template_name;
            render.base_name = base_name;
            render.short_name = make_short_template_name(template_name);
            render.class_key = class_key;
            render.is_function_template = is_function_template;
            render.instantiation_line = is_function_template
                ? generate_explicit_function_instantiation(normalized_template_name)
                : generate_explicit_instantiation(class_key, normalized_template_name);
            render.extern_line = is_function_template
                ? generate_extern_function_instantiation(normalized_template_name)
                : generate_extern_template(class_key, normalized_template_name);
            return render;
        }

        fs::path resolve_project_root_dir(
            const SuggestionContext& context,
            const analyzers::TemplateAnalysisResult::TemplateStats& tmpl
        ) {
            fs::path project_root_dir = context.project_root;
            if (!project_root_dir.empty() && project_root_dir.is_relative()) {
                project_root_dir = fs::absolute(project_root_dir);
            }
            if (!project_root_dir.empty() && !tmpl.files_using.empty()) {
                const fs::path derived_root = find_repository_root(resolve_source_path(tmpl.files_using[0]));
                if (!derived_root.empty() &&
                    path_utils::is_under(derived_root, project_root_dir) &&
                    derived_root != project_root_dir) {
                    project_root_dir = derived_root;
                }
            }
            if (project_root_dir.empty() && !tmpl.files_using.empty()) {
                project_root_dir = resolve_source_path(tmpl.files_using[0]).parent_path();
                while (project_root_dir.has_parent_path() &&
                       !fs::exists(project_root_dir / "CMakeLists.txt") &&
                       !fs::exists(project_root_dir / "meson.build")) {
                    project_root_dir = project_root_dir.parent_path();
                }
            }
            if (project_root_dir.empty() && !context.trace.units.empty()) {
                const fs::path source_path = context.trace.units.front().source_file;
                if (!source_path.empty()) {
                    project_root_dir = find_project_root_for_templates(source_path);
                    if (project_root_dir.empty()) {
                        project_root_dir = find_repository_root(source_path);
                    }
                    if (project_root_dir.empty()) {
                        project_root_dir = source_path.parent_path();
                    }
                }
            }
            return project_root_dir;
        }

        void populate_template_suggestion_metadata(
            Suggestion& suggestion,
            const analyzers::TemplateAnalysisResult::TemplateStats& tmpl,
            const TemplateRenderInfo& render,
            const BuildTrace& trace,
            const fs::path& header_path
        ) {
            const std::uint64_t signature_hash = std::hash<std::string>{}(render.normalized_template_name);
            std::ostringstream id_suffix;
            id_suffix << render.base_name << "-" << std::hex << signature_hash;
            suggestion.id = generate_suggestion_id("template", header_path, id_suffix.str());
            suggestion.type = SuggestionType::ExplicitTemplate;
            suggestion.priority = calculate_priority(tmpl, trace.total_time);
            suggestion.confidence = 0.7;
            suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
            suggestion.application_summary = "Create explicit-instantiation edits";
            suggestion.title = "Add explicit instantiation for " + render.short_name;

            std::ostringstream desc;
            desc << "Template '" << render.template_name << "' is instantiated "
                 << tmpl.instantiation_count << " times with total time of "
                 << std::chrono::duration_cast<std::chrono::milliseconds>(tmpl.total_time).count()
                 << "ms.\n\n";
            desc << "Suggested explicit instantiation is listed in the **Text Edits** section below.";
            suggestion.description = desc.str();

            suggestion.rationale = "Explicit template instantiation forces the compiler to "
                "instantiate a template in a single translation unit, while extern template "
                "prevents duplicate instantiations in other units.";

            const Duration savings = tmpl.total_time * (tmpl.instantiation_count - 1) /
                                     tmpl.instantiation_count;
            suggestion.estimated_savings = savings;

            if (trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(trace.total_time.count());
            }

            suggestion.implementation_steps = {
                "Add explicit instantiation definition in a compiled source file: " + render.instantiation_line,
                "Add extern template in header: " + render.extern_line,
                "Rebuild and verify link succeeds"
            };

            suggestion.impact.total_files_affected = tmpl.files_using.size();
            suggestion.impact.cumulative_savings = savings;

            suggestion.caveats = {
                "Requires identifying all type arguments used",
                "Must instantiate for each combination of template arguments",
                "Header users must see extern template before implicit use"
            };

            suggestion.verification = "Check that total template time decreases in next trace";
            suggestion.is_safe = true;
        }

        bool add_existing_instantiation_source_edits(
            Suggestion& suggestion,
            const analyzers::TemplateAnalysisResult::TemplateStats& tmpl,
            const fs::path& project_root_dir,
            const fs::path& header_path,
            const std::string& instantiation_line
        ) {
            const auto inst_source = resolve_existing_instantiation_source(tmpl, project_root_dir);
            if (!inst_source.has_value()) {
                return false;
            }

            suggestion.target_file.path = *inst_source;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Add explicit instantiation to an existing compiled source file";

            if (fs::exists(*inst_source) && fs::exists(header_path) &&
                !find_include_for_header(*inst_source, header_path.filename().string()).has_value()) {
                const std::string include_line =
                    "#include \"" + include_path_for_source(*inst_source, header_path) + "\"";
                suggestion.edits.push_back(
                    make_preferred_include_insertion_edit(*inst_source, include_line).edit
                );
            }

            std::ifstream in(*inst_source);
            const std::string source_content((std::istreambuf_iterator<char>(in)),
                                             std::istreambuf_iterator<char>());
            if (source_content.find(instantiation_line) == std::string::npos) {
                const std::size_t last_line = end_of_file_insert_line(source_content);

                TextEdit add_inst;
                add_inst.file = *inst_source;
                add_inst.start_line = last_line;
                add_inst.start_col = 0;
                add_inst.end_line = last_line;
                add_inst.end_col = 0;
                add_inst.new_text = "\n" + instantiation_line + "\n";
                suggestion.edits.push_back(add_inst);
            }

            return true;
        }

        bool add_new_instantiation_unit_edits(
            Suggestion& suggestion,
            const SuggestionContext& context,
            const fs::path& project_root_dir,
            const fs::path& header_path,
            const std::string& instantiation_line
        ) {
            fs::path inst_file = project_root_dir / "src" / "template_instantiations.cpp";
            if (!project_root_dir.empty() && !fs::exists(inst_file.parent_path())) {
                inst_file = project_root_dir / "template_instantiations.cpp";
            }
            if (project_root_dir.empty()) {
                inst_file = "template_instantiations.cpp";
            }

            suggestion.target_file.path = inst_file;
            suggestion.target_file.action = FileAction::Create;
            suggestion.target_file.note = "Create explicit instantiation file";

            if (fs::exists(inst_file)) {
                std::ifstream in(inst_file);
                const std::string inst_content((std::istreambuf_iterator<char>(in)),
                                               std::istreambuf_iterator<char>());

                if (inst_content.find(instantiation_line) == std::string::npos) {
                    const std::size_t last_line = end_of_file_insert_line(inst_content);

                    TextEdit add_inst;
                    add_inst.file = inst_file;
                    add_inst.start_line = last_line;
                    add_inst.start_col = 0;
                    add_inst.end_line = last_line;
                    add_inst.end_col = 0;
                    add_inst.new_text = "\n" + instantiation_line + "\n";
                    suggestion.edits.push_back(add_inst);
                }
            } else {
                std::ostringstream new_file_content;
                new_file_content << "// Explicit template instantiations\n";
                new_file_content << "// Auto-generated by BHA\n\n";
                if (fs::exists(header_path)) {
                    new_file_content << "#include \""
                                     << include_path_for_source(inst_file, header_path) << "\"\n\n";
                }
                new_file_content << instantiation_line << "\n";

                TextEdit create_inst;
                create_inst.file = inst_file;
                create_inst.start_line = 0;
                create_inst.start_col = 0;
                create_inst.end_line = 0;
                create_inst.end_col = 0;
                create_inst.new_text = new_file_content.str();
                suggestion.edits.push_back(create_inst);
            }

            const std::string inst_filename = inst_file.filename().string();
            bool build_wiring_proven = false;
            if (project_root_dir.empty()) {
                return build_wiring_proven;
            }

            if (fs::exists(project_root_dir / "CMakeLists.txt")) {
                const fs::path cmake_path = project_root_dir / "CMakeLists.txt";
                if (context.project_root.empty() ||
                    path_utils::is_under(cmake_path, project_root_dir)) {
                    std::ifstream cmake_in(cmake_path);
                    const std::string cmake_content((std::istreambuf_iterator<char>(cmake_in)),
                                                    std::istreambuf_iterator<char>());
                    if (cmake_content.find(inst_filename) == std::string::npos) {
                        if (auto target = find_first_cmake_target(cmake_content)) {
                            std::error_code ec;
                            fs::path rel_inst = inst_file;
                            if (rel_inst.is_absolute()) {
                                if (auto rel = fs::relative(inst_file, cmake_path.parent_path(), ec); !ec) {
                                    rel_inst = rel;
                                } else {
                                    rel_inst = inst_file.filename();
                                }
                            }

                            TextEdit cmake_edit;
                            cmake_edit.file = cmake_path;
                            cmake_edit.start_line = target->end_line + 1;
                            cmake_edit.start_col = 0;
                            cmake_edit.end_line = target->end_line + 1;
                            cmake_edit.end_col = 0;
                            cmake_edit.new_text = "\n\nif(TARGET " + target->name + ")\n"
                                                  "  target_sources(" + target->name +
                                                  " PRIVATE \"" + rel_inst.generic_string() + "\")\n"
                                                  "endif()\n";
                            suggestion.edits.push_back(cmake_edit);
                            build_wiring_proven = true;

                            FileTarget cmake_target;
                            cmake_target.path = cmake_path;
                            cmake_target.action = FileAction::Modify;
                            cmake_target.note = "Add template_instantiations.cpp to target sources";
                            suggestion.secondary_files.push_back(cmake_target);
                        }
                    }
                }
            }

            if (fs::exists(project_root_dir / "meson.build")) {
                const fs::path meson_path = project_root_dir / "meson.build";
                if (context.project_root.empty() ||
                    path_utils::is_under(meson_path, project_root_dir)) {
                    std::ifstream meson_in(meson_path);
                    const std::string meson_content((std::istreambuf_iterator<char>(meson_in)),
                                                    std::istreambuf_iterator<char>());
                    if (meson_content.find(inst_filename) == std::string::npos) {
                        if (auto span = find_first_meson_target(meson_content)) {
                            if (!span->single_line) {
                                TextEdit meson_edit;
                                meson_edit.file = meson_path;
                                meson_edit.start_line = span->end_line;
                                meson_edit.start_col = 0;
                                meson_edit.end_line = span->end_line;
                                meson_edit.end_col = 0;
                                meson_edit.new_text = "  '" + inst_filename + "',\n";
                                suggestion.edits.push_back(meson_edit);
                                build_wiring_proven = true;

                                FileTarget meson_target;
                                meson_target.path = meson_path;
                                meson_target.action = FileAction::Modify;
                                meson_target.note = "Add template_instantiations.cpp to Meson target";
                                suggestion.secondary_files.push_back(meson_target);
                            }
                        }
                    }
                }
            }

            for (const auto& entry : fs::directory_iterator(project_root_dir)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const fs::path path = entry.path();
                if (path.extension() == ".pro") {
                    std::ifstream in(path);
                    const std::string content((std::istreambuf_iterator<char>(in)),
                                              std::istreambuf_iterator<char>());
                    if (content.find(inst_filename) == std::string::npos) {
                        TextEdit pro_edit;
                        pro_edit.file = path;
                        pro_edit.start_line = count_lines_until(content, content.size());
                        pro_edit.start_col = 0;
                        pro_edit.end_line = pro_edit.start_line;
                        pro_edit.end_col = 0;
                        pro_edit.new_text = "\nSOURCES += " + inst_filename + "\n";
                        suggestion.edits.push_back(pro_edit);
                        build_wiring_proven = true;

                        FileTarget pro_target;
                        pro_target.path = path;
                        pro_target.action = FileAction::Modify;
                        pro_target.note = "Add template_instantiations.cpp to qmake sources";
                        suggestion.secondary_files.push_back(pro_target);
                    }
                }
            }

            for (const auto& name : {"Makefile", "makefile", "GNUmakefile"}) {
                if (fs::exists(project_root_dir / name)) {
                    const fs::path make_path = project_root_dir / name;
                    std::ifstream make_in(make_path);
                    const std::string make_content((std::istreambuf_iterator<char>(make_in)),
                                                   std::istreambuf_iterator<char>());
                    if (make_content.find(inst_filename) == std::string::npos) {
                        TextEdit make_edit;
                        make_edit.file = make_path;
                        make_edit.start_line = count_lines_until(make_content, make_content.size());
                        make_edit.start_col = 0;
                        make_edit.end_line = make_edit.start_line;
                        make_edit.end_col = 0;
                        make_edit.new_text = "\nSRCS += " + inst_filename + "\n";
                        suggestion.edits.push_back(make_edit);
                        build_wiring_proven = true;

                        FileTarget make_target;
                        make_target.path = make_path;
                        make_target.action = FileAction::Modify;
                        make_target.note = "Add template_instantiations.cpp to Make sources";
                        suggestion.secondary_files.push_back(make_target);
                    }
                    break;
                }
            }

            for (const auto& entry : fs::directory_iterator(project_root_dir)) {
                if (entry.path().extension() != ".vcxproj") {
                    continue;
                }
                std::ifstream proj_in(entry.path());
                const std::string proj_content((std::istreambuf_iterator<char>(proj_in)),
                                               std::istreambuf_iterator<char>());
                if (proj_content.find(inst_filename) != std::string::npos) {
                    continue;
                }
                const std::size_t cl_pos = proj_content.find("<ClCompile");
                if (cl_pos == std::string::npos) {
                    continue;
                }
                const std::size_t item_group_pos = proj_content.find("</ItemGroup>", cl_pos);
                if (item_group_pos == std::string::npos) {
                    continue;
                }

                TextEdit vcx_edit;
                vcx_edit.file = entry.path();
                vcx_edit.start_line = count_lines_until(proj_content, item_group_pos);
                vcx_edit.start_col = 0;
                vcx_edit.end_line = vcx_edit.start_line;
                vcx_edit.end_col = 0;
                vcx_edit.new_text = "  <ClCompile Include=\"" + inst_filename + "\" />\n";
                suggestion.edits.push_back(vcx_edit);
                build_wiring_proven = true;

                FileTarget vcx_target;
                vcx_target.path = entry.path();
                vcx_target.action = FileAction::Modify;
                vcx_target.note = "Add template_instantiations.cpp to MSBuild project";
                suggestion.secondary_files.push_back(vcx_target);
                break;
            }

            return build_wiring_proven;
        }

        void add_extern_template_edit(
            Suggestion& suggestion,
            const fs::path& declaration_header,
            const fs::path& header_path,
            const std::string& extern_line
        ) {
            if (!fs::exists(header_path)) {
                return;
            }

            std::ifstream header_in(header_path);
            const std::string header_content((std::istreambuf_iterator<char>(header_in)),
                                             std::istreambuf_iterator<char>());
            if (header_content.find(extern_line) != std::string::npos) {
                return;
            }

            const std::size_t insert_line = find_extern_template_insertion_line(header_path);
            const auto header_lines = file_utils::read_lines(header_path);
            const std::size_t line_count = header_lines.is_ok()
                ? header_lines.value().size()
                : 0;

            TextEdit extern_edit;
            extern_edit.file = header_path;
            if (insert_line >= line_count) {
                const std::size_t last_line = end_of_file_insert_line(header_content);
                extern_edit.start_line = last_line;
                extern_edit.start_col = 0;
                extern_edit.end_line = last_line;
                extern_edit.end_col = 0;
                extern_edit.new_text = "\n" + extern_line + "\n";
            } else {
                extern_edit.start_line = insert_line;
                extern_edit.start_col = 0;
                extern_edit.end_line = insert_line;
                extern_edit.end_col = 0;
                extern_edit.new_text = extern_line + "\n";
            }
            suggestion.edits.push_back(extern_edit);

            FileTarget header_target;
            header_target.path = header_path;
            header_target.action = FileAction::Modify;
            header_target.line_start = insert_line + 1;
            header_target.line_end = insert_line + 1;
            header_target.note = header_path == declaration_header
                ? "Add extern template declaration"
                : "Add extern template declaration in a header that already exposes dependent types";
            suggestion.secondary_files.push_back(header_target);
        }

        void mark_manual_integration_advisory(Suggestion& suggestion) {
            suggestion.application_mode = SuggestionApplicationMode::Advisory;
            suggestion.application_summary = "Manual integration required";
            suggestion.application_guidance =
                "BHA could place the extern template declaration in a compile-valid header context, but it could not prove that a generated explicit-instantiation translation unit would be compiled. "
                "Place the explicit instantiation in an existing compiled source file, or add the generated file to the owning target manually, then rerun BHA.";
            suggestion.auto_apply_blocked_reason =
                "No existing compiled source file was available for the explicit instantiation, and BHA could not prove build-system ownership for a new translation unit.";
            suggestion.is_safe = false;
            suggestion.edits.clear();
            suggestion.secondary_files.clear();
        }

    }  // namespace

    Result<SuggestionResult, Error> TemplateSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& templates = context.analysis.templates;

        if (templates.templates.empty()) {
            auto end_time = std::chrono::steady_clock::now();
            result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        const auto& template_cfg = context.options.heuristics.templates;
        const std::size_t min_instantiation_count = std::max<std::size_t>(template_cfg.min_instantiation_count, 2);
        const auto min_template_time = template_cfg.min_total_time;

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& tmpl : templates.templates) {
            if (context.is_cancelled()) {
                break;
            }
            ++analyzed;

            if (tmpl.instantiation_count < min_instantiation_count) {
                ++skipped;
                continue;
            }

            if (tmpl.total_time < min_template_time) {
                ++skipped;
                continue;
            }

            if (!context.target_files_lookup.empty()) {
                bool any_target = false;
                for (const auto& file : tmpl.files_using) {
                    if (context.should_analyze(fs::path(file))) {
                        any_target = true;
                        break;
                    }
                }
                if (!any_target) {
                    ++skipped;
                    continue;
                }
            }

            const std::string& template_name = !tmpl.full_signature.empty() ? tmpl.full_signature : tmpl.name;

            if (template_name.find('<') == std::string::npos ||
                template_name.find('>') == std::string::npos) {
                ++skipped;
                continue;
            }
            if (template_name.size() > 512) {
                ++skipped;
                continue;
            }

            const bool is_function_template = looks_like_function_template(template_name);
            if (is_blacklisted_template(template_name)) {
                ++skipped;
                continue;
            }
            if (has_unspellable_instantiation_component(template_name)) {
                const fs::path advisory_target = !tmpl.locations.empty() && tmpl.locations.front().has_location()
                    ? resolve_source_path(tmpl.locations.front().file)
                    : (!tmpl.files_using.empty() ? resolve_source_path(tmpl.files_using.front()) : fs::path("template-instantiation"));
                result.suggestions.push_back(
                    make_unspellable_template_advisory(tmpl, template_name, advisory_target, context.trace)
                );
                continue;
            }
            if (is_function_template &&
                (template_name.find('(') == std::string::npos || template_name.find(')') == std::string::npos)) {
                ++skipped;
                continue;
            }

            const std::unordered_set<std::string> unique_users(tmpl.files_using.begin(), tmpl.files_using.end());
            if (unique_users.size() < 2) {
                ++skipped;
                continue;
            }
            const std::string normalized_template_name = is_function_template
                ? template_name
                : strip_leading_keywords(template_name);
            if (normalized_template_name.size() > 220) {
                ++skipped;
                continue;
            }
            if (!is_function_template &&
                count_top_level_template_args(normalized_template_name) > 4) {
                ++skipped;
                continue;
            }
            const std::string base_name = is_function_template
                ? base_function_name(normalized_template_name)
                : base_template_name(normalized_template_name);
            if (base_name.empty()) {
                ++skipped;
                continue;
            }
            auto selected_header = select_template_header_candidate(
                tmpl,
                context.project_root,
                normalized_template_name,
                base_name,
                is_function_template
            );
            if (!selected_header.has_value()) {
                ++skipped;
                continue;
            }
            if (selected_header->insertion_header.empty()) {
                result.suggestions.push_back(make_context_visibility_advisory(
                    tmpl,
                    template_name,
                    selected_header->declaration_header,
                    context.trace
                ));
                continue;
            }

            const fs::path& declaration_header = selected_header->declaration_header;
            const fs::path& header_path = selected_header->insertion_header;
            const auto render = make_template_render_info(
                template_name,
                normalized_template_name,
                base_name,
                selected_header->class_key,
                is_function_template
            );

            Suggestion suggestion;
            populate_template_suggestion_metadata(
                suggestion,
                tmpl,
                render,
                context.trace,
                header_path
            );

            const fs::path project_root_dir = resolve_project_root_dir(context, tmpl);
            const bool has_existing_instantiation_unit = add_existing_instantiation_source_edits(
                suggestion,
                tmpl,
                project_root_dir,
                header_path,
                render.instantiation_line
            );

            bool build_wiring_proven = has_existing_instantiation_unit;
            if (!has_existing_instantiation_unit) {
                build_wiring_proven = add_new_instantiation_unit_edits(
                    suggestion,
                    context,
                    project_root_dir,
                    header_path,
                    render.instantiation_line
                );
            }

            add_extern_template_edit(
                suggestion,
                declaration_header,
                header_path,
                render.extern_line
            );

            if (!build_wiring_proven) {
                mark_manual_integration_advisory(suggestion);
            }

            if (suggestion.edits.empty() &&
                suggestion.application_mode == SuggestionApplicationMode::DirectEdits) {
                ++skipped;
                continue;
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

    void register_template_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<TemplateSuggester>()
        );
    }
}  // namespace bha::suggestions
