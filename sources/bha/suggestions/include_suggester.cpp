//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/include_suggester.hpp"
#include "bha/suggestions/unreal_context.hpp"
#include "bha/utils/path_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {

        struct TidyUnusedInclude {
            fs::path file;
            std::size_t line = 0;
            std::string header_name;
        };

        struct ConditionalContext {
            bool inside_conditional = false;
            bool platform_guarded = false;
            std::vector<std::string> active_conditions;
        };

        struct IncludeRemovalAssessment {
            bool allow_direct_edits = true;
            bool platform_sensitive = false;
            std::string blocked_reason;
        };

        std::optional<Suggestion> build_unreal_iwyu_suggestion(const SuggestionContext& context) {
            if (!context.options.heuristics.unreal.emit_iwyu) {
                return std::nullopt;
            }

            const auto modules = collect_unreal_module_context(context);
            std::vector<UnrealModuleContext> candidates;
            candidates.reserve(modules.size());
            for (const auto& module : modules) {
                if (module.rules.build_cs_path.empty()) {
                    continue;
                }
                if (module.stats.source_files == 0) {
                    continue;
                }
                if (module.rules.enforce_iwyu.value_or(false)) {
                    continue;
                }
                candidates.push_back(module);
            }

            if (candidates.empty()) {
                return std::nullopt;
            }

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("unreal-iwyu", candidates.front().rules.build_cs_path);
            suggestion.type = SuggestionType::IncludeRemoval;
            suggestion.priority = candidates.size() >= 3 ? Priority::High : Priority::Medium;
            suggestion.confidence = 0.83;
            suggestion.title = "Unreal Module IWYU (UBT) Configuration (" + std::to_string(candidates.size()) + " modules)";

            std::ostringstream desc;
            desc << "Apply a ModuleRules-only UnrealBuildTool (UBT) change to enable IWYU without bulk source rewrites:\n";
            for (const auto& module : candidates) {
                const auto include_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    module.stats.include_parse_time
                ).count();
                desc << "  - " << module.rules.module_name
                     << " (" << make_repo_relative(module.rules.build_cs_path)
                     << ", " << module.stats.source_files << " source files"
                     << ", include parse " << include_ms << "ms)\n";
                desc << "    Set: bEnforceIWYU = true;\n";
            }
            suggestion.description = desc.str();

            suggestion.rationale =
                "This suggestion only updates ModuleRules (.Build.cs) settings in UBT. "
                "That keeps rollout localized per module while improving include discipline and reducing transitive include churn.";

            Duration total_include_time = Duration::zero();
            std::size_t affected_files = 0;
            for (const auto& module : candidates) {
                total_include_time += module.stats.include_parse_time;
                affected_files += module.stats.source_files;
            }
            suggestion.estimated_savings = total_include_time / 10;
            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }
            suggestion.impact.total_files_affected = affected_files;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;

            suggestion.target_file.path = candidates.front().rules.build_cs_path;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Enable module IWYU enforcement in ModuleRules";
            if (candidates.front().rules.enforce_iwyu_line.has_value()) {
                suggestion.target_file.line_start = *candidates.front().rules.enforce_iwyu_line;
                suggestion.target_file.line_end = *candidates.front().rules.enforce_iwyu_line;
            }
            for (std::size_t i = 1; i < candidates.size(); ++i) {
                FileTarget secondary;
                secondary.path = candidates[i].rules.build_cs_path;
                secondary.action = FileAction::Modify;
                secondary.note = "Enable module IWYU enforcement in ModuleRules";
                if (candidates[i].rules.enforce_iwyu_line.has_value()) {
                    secondary.line_start = *candidates[i].rules.enforce_iwyu_line;
                    secondary.line_end = *candidates[i].rules.enforce_iwyu_line;
                }
                suggestion.secondary_files.push_back(std::move(secondary));
            }

            suggestion.implementation_steps = {
                "Set bEnforceIWYU = true; in each listed <Module>.Build.cs file",
                "Regenerate project files and run UnrealBuildTool for each affected target",
                "Fix direct-include violations reported by UBT/IWYU checks"
            };
            suggestion.caveats = {
                "Some modules intentionally rely on legacy transitive includes and may need staged rollout",
                "Keep generated headers and module export headers included explicitly where required",
                "UHT requires *.generated.h to remain the last include in each UObject header"
            };
            suggestion.verification =
                "Run UnrealBuildTool for targets using the updated modules and confirm clean compile with IWYU enabled.";

            const auto include_order_violations = find_generated_include_order_violations(context.project_root);
            const auto module_name_collisions = find_unreal_module_name_collisions(modules);
            const bool has_include_order_blocker = !include_order_violations.empty();
            const bool has_module_name_collision = !module_name_collisions.empty();
            if (!has_include_order_blocker && !has_module_name_collision) {
                for (const auto& module : candidates) {
                    if (auto edit = make_unreal_assignment_edit(
                        module.rules.build_cs_path,
                        "bEnforceIWYU",
                        "true",
                        module.rules.enforce_iwyu_line
                    )) {
                        suggestion.edits.push_back(std::move(*edit));
                    }
                }
            }

            if (has_include_order_blocker) {
                suggestion.application_mode = SuggestionApplicationMode::Advisory;
                suggestion.is_safe = false;
                suggestion.application_summary = "Manual review only";
                suggestion.application_guidance =
                    "Fix Unreal header include order first, then enable bEnforceIWYU. Keep *.generated.h as the last include in each header.";
                const auto& first = include_order_violations.front();
                std::ostringstream reason;
                reason << "Detected Unreal header include-order violations (include appears after *.generated.h), e.g. "
                       << make_repo_relative(first.header_path) << ":" << first.generated_include_line
                       << " followed by include at line " << first.trailing_include_line << ".";
                suggestion.auto_apply_blocked_reason = reason.str();
                suggestion.caveats.push_back(
                    "Unreal requires *.generated.h to be the last include in a header; enable IWYU only after these files are corrected."
                );
            } else if (has_module_name_collision) {
                suggestion.application_mode = SuggestionApplicationMode::Advisory;
                suggestion.is_safe = false;
                suggestion.application_summary = "Manual review only";
                suggestion.application_guidance =
                    "Multiple ModuleRules files define the same Unreal module name. Resolve module ownership ambiguity, then apply IWYU settings.";
                const auto& first = module_name_collisions.front();
                std::ostringstream reason;
                reason << "Ambiguous Unreal module rules for '" << first.name
                       << "' (multiple .Build.cs files): ";
                for (std::size_t i = 0; i < first.paths.size(); ++i) {
                    if (i > 0) {
                        reason << ", ";
                    }
                    reason << make_repo_relative(first.paths[i]);
                }
                suggestion.auto_apply_blocked_reason = reason.str();
            } else if (suggestion.edits.size() == candidates.size()) {
                suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
                suggestion.is_safe = true;
                suggestion.application_summary = "Auto-apply via direct text edits";
                suggestion.application_guidance =
                    "BHA can set bEnforceIWYU in ModuleRules files. Rebuild Unreal targets and rollback if validation fails.";
            } else {
                suggestion.application_mode = SuggestionApplicationMode::Advisory;
                suggestion.is_safe = false;
                suggestion.application_summary = "Manual review only";
                suggestion.application_guidance =
                    "Automatic edit placement failed for one or more ModuleRules files. Apply the listed Build.cs changes manually.";
                suggestion.auto_apply_blocked_reason =
                    "At least one ModuleRules constructor block could not be located for safe edit insertion.";
            }

            return suggestion;
        }

        bool is_likely_system_header(const fs::path& path) {
            const std::string p = path.generic_string();
            return p.starts_with("/usr/") ||
                   p.starts_with("/opt/") ||
                   p.find("/include/c++/") != std::string::npos ||
                   p.find("Program Files") != std::string::npos;
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

        std::optional<fs::path> find_compile_commands_dir(const fs::path& project_root) {
            if (project_root.empty()) {
                return std::nullopt;
            }

            const std::array candidates{
                project_root,
                project_root / "build",
                project_root / "out" / "build",
                project_root / "cmake-build-debug",
                project_root / "cmake-build-release"
            };
            for (const auto& dir : candidates) {
                if (fs::exists(dir / "compile_commands.json")) {
                    return dir;
                }
            }

            std::error_code ec;
            std::size_t scanned = 0;
            for (const auto& entry : fs::recursive_directory_iterator(project_root, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (++scanned > 2000) {
                    break;
                }
                if (entry.path().filename() == "compile_commands.json") {
                    return entry.path().parent_path();
                }
            }

            return std::nullopt;
        }

        std::optional<fs::path> resolve_compile_commands_dir(const SuggestionContext& context) {
            if (context.options.compile_commands_path.has_value()) {
                fs::path path = *context.options.compile_commands_path;
                if (path.filename() == "compile_commands.json") {
                    path = path.parent_path();
                }
                if (!path.empty() && fs::exists(path / "compile_commands.json")) {
                    return path;
                }
            }
            if (auto discovered = find_compile_commands_dir(context.project_root)) {
                return discovered;
            }
            return std::nullopt;
        }

        std::vector<fs::path> collect_compile_commands_sources(const fs::path& build_dir) {
            std::vector<fs::path> sources;
            const fs::path compile_commands = build_dir / "compile_commands.json";
            std::ifstream input(compile_commands);
            if (!input) {
                return sources;
            }

            const std::string content{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()
            };
            const std::regex file_regex(R"re("file"\s*:\s*"([^"]+)")re");
            std::smatch match;
            auto it = content.cbegin();
            std::unordered_set<std::string> seen;
            while (std::regex_search(it, content.cend(), match, file_regex)) {
                fs::path source = match[1].str();
                source = source.lexically_normal();
                const std::string key = source.generic_string();
                if (seen.insert(key).second) {
                    sources.push_back(std::move(source));
                }
                it = match.suffix().first;
            }
            return sources;
        }

        std::vector<TidyUnusedInclude> run_include_cleaner_for_file(
            const fs::path& build_dir,
            const fs::path& source_file
        ) {
            std::vector<TidyUnusedInclude> results;
            const fs::path resolved_source = resolve_source_path(source_file);
            if (!fs::exists(resolved_source)) {
                return results;
            }

            static const auto clang_tidy_binary = [] {
                if (const char* env = std::getenv("BHA_CLANG_TIDY")) {
                    const fs::path path = env;
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
                return std::string("clang-tidy");
            }();

            const std::string cmd =
                shell_quote(clang_tidy_binary) + " -checks=" + shell_quote("-*,misc-include-cleaner") +
                " -p " + shell_quote(build_dir.string()) +
                " " + shell_quote(resolved_source.string()) + " --quiet 2>&1";

            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) {
                return results;
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
                return results;
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
                const auto third_colon = line.find(':', second_colon + 1);
                if (third_colon == std::string::npos) {
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

                const auto directives = find_include_directives(diag_file);
                auto it = std::find_if(
                    directives.begin(),
                    directives.end(),
                    [&](const IncludeDirective& include_dir) {
                        return include_dir.line + 1 == line_number;
                    }
                );
                if (it == directives.end()) {
                    continue;
                }

                TidyUnusedInclude diag;
                diag.file = diag_file;
                diag.line = it->line;
                diag.header_name = it->header_name;
                results.push_back(std::move(diag));
            }

            return results;
        }

        std::unordered_map<std::string, std::vector<TidyUnusedInclude>> collect_tidy_unused_includes(
            const fs::path& build_dir,
            const std::vector<fs::path>& source_files
        ) {
            std::unordered_map<std::string, std::vector<TidyUnusedInclude>> results;
            for (const auto& source_file : source_files) {
                const fs::path resolved_source = resolve_source_path(source_file).lexically_normal();
                const std::string key = resolved_source.generic_string();
                auto diagnostics = run_include_cleaner_for_file(build_dir, resolved_source);
                if (!diagnostics.empty()) {
                    results.emplace(key, std::move(diagnostics));
                }
            }
            return results;
        }

        bool matches_candidate_header(
            const std::string& include_name,
            const fs::path& header_path
        ) {
            const fs::path include_path(include_name);
            const fs::path normalized_header = header_path.lexically_normal();
            return include_path.filename() == normalized_header.filename() ||
                   include_path.generic_string() == normalized_header.generic_string();
        }

        const analyzers::DependencyAnalysisResult::HeaderInfo* find_header_info(
            const analyzers::DependencyAnalysisResult& deps,
            const std::string& include_name
        ) {
            const fs::path include_path(include_name);
            for (const auto& header : deps.headers) {
                if (matches_candidate_header(include_name, header.path)) {
                    return &header;
                }
                if (include_path.filename() == header.path.filename()) {
                    return &header;
                }
            }
            return nullptr;
        }

        bool is_header_file(const fs::path& file) {
            static constexpr std::array<std::string_view, 5> kHeaderExts = {
                ".h", ".hpp", ".hh", ".hxx", ".h++"
            };
            return path_has_extension(file, kHeaderExts);
        }

        bool is_source_file(const fs::path& file) {
            static constexpr std::array<std::string_view, 4> kSourceExts = {
                ".c", ".cc", ".cpp", ".cxx"
            };
            return path_has_extension(file, kSourceExts);
        }

        [[nodiscard]] std::string uppercase_copy(std::string input) {
            std::ranges::transform(
                input,
                input.begin(),
                [](const unsigned char ch) {
                    return static_cast<char>(std::toupper(ch));
                }
            );
            return input;
        }

        [[nodiscard]] bool contains_any_token(
            const std::string& haystack,
            const std::initializer_list<std::string_view> tokens
        ) {
            for (const auto token : tokens) {
                if (haystack.find(token) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool condition_mentions_platform(const std::string& condition) {
            const std::string upper = uppercase_copy(condition);
            return contains_any_token(
                upper,
                {
                    "_WIN32",
                    "_WIN64",
                    "WIN32",
                    "WIN64",
                    "__WINDOWS__",
                    "__MINGW32__",
                    "__MINGW64__",
                    "__CYGWIN__",
                    "__LINUX__",
                    "__GNU_LINUX__",
                    "__APPLE__",
                    "__MACH__",
                    "TARGET_OS_",
                    "__ANDROID__",
                    "__FREEBSD__",
                    "__NETBSD__",
                    "__OPENBSD__",
                    "__UNIX__",
                    "__UNIX",
                    "__POSIX__",
                    "_POSIX_"
                }
            );
        }

        [[nodiscard]] bool header_name_is_platform_specific(const std::string& header_name) {
            const std::string normalized = uppercase_copy(fs::path(header_name).generic_string());
            return contains_any_token(
                normalized,
                {
                    "WINDOWS.H",
                    "WINUSER.H",
                    "WINSOCK",
                    "WINNT.H",
                    "WINBASE.H",
                    "WINDEF.H",
                    "AFX",
                    "ATL",
                    "LINUX/",
                    "SYS/",
                    "UNISTD.H",
                    "PTHREAD.H",
                    "MACH/",
                    "COREFOUNDATION/",
                    "IOKIT/",
                    "X11/",
                    "WAYLAND/",
                    "ANDROID/"
                }
            );
        }

        [[nodiscard]] ConditionalContext analyze_conditional_context(
            const fs::path& file,
            const std::size_t target_line
        ) {
            ConditionalContext context;
            auto lines_result = file_utils::read_lines(file);
            if (lines_result.is_err()) {
                return context;
            }

            struct ConditionalFrame {
                std::string expression;
                bool platform_guarded = false;
            };

            std::vector<ConditionalFrame> stack;
            stack.reserve(8);
            bool in_block_comment = false;

            for (std::size_t index = 0; index < lines_result.value().size(); ++index) {
                if (index == target_line) {
                    context.inside_conditional = !stack.empty();
                    for (const auto& frame : stack) {
                        context.active_conditions.push_back(frame.expression);
                        context.platform_guarded = context.platform_guarded || frame.platform_guarded;
                    }
                    return context;
                }

                const std::string cleaned = trim_whitespace_copy(
                    strip_comments_and_strings(lines_result.value()[index], in_block_comment)
                );
                if (!cleaned.starts_with('#')) {
                    continue;
                }

                auto directive = trim_whitespace_copy(cleaned.substr(1));
                if (directive.rfind("ifdef", 0) == 0) {
                    directive = trim_whitespace_copy(directive.substr(5));
                    stack.push_back({directive, condition_mentions_platform(directive)});
                    continue;
                }
                if (directive.rfind("ifndef", 0) == 0) {
                    directive = trim_whitespace_copy(directive.substr(6));
                    const std::string expression = "!defined(" + directive + ")";
                    stack.push_back({expression, condition_mentions_platform(directive)});
                    continue;
                }
                if (directive.rfind("if", 0) == 0) {
                    directive = trim_whitespace_copy(directive.substr(2));
                    stack.push_back({directive, condition_mentions_platform(directive)});
                    continue;
                }
                if (directive.rfind("elif", 0) == 0) {
                    directive = trim_whitespace_copy(directive.substr(4));
                    if (!stack.empty()) {
                        stack.back() = {directive, condition_mentions_platform(directive)};
                    }
                    continue;
                }
                if (directive == "else") {
                    if (!stack.empty()) {
                        const bool platform_guarded = stack.back().platform_guarded;
                        stack.back() = {"else", platform_guarded};
                    }
                    continue;
                }
                if (directive == "endif") {
                    if (!stack.empty()) {
                        stack.pop_back();
                    }
                }
            }

            return context;
        }

        [[nodiscard]] IncludeRemovalAssessment assess_include_removal_safety(
            const TidyUnusedInclude& diag
        ) {
            IncludeRemovalAssessment assessment;
            const ConditionalContext conditional = analyze_conditional_context(diag.file, diag.line);
            const bool conditional_include = conditional.inside_conditional;
            const bool platform_specific_header = header_name_is_platform_specific(diag.header_name);
            assessment.platform_sensitive = conditional.platform_guarded || platform_specific_header;

            if (!conditional_include && !assessment.platform_sensitive) {
                return assessment;
            }

            assessment.allow_direct_edits = false;
            std::ostringstream reason;
            if (conditional_include) {
                reason << "Include is inside conditional compilation";
                if (!conditional.active_conditions.empty()) {
                    reason << " (";
                    for (std::size_t i = 0; i < conditional.active_conditions.size(); ++i) {
                        if (i > 0) {
                            reason << ", ";
                        }
                        reason << conditional.active_conditions[i];
                    }
                    reason << ")";
                }
            }
            if (assessment.platform_sensitive) {
                if (!assessment.blocked_reason.empty() || reason.tellp() > 0) {
                    reason << "; ";
                }
                reason << "header appears platform-specific for the active toolchain/configuration";
            }
            assessment.blocked_reason = reason.str();
            return assessment;
        }

        std::vector<std::string> extract_declared_type_names(const fs::path& header_path) {
            auto lines_result = file_utils::read_lines(header_path);
            if (lines_result.is_err()) {
                return {};
            }

            static const std::regex class_or_struct_regex(
                R"(^\s*(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"
            );

            std::vector<std::string> names;
            std::unordered_set<std::string> seen;
            bool in_block_comment = false;
            bool template_pending = false;

            for (const auto& raw_line : lines_result.value()) {
                const std::string line = strip_comments_and_strings(raw_line, in_block_comment);

                const auto start = line.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) {
                    continue;
                }
                const auto end = line.find_last_not_of(" \t\r\n");
                const std::string trimmed = line.substr(start, end - start + 1);

                if (trimmed.rfind("template", 0) == 0) {
                    template_pending = true;
                    if (trimmed.find('>') != std::string::npos) {
                        template_pending = false;
                    }
                    continue;
                }

                if (template_pending) {
                    if (trimmed.find('>') != std::string::npos) {
                        template_pending = false;
                    }
                    continue;
                }

                std::smatch match;
                if (!std::regex_search(trimmed, match, class_or_struct_regex)) {
                    continue;
                }
                if (trimmed.find('{') == std::string::npos && trimmed.find(';') == std::string::npos) {
                    continue;
                }

                const std::string name = match[2].str();
                if (seen.insert(name).second) {
                    names.push_back(name);
                }
            }

            return names;
        }

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

        bool contains_identifier(const std::string& line, const std::string& symbol);

        bool file_references_any_identifier(
            const fs::path& file_path,
            const std::vector<std::string>& identifiers
        ) {
            if (identifiers.empty()) {
                return false;
            }

            auto lines_result = file_utils::read_lines(file_path);
            if (lines_result.is_err()) {
                return false;
            }

            bool in_block_comment = false;
            for (const auto& raw_line : lines_result.value()) {
                const std::string line = strip_comments_and_strings(raw_line, in_block_comment);
                for (const auto& identifier : identifiers) {
                    if (contains_identifier(line, identifier)) {
                        return true;
                    }
                }
            }

            return false;
        }

        struct MoveToCppAssessment {
            bool has_forward_decl = false;
            bool mentions_symbol = false;
            bool unsafe_usage = false;
        };

        bool is_identifier_char(const char ch) {
            const unsigned char value = static_cast<unsigned char>(ch);
            return std::isalnum(value) || ch == '_';
        }

        bool contains_identifier(const std::string& line, const std::string& symbol) {
            if (symbol.empty() || line.empty()) {
                return false;
            }
            std::size_t pos = line.find(symbol);
            while (pos != std::string::npos) {
                const bool left_ok = (pos == 0) || !is_identifier_char(line[pos - 1]);
                const std::size_t end = pos + symbol.size();
                const bool right_ok = (end >= line.size()) || !is_identifier_char(line[end]);
                if (left_ok && right_ok) {
                    return true;
                }
                pos = line.find(symbol, pos + 1);
            }
            return false;
        }

        MoveToCppAssessment assess_move_to_cpp(
            const fs::path& including_header,
            const std::vector<std::string>& symbols
        ) {
            MoveToCppAssessment assessment;
            if (symbols.empty()) {
                return assessment;
            }

            auto lines_result = file_utils::read_lines(including_header);
            if (lines_result.is_err()) {
                return assessment;
            }

            bool in_block_comment = false;
            for (const auto& raw_line : lines_result.value()) {
                const std::string line = strip_comments_and_strings(raw_line, in_block_comment);
                for (const auto& symbol : symbols) {
                    if (!contains_identifier(line, symbol)) {
                        continue;
                    }

                    assessment.mentions_symbol = true;

                    if (line.find("class " + symbol) != std::string::npos ||
                        line.find("struct " + symbol) != std::string::npos) {
                        if (line.find(';') != std::string::npos && line.find('{') == std::string::npos) {
                            assessment.has_forward_decl = true;
                            continue;
                        }
                    }

                    const auto symbol_pos = line.find(symbol);
                    std::size_t after_pos = symbol_pos + symbol.size();
                    while (after_pos < line.size() &&
                           std::isspace(static_cast<unsigned char>(line[after_pos]))) {
                        ++after_pos;
                    }
                    const char next = after_pos < line.size() ? line[after_pos] : '\0';
                    if (next == '*' || next == '&') {
                        continue;
                    }

                    const bool inheritance = (line.find("class ") != std::string::npos ||
                                              line.find("struct ") != std::string::npos) &&
                                              line.find(':') != std::string::npos;
                    const bool complete_type_ops = line.find("sizeof") != std::string::npos ||
                                                   line.find("alignof") != std::string::npos ||
                                                   line.find("new ") != std::string::npos ||
                                                   line.find("delete ") != std::string::npos ||
                                                   line.find("dynamic_cast") != std::string::npos ||
                                                   line.find("static_cast") != std::string::npos ||
                                                   line.find("typeid") != std::string::npos ||
                                                   line.find(symbol + "::") != std::string::npos;
                    const bool template_by_value = line.find("<" + symbol + ">") != std::string::npos ||
                                                   line.find("< " + symbol + " >") != std::string::npos;

                    if (inheritance || complete_type_ops || template_by_value ||
                        line.find(';') != std::string::npos || line.find('(') != std::string::npos) {
                        assessment.unsafe_usage = true;
                        break;
                    }
                }
                if (assessment.unsafe_usage) {
                    break;
                }
            }

            return assessment;
        }

        std::optional<fs::path> find_corresponding_source(
            const fs::path& header_path,
            const fs::path& project_root,
            const std::vector<fs::path>& source_files
        ) {
            static const std::array source_exts{".cpp", ".cc", ".cxx", ".c"};

            const fs::path normalized = header_path.lexically_normal();
            for (const auto& ext : source_exts) {
                fs::path candidate = normalized;
                candidate.replace_extension(ext);
                if (fs::exists(candidate)) {
                    return candidate.lexically_normal();
                }
            }

            if (!project_root.empty()) {
                const fs::path include_root = (project_root / "include").lexically_normal();
                if (path_utils::is_under(normalized, include_root)) {
                    const fs::path rel = normalized.lexically_relative(include_root);
                    if (!rel.empty()) {
                        for (const auto& ext : source_exts) {
                            fs::path candidate = project_root / "src" / rel;
                            candidate.replace_extension(ext);
                            if (fs::exists(candidate)) {
                                return candidate.lexically_normal();
                            }
                        }
                    }
                }
            }

            for (const auto& source : source_files) {
                const fs::path normalized_source = source.lexically_normal();
                if (normalized_source.stem() != normalized.stem()) {
                    continue;
                }
                return normalized_source;
            }

            return std::nullopt;
        }

        std::optional<fs::path> resolve_local_include_path(
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

    }  // namespace

    Result<SuggestionResult, Error> IncludeSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        if (is_unreal_mode_active(context)) {
            if (auto unreal_iwyu = build_unreal_iwyu_suggestion(context)) {
                result.suggestions.push_back(std::move(*unreal_iwyu));
                result.items_analyzed = 1;
            }
            auto end_time = std::chrono::steady_clock::now();
            result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        const auto& deps = context.analysis.dependencies;
        const auto& files = context.analysis.files;
        const auto compile_commands_dir = resolve_compile_commands_dir(context);
        std::unordered_map<std::string, std::vector<TidyUnusedInclude>> tidy_unused_cache;
        std::unordered_set<std::string> clang_tidy_scanned;

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        if (compile_commands_dir.has_value()) {
            std::vector<fs::path> to_scan;
            const auto compile_db_sources = collect_compile_commands_sources(*compile_commands_dir);
            to_scan.reserve(std::min<std::size_t>(compile_db_sources.size(), 25));
            for (const auto& source_file : compile_db_sources) {
                const auto ext = source_file.extension().string();
                if (ext != ".c" && ext != ".cc" && ext != ".cpp" && ext != ".cxx") {
                    continue;
                }
                const fs::path resolved = source_file.lexically_normal();
                const std::string key = resolved.generic_string();
                if (!clang_tidy_scanned.insert(key).second) {
                    continue;
                }
                to_scan.push_back(resolved);
                if (to_scan.size() >= 25) {
                    break;
                }
            }

            tidy_unused_cache = collect_tidy_unused_includes(*compile_commands_dir, to_scan);

            std::unordered_map<std::string, std::vector<TidyUnusedInclude>> grouped_diagnostics;
            for (const auto& [_, diagnostics] : tidy_unused_cache) {
                for (const auto& diag : diagnostics) {
                    grouped_diagnostics[diag.header_name].push_back(diag);
                }
            }

            for (auto& [header_name, diagnostics] : grouped_diagnostics) {
                if (diagnostics.empty()) {
                    continue;
                }

                std::vector<TidyUnusedInclude> safe_diagnostics;
                std::vector<TidyUnusedInclude> advisory_diagnostics;
                std::vector<std::string> advisory_reasons;
                std::unordered_map<std::string, std::vector<std::string>> callable_symbol_cache;
                safe_diagnostics.reserve(diagnostics.size());
                advisory_diagnostics.reserve(diagnostics.size());
                for (const auto& diag : diagnostics) {
                    IncludeRemovalAssessment assessment = assess_include_removal_safety(diag);
                    if (assessment.allow_direct_edits) {
                        std::optional<fs::path> resolved_header;
                        if (const auto* resolved_header_info = find_header_info(deps, diag.header_name);
                            resolved_header_info != nullptr) {
                            resolved_header = resolve_project_path(
                                resolved_header_info->path,
                                context.project_root
                            );
                        } else {
                            resolved_header = resolve_local_include_path(
                                diag.header_name,
                                diag.file,
                                context.project_root
                            );
                        }

                        if (resolved_header.has_value()) {
                            const std::string cache_key = resolved_header->lexically_normal().generic_string();
                            auto cache_it = callable_symbol_cache.find(cache_key);
                            if (cache_it == callable_symbol_cache.end()) {
                                cache_it = callable_symbol_cache.emplace(
                                    cache_key,
                                    extract_declared_callable_names(*resolved_header)
                                ).first;
                            }

                            if (!cache_it->second.empty() &&
                                file_references_any_identifier(diag.file, cache_it->second)) {
                                assessment.allow_direct_edits = false;
                                assessment.blocked_reason =
                                    "source file references callable API declared by the included header";
                            }
                        }
                    }
                    if (assessment.allow_direct_edits) {
                        safe_diagnostics.push_back(diag);
                        continue;
                    }
                    advisory_diagnostics.push_back(diag);
                    if (!assessment.blocked_reason.empty()) {
                        advisory_reasons.push_back(assessment.blocked_reason);
                    }
                }

                const std::string header_name_copy = header_name;
                const auto* header_info = find_header_info(deps, header_name_copy);
                const auto estimated_savings = header_info != nullptr
                    ? header_info->total_parse_time / 4
                    : Duration::zero();

                auto build_include_removal_suggestion = [&](const std::vector<TidyUnusedInclude>& selected_diagnostics,
                                                           const bool direct_edits) {
                    Suggestion suggestion;
                    suggestion.id = generate_suggestion_id(
                        direct_edits ? "unused-explicit" : "unused-explicit-advisory",
                        fs::path(header_name_copy)
                    );
                    suggestion.type = SuggestionType::IncludeRemoval;
                    suggestion.priority = selected_diagnostics.size() >= 10 ? Priority::High : Priority::Medium;
                    suggestion.confidence = direct_edits ? 0.98 : 0.72;
                    suggestion.title = (direct_edits
                        ? "Remove unused include of "
                        : "Review conditional/platform-specific include of ") +
                        fs::path(header_name_copy).filename().string();
                    suggestion.estimated_savings = estimated_savings;
                    if (context.trace.total_time.count() > 0) {
                        suggestion.estimated_savings_percent =
                            100.0 * static_cast<double>(estimated_savings.count()) /
                            static_cast<double>(context.trace.total_time.count());
                    }

                    std::ostringstream desc;
                    desc << "clang-tidy misc-include-cleaner reported '" << header_name_copy
                         << "' as unused in " << selected_diagnostics.size() << " translation units.";
                    suggestion.description = desc.str();
                    suggestion.rationale =
                        "This suggestion is based on explicit semantic diagnostics from clang-tidy, but BHA avoids auto-removing includes when the active build configuration may hide cross-platform or conditional dependencies.";

                    suggestion.target_file.path = selected_diagnostics.front().file;
                    suggestion.target_file.action = FileAction::Modify;
                    suggestion.target_file.line_start = selected_diagnostics.front().line + 1;
                    suggestion.target_file.line_end = selected_diagnostics.front().line + 1;
                    suggestion.target_file.note = direct_edits
                        ? "Remove unused include confirmed by clang-tidy"
                        : "Review include manually before removing it";

                    suggestion.impact.total_files_affected = selected_diagnostics.size();
                    suggestion.impact.cumulative_savings = estimated_savings;
                    suggestion.caveats = {
                        "Diagnostics reflect the active compile_commands.json configuration",
                        "Cross-platform and feature-gated builds may still require these includes in other variants"
                    };
                    suggestion.verification = "Compile all supported targets after applying the edits";

                    if (direct_edits) {
                        suggestion.is_safe = true;
                        suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
                        for (const auto& diag : selected_diagnostics) {
                            suggestion.edits.push_back(make_delete_line_edit(diag.file, diag.line));
                        }
                        suggestion.implementation_steps = {
                            "Apply the explicit removals reported by clang-tidy misc-include-cleaner",
                            "Rebuild and run tests",
                            "Re-run clang-tidy to confirm the include-cleaner warnings are gone"
                        };
                    } else {
                        suggestion.is_safe = false;
                        suggestion.application_mode = SuggestionApplicationMode::Advisory;
                        suggestion.application_summary = "Manual review only";
                        suggestion.application_guidance =
                            "Re-check these includes in other platform or feature configurations before removing them. Only auto-remove includes that are unused in all supported variants.";
                        if (!advisory_reasons.empty()) {
                            std::sort(advisory_reasons.begin(), advisory_reasons.end());
                            advisory_reasons.erase(
                                std::unique(advisory_reasons.begin(), advisory_reasons.end()),
                                advisory_reasons.end()
                            );
                            std::ostringstream reason;
                            for (std::size_t i = 0; i < advisory_reasons.size(); ++i) {
                                if (i > 0) {
                                    reason << "; ";
                                }
                                reason << advisory_reasons[i];
                            }
                            suggestion.auto_apply_blocked_reason = reason.str();
                        }
                        suggestion.implementation_steps = {
                            "Verify the include across the project's supported platforms or feature sets",
                            "Remove it manually only if it stays unused in each relevant configuration",
                            "Rebuild and rerun clang-tidy for each supported variant"
                        };
                    }

                    result.suggestions.push_back(std::move(suggestion));
                };

                if (!safe_diagnostics.empty()) {
                    build_include_removal_suggestion(safe_diagnostics, true);
                }
                if (!advisory_diagnostics.empty()) {
                    build_include_removal_suggestion(advisory_diagnostics, false);
                }
            }
        }

        std::vector<fs::path> source_files;
        source_files.reserve(files.size());
        std::unordered_set<std::string> source_seen;
        for (const auto& file_result : files) {
            if (!is_source_file(file_result.file)) {
                continue;
            }
            fs::path resolved_source = resolve_project_path(file_result.file, context.project_root);
            const std::string key = resolved_source.generic_string();
            if (source_seen.insert(key).second) {
                source_files.push_back(std::move(resolved_source));
            }
        }

        std::vector<fs::path> candidate_headers;
        std::unordered_set<std::string> header_seen;
        std::unordered_map<std::string, std::vector<fs::path>> header_includers;

        for (const auto& file_result : files) {
            if (!is_header_file(file_result.file)) {
                continue;
            }
            fs::path header = resolve_project_path(file_result.file, context.project_root);
            const std::string key = header.generic_string();
            if (header_seen.insert(key).second) {
                candidate_headers.push_back(std::move(header));
            }
        }

        for (const auto& header_info : deps.headers) {
            if (!is_header_file(header_info.path)) {
                continue;
            }

            const fs::path header = resolve_project_path(header_info.path, context.project_root);
            const std::string header_key = header.generic_string();
            if (header_seen.insert(header_key).second) {
                candidate_headers.push_back(header);
            }

            auto& includers = header_includers[header_key];
            includers.reserve(includers.size() + header_info.included_by.size());
            for (const auto& includer : header_info.included_by) {
                fs::path resolved_includer = resolve_project_path(includer, context.project_root);
                if (!is_source_file(resolved_includer)) {
                    continue;
                }
                const std::string key = resolved_includer.generic_string();
                if (source_seen.insert(key).second) {
                    source_files.push_back(resolved_includer);
                }
                includers.push_back(std::move(resolved_includer));
            }
        }

        for (const auto& source_file : source_files) {
            if (!fs::exists(source_file)) {
                continue;
            }
            const auto source_includes = find_include_directives(source_file);
            for (const auto& include_dir : source_includes) {
                if (include_dir.is_system) {
                    continue;
                }
                const auto included_header = resolve_local_include_path(
                    include_dir.header_name,
                    source_file,
                    context.project_root
                );
                if (!included_header.has_value() || !is_header_file(*included_header)) {
                    continue;
                }
                const std::string key = included_header->generic_string();
                if (header_seen.insert(key).second) {
                    candidate_headers.push_back(*included_header);
                }
            }
        }

        std::unordered_set<std::string> emitted_move_keys;
        std::unordered_map<std::string, std::vector<std::string>> declared_symbol_cache;

        for (const auto& including_header : candidate_headers) {
            if (context.is_cancelled()) {
                break;
            }

            const bool header_selected = context.should_analyze(including_header);
            if (!header_selected && !context.target_files.empty()) {
                const auto includers_it = header_includers.find(including_header.generic_string());
                const bool includer_selected = includers_it != header_includers.end() &&
                    std::ranges::any_of(
                        includers_it->second,
                        [&](const fs::path& includer) {
                            return context.should_analyze(includer);
                        }
                    );
                if (!includer_selected) {
                    continue;
                }
            } else if (!header_selected) {
                continue;
            }
            ++analyzed;

            if (!fs::exists(including_header)) {
                ++skipped;
                continue;
            }

            const auto source_file = find_corresponding_source(including_header, context.project_root, source_files);
            if (!source_file.has_value() || !fs::exists(*source_file)) {
                ++skipped;
                continue;
            }

            const auto header_includes = find_include_directives(including_header);
            bool emitted_from_header = false;

            for (const auto& include_dir : header_includes) {
                if (include_dir.is_system) {
                    continue;
                }

                const auto* header_info = find_header_info(deps, include_dir.header_name);
                std::optional<fs::path> resolved_include_header;
                if (header_info != nullptr) {
                    resolved_include_header = resolve_project_path(header_info->path, context.project_root);
                } else {
                    resolved_include_header = resolve_local_include_path(
                        include_dir.header_name,
                        including_header,
                        context.project_root
                    );
                }

                if (!resolved_include_header.has_value()) {
                    continue;
                }
                const fs::path included_header = resolved_include_header->lexically_normal();
                if (is_likely_system_header(included_header)) {
                    continue;
                }
                if (!included_header.empty() && included_header == including_header) {
                    continue;
                }

                const std::string dedup_key = including_header.generic_string() + "|" + included_header.generic_string();
                if (!emitted_move_keys.insert(dedup_key).second) {
                    continue;
                }

                if (find_include_for_header(*source_file, include_dir.header_name).has_value()) {
                    continue;
                }
                if (find_include_for_header(*source_file, included_header.filename().string()).has_value()) {
                    continue;
                }

                const std::string cache_key = included_header.generic_string();
                auto cache_it = declared_symbol_cache.find(cache_key);
                if (cache_it == declared_symbol_cache.end()) {
                    cache_it = declared_symbol_cache.emplace(
                        cache_key,
                        fs::exists(included_header) ? extract_declared_type_names(included_header) : std::vector<std::string>{}
                    ).first;
                }

                const auto assessment = assess_move_to_cpp(including_header, cache_it->second);
                if (!assessment.mentions_symbol || !assessment.has_forward_decl || assessment.unsafe_usage) {
                    continue;
                }

                auto include_decl = find_include_for_header(including_header, include_dir.header_name);
                if (!include_decl.has_value()) {
                    include_decl = find_include_for_header(including_header, included_header.filename().string());
                }
                if (!include_decl.has_value()) {
                    continue;
                }

                Suggestion suggestion;
                suggestion.id = generate_suggestion_id("move", included_header, including_header.filename().string());
                suggestion.type = SuggestionType::MoveToCpp;
                const Duration include_parse_time = header_info != nullptr
                    ? header_info->total_parse_time
                    : Duration::zero();
                const std::size_t include_count = header_info != nullptr
                    ? std::max<std::size_t>(header_info->inclusion_count, 1)
                    : 1;
                suggestion.priority = include_parse_time > std::chrono::milliseconds(200)
                    ? Priority::Medium
                    : Priority::Low;
                suggestion.confidence = 0.82;
                suggestion.is_safe = true;
                suggestion.application_mode = SuggestionApplicationMode::DirectEdits;

                std::ostringstream title;
                title << "Move " << included_header.filename().string()
                      << " include from " << including_header.filename().string()
                      << " to " << source_file->filename().string();
                suggestion.title = title.str();

                std::ostringstream desc;
                desc << "Header '" << make_repo_relative(including_header)
                     << "' already forward-declares symbols from '"
                     << make_repo_relative(included_header)
                     << "' and only uses them in incomplete-type-safe contexts. "
                     << "Move the include to '" << make_repo_relative(*source_file)
                     << "' to reduce transitive dependencies.";
                suggestion.description = desc.str();

                suggestion.rationale =
                    "When a header only needs forward declarations, placing heavy includes in the .cpp "
                    "reduces incremental rebuild fanout while keeping translation units self-sufficient.";

                suggestion.estimated_savings = include_parse_time / include_count;
                if (context.trace.total_time.count() > 0) {
                    suggestion.estimated_savings_percent =
                        100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                        static_cast<double>(context.trace.total_time.count());
                }

                suggestion.target_file.path = including_header;
                suggestion.target_file.action = FileAction::Modify;
                suggestion.target_file.line_start = include_decl->line + 1;
                suggestion.target_file.line_end = include_decl->line + 1;
                suggestion.target_file.note = "Remove header include after confirming forward declaration coverage";

                suggestion.edits.push_back(make_delete_line_edit(including_header, include_decl->line));

                const std::string include_line = "#include \"" + include_dir.header_name + "\"";
                if (auto insert_line = find_include_insertion_line(*source_file)) {
                    suggestion.edits.push_back(make_insert_after_line_edit(
                        *source_file,
                        *insert_line,
                        include_line
                    ));

                    FileTarget cpp_target;
                    cpp_target.path = *source_file;
                    cpp_target.action = FileAction::AddInclude;
                    cpp_target.line_start = *insert_line + 2;
                    cpp_target.line_end = *insert_line + 2;
                    cpp_target.note = "Add moved include to source file";
                    suggestion.secondary_files.push_back(std::move(cpp_target));
                } else {
                    suggestion.edits.push_back(make_insert_at_start_edit(*source_file, include_line));

                    FileTarget cpp_target;
                    cpp_target.path = *source_file;
                    cpp_target.action = FileAction::AddInclude;
                    cpp_target.line_start = 1;
                    cpp_target.line_end = 1;
                    cpp_target.note = "Add moved include to source file";
                    suggestion.secondary_files.push_back(std::move(cpp_target));
                }

                suggestion.impact.total_files_affected = 2;
                suggestion.impact.cumulative_savings = suggestion.estimated_savings;
                suggestion.implementation_steps = {
                    "Remove the include from the header",
                    "Add the include to the corresponding source file",
                    "Rebuild to verify the header still compiles with forward declarations only"
                };
                suggestion.caveats = {
                    "Do not move includes required by inline definitions or templates in the header",
                    "Re-verify after API changes in the moved header"
                };
                suggestion.verification = "Run a full rebuild and unit tests after applying the edit";

                result.suggestions.push_back(std::move(suggestion));
                emitted_from_header = true;
            }

            if (!emitted_from_header) {
                ++skipped;
            }
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

    void register_include_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<IncludeSuggester>()
        );
    }
}  // namespace bha::suggestions
