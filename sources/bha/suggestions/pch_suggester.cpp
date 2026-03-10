//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/pch_suggester.hpp"
#include "bha/suggestions/unreal_context.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bha::suggestions
{
    namespace {

        struct CMakeTargetInfo {
            std::string name;
            std::size_t line = 0;
            std::size_t insert_after_line = 0;
            bool is_macro = false;
        };

        struct MesonTargetSpan {
            std::size_t start_line = 0;
            std::size_t end_line = 0;
            bool single_line = false;
            bool has_pch = false;
        };

        struct CMakeCommandStart {
            std::string name;
            std::size_t open_pos = 0;
        };

        std::optional<int> parse_cpp_standard(std::string_view token) {
            constexpr std::string_view kPrefix = "-std=";
            if (!token.starts_with(kPrefix)) {
                return std::nullopt;
            }
            const std::string_view value = token.substr(kPrefix.size());
            if (value == "c++11" || value == "gnu++11") return 11;
            if (value == "c++14" || value == "gnu++14") return 14;
            if (value == "c++17" || value == "gnu++17") return 17;
            if (value == "c++20" || value == "gnu++20" || value == "c++2a" || value == "gnu++2a") return 20;
            if (value == "c++23" || value == "gnu++23" || value == "c++2b" || value == "gnu++2b") return 23;
            return std::nullopt;
        }

        std::optional<int> find_min_cpp_standard(const BuildTrace& trace) {
            std::optional<int> best;
            for (const auto& unit : trace.units) {
                for (const auto& arg : unit.command_line) {
                    if (auto standard = parse_cpp_standard(arg)) {
                        if (!best || *standard < *best) {
                            best = *standard;
                        }
                    }
                }
            }
            return best;
        }

        bool is_cxx17_only_header(const fs::path& path) {
            std::string name = path.filename().string();
            std::ranges::transform(name, name.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name == "filesystem" || name == "string_view" || name == "optional" ||
                name == "variant" || name == "any" || name == "charconv" ||
                name == "memory_resource" || name == "execution" || name == "shared_mutex") {
                return true;
            }
            if (name.rfind("fs_", 0) == 0) {
                return true;
            }
            std::string lower = path.generic_string();
            std::ranges::transform(lower, lower.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("/bits/fs_") != std::string::npos) {
                return true;
            }
            return false;
        }

        bool is_unstable_external_header(const fs::path& path) {
            std::string lower = path.generic_string();
            std::ranges::transform(lower, lower.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("/bits/") != std::string::npos) {
                return true;
            }
            if (lower.find("/c++/") != std::string::npos) {
                return true;
            }
            const std::string ext = path.extension().string();
            if (ext == ".tcc" || ext == ".inc" || ext == ".inl" || ext == ".ipp" || ext == ".def") {
                return true;
            }
            return false;
        }

        bool has_header_extension(const fs::path& path) {
            std::string ext = path.extension().string();
            std::ranges::transform(ext, ext.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx";
        }

        bool is_non_header_artifact(const fs::path& path) {
            std::string lower = path.generic_string();
            std::ranges::transform(lower, lower.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("cmakelists.txt") != std::string::npos) {
                return true;
            }
            const std::string ext = path.extension().string();
            if (ext == ".txt" || ext == ".cmake" || ext == ".mk") {
                return true;
            }
            return false;
        }

        std::optional<CMakeCommandStart> parse_cmake_command_start(std::string_view line) {
            if (line.empty()) {
                return std::nullopt;
            }
            const unsigned char first = static_cast<unsigned char>(line.front());
            if (!(std::isalpha(first) || line.front() == '_')) {
                return std::nullopt;
            }
            std::size_t i = 1;
            while (i < line.size()) {
                const unsigned char ch = static_cast<unsigned char>(line[i]);
                if (std::isalnum(ch) || line[i] == '_') {
                    ++i;
                    continue;
                }
                break;
            }
            if (i == 0) {
                return std::nullopt;
            }
            std::size_t j = i;
            while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) {
                ++j;
            }
            if (j >= line.size() || line[j] != '(') {
                return std::nullopt;
            }
            return CMakeCommandStart{std::string(line.substr(0, i)), j};
        }

        int count_paren_delta_outside_quotes(std::string_view text) {
            int delta = 0;
            bool in_quote = false;
            char quote = '\0';
            for (const char c : text) {
                if (in_quote) {
                    if (c == quote) {
                        in_quote = false;
                    }
                    continue;
                }
                if (c == '"' || c == '\'') {
                    in_quote = true;
                    quote = c;
                    continue;
                }
                if (c == '(') {
                    ++delta;
                } else if (c == ')') {
                    --delta;
                }
            }
            return delta;
        }

        std::optional<std::size_t> find_cmake_block_end(
            const std::string& content,
            const std::size_t start_line
        ) {
            std::istringstream input(content);
            std::string line;
            std::vector<std::string> lines;
            while (std::getline(input, line)) {
                lines.push_back(line);
            }
            if (start_line >= lines.size()) {
                return std::nullopt;
            }
            int paren_depth = 0;
            bool seen_open = false;
            for (std::size_t i = start_line; i < lines.size(); ++i) {
                const int delta = count_paren_delta_outside_quotes(lines[i]);
                if (delta > 0) {
                    seen_open = true;
                }
                paren_depth += delta;
                if (seen_open && paren_depth <= 0) {
                    return i;
                }
            }
            return std::nullopt;
        }

        std::optional<std::string> find_project_name(const std::string& content) {
            std::regex project_regex(R"(^\s*project\s*\(\s*([A-Za-z0-9_\-\.]+))", std::regex::icase);
            std::istringstream input(content);
            std::string line;
            while (std::getline(input, line)) {
                std::smatch match;
                if (std::regex_search(line, match, project_regex) && match.size() >= 2) {
                    return match[1].str();
                }
            }
            return std::nullopt;
        }

        bool is_cmake_target_candidate(const std::string& name, const std::string& line) {
            if (name.find("::") != std::string::npos) {
                return false;
            }
            std::string lower_line = line;
            std::ranges::transform(lower_line, lower_line.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower_line.find("alias") != std::string::npos || lower_line.find("imported") != std::string::npos) {
                return false;
            }
            std::string lower_name = name;
            std::ranges::transform(lower_name, lower_name.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower_name.find("gtest") != std::string::npos || lower_name.find("gmock") != std::string::npos) {
                return false;
            }
            if (lower_name.find("test") != std::string::npos ||
                lower_name.find("benchmark") != std::string::npos ||
                lower_name.find("bench") != std::string::npos ||
                lower_name.find("mock") != std::string::npos) {
                return false;
            }
            return true;
        }

        bool is_probable_cmake_target_name(std::string_view name) {
            if (name.empty()) {
                return false;
            }
            if (name.find('$') != std::string::npos ||
                name.find('<') != std::string::npos ||
                name.find('>') != std::string::npos) {
                return false;
            }
            if (name.front() == '-') {
                return false;
            }
            for (const char c : name) {
                const unsigned char ch = static_cast<unsigned char>(c);
                if (std::isalnum(ch) != 0 || c == '_' || c == '-' || c == '.') {
                    continue;
                }
                return false;
            }
            return true;
        }

        std::optional<CMakeTargetInfo> find_first_cmake_target(const std::string& content) {
            std::regex target_regex(R"(^\s*add_(executable|library)\s*\(\s*([A-Za-z0-9_\-\.]+))",
                                    std::regex::icase);

            const auto project_name = find_project_name(content);
            std::vector<CMakeTargetInfo> candidates;

            std::istringstream input(content);
            std::string line;
            std::size_t line_num = 0;
            while (std::getline(input, line)) {
                std::smatch match;
                if (std::regex_search(line, match, target_regex) && match.size() >= 3) {
                    const std::string name = match[2].str();
                    if (is_cmake_target_candidate(name, line)) {
                        candidates.push_back(CMakeTargetInfo{name, line_num, line_num, false});
                    }
                }
                ++line_num;
            }

            if (candidates.empty()) {
                return std::nullopt;
            }
            if (project_name) {
                for (const auto& candidate : candidates) {
                    if (candidate.name == *project_name ||
                        candidate.name.find(*project_name) == 0) {
                        return candidate;
                    }
                }
            }
            return candidates.front();
        }

        std::optional<MesonTargetSpan> find_first_meson_target(const std::string& content) {
            std::regex target_regex(R"(^\s*(executable|library|shared_library|static_library)\s*\()",
                                    std::regex::icase);

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
                    if (lines[i][j] == '(') ++paren_depth;
                    if (lines[i][j] == ')') --paren_depth;
                }

                MesonTargetSpan span;
                span.start_line = i;

                if (paren_depth <= 0) {
                    span.end_line = i;
                    span.single_line = true;
                } else {
                    for (std::size_t k = i + 1; k < lines.size(); ++k) {
                        for (char c : lines[k]) {
                            if (c == '(') ++paren_depth;
                            if (c == ')') --paren_depth;
                        }
                        if (paren_depth <= 0) {
                            span.end_line = k;
                            break;
                        }
                    }
                }

                const std::size_t end = std::min(span.end_line, lines.size() - 1);
                for (std::size_t k = span.start_line; k <= end; ++k) {
                    if (lines[k].find("cpp_pch") != std::string::npos ||
                        lines[k].find("c_pch") != std::string::npos) {
                        span.has_pch = true;
                        break;
                    }
                }

                return span;
            }

            return std::nullopt;
        }

        std::optional<std::string> parse_cmake_target_from_line(const std::string& line) {
            auto open = line.find('(');
            if (open == std::string::npos) {
                return std::nullopt;
            }
            std::size_t pos = open + 1;
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
                ++pos;
            }
            if (pos >= line.size()) {
                return std::nullopt;
            }
            if (line[pos] == '"' || line[pos] == '\'') {
                const char quote = line[pos++];
                const auto end = line.find(quote, pos);
                if (end == std::string::npos) {
                    return std::nullopt;
                }
                return line.substr(pos, end - pos);
            }
            const auto end = line.find_first_of(" \t\r\n)", pos);
            if (end == std::string::npos) {
                return line.substr(pos);
            }
            return line.substr(pos, end - pos);
        }

        bool has_cmake_pch_for_target(const std::string& content, const std::string& target) {
            std::istringstream input(content);
            std::string line;
            while (std::getline(input, line)) {
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                if (trimmed.rfind("target_precompile_headers", 0) != 0) {
                    continue;
                }
                if (auto parsed = parse_cmake_target_from_line(trimmed)) {
                    if (*parsed == target) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool is_excluded_cmake_path(const std::string& lower_path) {
            return lower_path.find("/test") != std::string::npos ||
                   lower_path.find("/tests") != std::string::npos ||
                   lower_path.find("example") != std::string::npos ||
                   lower_path.find("benchmark") != std::string::npos ||
                   lower_path.find("install_test") != std::string::npos ||
                   lower_path.find("/cmake/") != std::string::npos;
        }

        int score_cmake_path(const std::string& lower_path) {
            int score = 0;
            if (lower_path.find("/src/") != std::string::npos) {
                score += 20;
            }
            if (lower_path.find("/lib/") != std::string::npos) {
                score += 10;
            }
            score -= static_cast<int>(lower_path.size() / 10);
            return score;
        }

        std::vector<CMakeTargetInfo> find_macro_targets(const std::string& content);

        std::optional<std::pair<fs::path, CMakeTargetInfo>> find_cmake_target_in_tree(
            const fs::path& project_root,
            const std::function<bool()>& should_cancel
        ) {
            if (project_root.empty() || !fs::exists(project_root)) {
                return std::nullopt;
            }

            struct ScoredTarget {
                fs::path path;
                CMakeTargetInfo target;
                int score = 0;
            };
            std::vector<ScoredTarget> candidates;

            for (const auto& entry : fs::recursive_directory_iterator(project_root)) {
                if (should_cancel && should_cancel()) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (entry.path().filename() != "CMakeLists.txt") {
                    continue;
                }
                const auto rel = entry.path().lexically_relative(project_root);
                if (rel.empty()) {
                    continue;
                }
                std::string rel_str = rel.generic_string();
                std::string lower_rel = rel_str;
                std::ranges::transform(lower_rel, lower_rel.begin(),
                                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower_rel.find("build") != std::string::npos ||
                    lower_rel.find(".git") != std::string::npos) {
                    continue;
                }
                if (is_excluded_cmake_path(lower_rel)) {
                    continue;
                }

                std::ifstream in(entry.path());
                if (!in) {
                    continue;
                }
                std::string content((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
                std::vector<CMakeTargetInfo> file_targets = find_macro_targets(content);
                if (file_targets.empty()) {
                    if (auto target = find_first_cmake_target(content)) {
                        file_targets.push_back(*target);
                    }
                }

                for (const auto& target : file_targets) {
                    const int score = score_cmake_path(lower_rel);
                    candidates.push_back(ScoredTarget{entry.path(), target, score});
                }
            }
            if (candidates.empty()) {
                return std::nullopt;
            }
            std::ranges::sort(candidates, [](const ScoredTarget& a, const ScoredTarget& b) {
                if (a.score != b.score) {
                    return a.score > b.score;
                }
                const auto a_path = a.path.generic_string();
                const auto b_path = b.path.generic_string();
                if (a_path != b_path) {
                    return a_path < b_path;
                }
                if (a.target.name != b.target.name) {
                    return a.target.name < b.target.name;
                }
                return a.target.line < b.target.line;
            });
            return std::make_pair(candidates.front().path, candidates.front().target);
        }

        bool has_cmake_pch_config_in_tree(
            const fs::path& project_root,
            const std::function<bool()>& should_cancel
        ) {
            if (project_root.empty() || !fs::exists(project_root)) {
                return false;
            }
            for (const auto& entry : fs::recursive_directory_iterator(project_root)) {
                if (should_cancel && should_cancel()) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (entry.path().filename() != "CMakeLists.txt") {
                    continue;
                }
                std::ifstream in(entry.path());
                if (!in) {
                    continue;
                }
                std::string content((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
                if (content.find("target_precompile_headers") != std::string::npos) {
                    return true;
                }
            }
            return false;
        }

        CMakeTargetInfo pick_deterministic_target(std::vector<CMakeTargetInfo> targets) {
            std::ranges::sort(targets, [](const CMakeTargetInfo& a, const CMakeTargetInfo& b) {
                if (a.name != b.name) {
                    return a.name < b.name;
                }
                return a.line < b.line;
            });
            return targets.front();
        }

        bool has_qmake_pch_config(const std::string& content) {
            return content.find("PRECOMPILED_HEADER") != std::string::npos ||
                   content.find("precompile_header") != std::string::npos;
        }

        bool has_scons_pch_config(const std::string& content) {
            return content.find("env.PCH") != std::string::npos ||
                   content.find("PCH(") != std::string::npos ||
                   content.find("['PCH']") != std::string::npos ||
                   content.find("[\"PCH\"]") != std::string::npos;
        }

        bool has_make_pch_config(const std::string& content) {
            return content.find(" -include ") != std::string::npos ||
                   content.find("-include ") != std::string::npos ||
                   content.find("PRECOMPILED_HEADER") != std::string::npos ||
                   content.find("PCH") != std::string::npos;
        }

        bool is_cmake_builtin_target_command(const std::string& name) {
            std::string lower = name;
            std::ranges::transform(lower, lower.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            static const std::unordered_set<std::string> kBuiltins = {
                "add_dependencies",
                "add_executable",
                "add_library",
                "export",
                "install",
                "link_libraries",
                "set_property",
                "set_target_properties",
                "target_compile_definitions",
                "target_compile_options",
                "target_include_directories",
                "target_link_libraries",
                "target_link_options",
                "target_precompile_headers",
                "target_sources"
            };
            return kBuiltins.contains(lower);
        }

        bool macro_name_is_target_like(const std::string& name) {
            if (is_cmake_builtin_target_command(name)) {
                return false;
            }
            std::string lower = name;
            std::ranges::transform(lower, lower.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("library") != std::string::npos ||
                lower.find("executable") != std::string::npos ||
                lower.find("binary") != std::string::npos ||
                lower.find("target") != std::string::npos) {
                return true;
            }
            return false;
        }

        std::vector<std::string> tokenize_cmake_args(std::string_view args) {
            std::vector<std::string> tokens;
            std::string current;
            bool in_quote = false;
            char quote = '\0';

            auto flush = [&]() {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            };

            for (std::size_t i = 0; i < args.size(); ++i) {
                const char c = args[i];
                if (in_quote) {
                    if (c == quote) {
                        in_quote = false;
                    } else {
                        current.push_back(c);
                    }
                    continue;
                }
                if (c == '"' || c == '\'') {
                    in_quote = true;
                    quote = c;
                    continue;
                }
                if (std::isspace(static_cast<unsigned char>(c)) || c == ';') {
                    flush();
                    continue;
                }
                current.push_back(c);
            }
            flush();
            return tokens;
        }

        bool is_macro_keyword(const std::string& token) {
            std::string key = token;
            std::ranges::transform(key, key.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
            static const std::unordered_set<std::string> kKeywords = {
                "NAME",
                "HDRS",
                "SRCS",
                "SOURCES",
                "SRC",
                "SOURCE",
                "COPTS",
                "DEFINES",
                "LINKOPTS",
                "DEPS",
                "PUBLIC",
                "PRIVATE",
                "INTERFACE",
                "TEXTUAL_HDRS",
                "TESTONLY",
                "DISABLE_INSTALL"
            };
            return kKeywords.contains(key);
        }

        bool is_probable_source_token(const std::string& token) {
            if (token.empty()) {
                return false;
            }
            if (token.front() == '$') {
                return false;
            }
            static const std::array<std::string, 8> kExts = {
                ".c", ".cc", ".cpp", ".cxx", ".mm", ".m", ".ixx", ".cu"
            };
            for (const auto& ext : kExts) {
                if (token.size() >= ext.size() &&
                    token.compare(token.size() - ext.size(), ext.size(), ext) == 0) {
                    return true;
                }
            }
            return false;
        }

        bool macro_args_have_sources(const std::vector<std::string>& tokens) {
            for (std::size_t i = 0; i < tokens.size(); ++i) {
                std::string key = tokens[i];
                std::ranges::transform(key, key.begin(),
                                       [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
                if (key != "SRCS" && key != "SOURCES" && key != "SRC" && key != "SOURCE") {
                    continue;
                }
                for (std::size_t j = i + 1; j < tokens.size(); ++j) {
                    if (is_macro_keyword(tokens[j])) {
                        break;
                    }
                    if (is_probable_source_token(tokens[j])) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool macro_args_has_testonly(const std::vector<std::string>& tokens) {
            for (const auto& token : tokens) {
                std::string key = token;
                std::ranges::transform(key, key.begin(),
                                       [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
                if (key == "TESTONLY") {
                    return true;
                }
            }
            return false;
        }

        std::optional<std::string> extract_macro_target_name(std::string_view args) {
            const auto tokens = tokenize_cmake_args(args);
            if (tokens.empty()) {
                return std::nullopt;
            }
            for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
                std::string key = tokens[i];
                std::ranges::transform(key, key.begin(),
                                       [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
                if (key == "NAME") {
                    if (is_probable_cmake_target_name(tokens[i + 1])) {
                        return tokens[i + 1];
                    }
                    return std::nullopt;
                }
            }
            if (is_probable_cmake_target_name(tokens.front())) {
                return tokens.front();
            }
            return std::nullopt;
        }

        std::vector<CMakeTargetInfo> find_macro_targets(const std::string& content) {
            std::vector<CMakeTargetInfo> results;
            std::istringstream input(content);
            std::string line;
            std::size_t line_num = 0;
            std::string pending;
            std::size_t pending_line = 0;
            int paren_depth = 0;
            bool collecting = false;

            while (std::getline(input, line)) {
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                if (trimmed.empty() || trimmed.rfind("#", 0) == 0) {
                    ++line_num;
                    continue;
                }

                if (!collecting) {
                    const auto start = parse_cmake_command_start(trimmed);
                    if (!start) {
                        ++line_num;
                        continue;
                    }
                    if (!macro_name_is_target_like(start->name)) {
                        ++line_num;
                        continue;
                    }
                    pending = trimmed;
                    pending_line = line_num;
                    collecting = true;
                    paren_depth = 0;
                } else {
                    pending += " " + trimmed;
                }

                paren_depth += count_paren_delta_outside_quotes(trimmed);
                if (collecting && paren_depth <= 0) {
                    const auto open = pending.find('(');
                    const auto close = pending.rfind(')');
                    if (open != std::string::npos && close != std::string::npos && close > open) {
                        std::string name = pending.substr(0, open);
                        name.erase(0, name.find_first_not_of(" \t"));
                        name.erase(name.find_last_not_of(" \t") + 1);
                std::string args = pending.substr(open + 1, close - open - 1);
                const auto tokens = tokenize_cmake_args(args);
                if (macro_args_have_sources(tokens) && !macro_args_has_testonly(tokens)) {
                    if (auto target = extract_macro_target_name(args)) {
                        if (is_cmake_target_candidate(*target, pending)) {
                            results.push_back(CMakeTargetInfo{*target, pending_line, line_num, true});
                        }
                    }
                }
            }
                    collecting = false;
                    pending.clear();
                }

                ++line_num;
            }

            return results;
        }

        bool has_ninja_pch_config(const std::string& content) {
            return content.find("pch.h.gch") != std::string::npos ||
                   content.find("rule pch") != std::string::npos ||
                   content.find("-include") != std::string::npos;
        }

        bool has_buck2_pch_config(const std::string& content) {
            return content.find("precompiled_header") != std::string::npos ||
                   content.find("cxx_precompiled_header(") != std::string::npos;
        }

        BuildSystemType detect_active_build_system(
            const BuildTrace& trace,
            const fs::path& project_root
        ) {
            if (trace.build_system != BuildSystemType::Unknown) {
                return trace.build_system;
            }
            if (project_root.empty()) {
                return BuildSystemType::Unknown;
            }
            if (fs::exists(project_root / "CMakeLists.txt")) return BuildSystemType::CMake;
            if (fs::exists(project_root / "meson.build")) return BuildSystemType::Meson;
            if (fs::exists(project_root / "Makefile") || fs::exists(project_root / "makefile")) {
                return BuildSystemType::Make;
            }
            if (fs::exists(project_root / "SConstruct")) return BuildSystemType::SCons;
            if (fs::exists(project_root / "BUILD") || fs::exists(project_root / "BUILD.bazel")) {
                return BuildSystemType::Bazel;
            }
            if (fs::exists(project_root / "BUCK") || fs::exists(project_root / "BUCK2")) {
                return BuildSystemType::Buck2;
            }
            for (const auto& entry : fs::directory_iterator(project_root)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (entry.path().extension() == ".vcxproj") {
                    return BuildSystemType::MSBuild;
                }
            }
            for (const auto& entry : fs::directory_iterator(project_root)) {
                if (!entry.is_directory()) {
                    continue;
                }
                if (entry.path().extension() == ".xcodeproj") {
                    return BuildSystemType::XCode;
                }
            }
            return BuildSystemType::Unknown;
        }

        bool has_qmake_project_file(const fs::path& project_root) {
            if (project_root.empty() || !fs::exists(project_root)) {
                return false;
            }
            for (const auto& entry : fs::directory_iterator(project_root)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (entry.path().extension() == ".pro") {
                    return true;
                }
            }
            return false;
        }

        std::optional<std::string> extract_cmake_target_from_output_path(std::string output) {
            std::replace(output.begin(), output.end(), '\\', '/');
            const std::string marker = "CMakeFiles/";
            const auto marker_pos = output.find(marker);
            if (marker_pos == std::string::npos) {
                return std::nullopt;
            }
            const std::size_t start = marker_pos + marker.size();
            const auto end = output.find(".dir/", start);
            if (end == std::string::npos || end <= start) {
                return std::nullopt;
            }
            const std::string target = output.substr(start, end - start);
            if (!target.empty()) {
                return target;
            }
            return std::nullopt;
        }

        std::optional<std::string> extract_cmake_target_from_command_line(
            const std::vector<std::string>& command_line
        ) {
            for (std::size_t i = 0; i < command_line.size(); ++i) {
                const std::string& token = command_line[i];
                if (token == "-o" && i + 1 < command_line.size()) {
                    if (auto target = extract_cmake_target_from_output_path(command_line[i + 1])) {
                        return target;
                    }
                    continue;
                }
                if (token.rfind("-o", 0) == 0 && token.size() > 2) {
                    if (auto target = extract_cmake_target_from_output_path(token.substr(2))) {
                        return target;
                    }
                }
                if (auto target = extract_cmake_target_from_output_path(token)) {
                    return target;
                }
            }
            return std::nullopt;
        }

        fs::path normalize_source_path_for_lookup(const fs::path& source, const fs::path& project_root) {
            fs::path resolved = resolve_source_path(source);
            if (resolved.is_relative() && !project_root.empty()) {
                resolved = (project_root / resolved).lexically_normal();
            }
            return resolved.lexically_normal();
        }

        std::vector<std::string> collect_cmake_target_hints_for_header(
            const SuggestionContext& context,
            const analyzers::DependencyAnalysisResult::HeaderInfo& header,
            const fs::path& project_root
        ) {
            std::unordered_set<std::string> includer_paths;
            std::unordered_set<std::string> includer_names;
            includer_paths.reserve(header.included_by.size());
            includer_names.reserve(header.included_by.size());
            for (const auto& includer : header.included_by) {
                const fs::path normalized = normalize_source_path_for_lookup(includer, project_root);
                includer_paths.insert(normalized.generic_string());
                includer_names.insert(normalized.filename().string());
            }

            std::unordered_map<std::string, std::size_t> target_counts;
            for (const auto& unit : context.trace.units) {
                const fs::path normalized = normalize_source_path_for_lookup(unit.source_file, project_root);
                const bool matches_by_path = includer_paths.contains(normalized.generic_string());
                const bool matches_by_name = includer_names.contains(normalized.filename().string());
                if (!matches_by_path && !matches_by_name) {
                    continue;
                }
                if (auto target = extract_cmake_target_from_command_line(unit.command_line)) {
                    ++target_counts[*target];
                }
            }

            if (target_counts.empty()) {
                return {};
            }

            std::vector<std::pair<std::string, std::size_t>> ranked;
            ranked.reserve(target_counts.size());
            for (const auto& [name, count] : target_counts) {
                ranked.emplace_back(name, count);
            }
            std::ranges::sort(ranked, [](const auto& lhs, const auto& rhs) {
                if (lhs.second != rhs.second) {
                    return lhs.second > rhs.second;
                }
                return lhs.first < rhs.first;
            });

            std::vector<std::string> hints;
            hints.reserve(ranked.size());
            for (const auto& [name, _] : ranked) {
                hints.push_back(name);
            }
            return hints;
        }

        std::vector<CMakeTargetInfo> find_direct_cmake_targets(const std::string& content) {
            std::regex target_regex(R"(^\s*add_(executable|library)\s*\(\s*([A-Za-z0-9_\-\.]+))",
                                    std::regex::icase);
            std::vector<CMakeTargetInfo> targets;

            std::istringstream input(content);
            std::string line;
            std::size_t line_num = 0;
            while (std::getline(input, line)) {
                std::smatch match;
                if (std::regex_search(line, match, target_regex) && match.size() >= 3) {
                    const std::string name = match[2].str();
                    if (is_cmake_target_candidate(name, line)) {
                        targets.push_back(CMakeTargetInfo{name, line_num, line_num, false});
                    }
                }
                ++line_num;
            }
            return targets;
        }

        std::optional<std::pair<fs::path, CMakeTargetInfo>> find_named_cmake_target_in_tree(
            const fs::path& project_root,
            const std::string& target_name,
            const std::function<bool()>& should_cancel
        ) {
            if (project_root.empty() || target_name.empty() || !fs::exists(project_root)) {
                return std::nullopt;
            }

            struct ScoredTarget {
                fs::path path;
                CMakeTargetInfo target;
                int score = 0;
            };
            std::vector<ScoredTarget> matches;

            for (const auto& entry : fs::recursive_directory_iterator(project_root)) {
                if (should_cancel && should_cancel()) {
                    break;
                }
                if (!entry.is_regular_file() || entry.path().filename() != "CMakeLists.txt") {
                    continue;
                }
                const auto rel = entry.path().lexically_relative(project_root);
                if (rel.empty()) {
                    continue;
                }
                std::string rel_str = rel.generic_string();
                std::string lower_rel = rel_str;
                std::ranges::transform(lower_rel, lower_rel.begin(),
                                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower_rel.find("build") != std::string::npos ||
                    lower_rel.find(".git") != std::string::npos ||
                    is_excluded_cmake_path(lower_rel)) {
                    continue;
                }

                std::ifstream in(entry.path());
                if (!in) {
                    continue;
                }
                std::string content((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
                auto macro_targets = find_macro_targets(content);
                auto direct_targets = find_direct_cmake_targets(content);
                macro_targets.insert(macro_targets.end(), direct_targets.begin(), direct_targets.end());
                for (const auto& target : macro_targets) {
                    if (target.name == target_name) {
                        matches.push_back(ScoredTarget{entry.path(), target, score_cmake_path(lower_rel)});
                    }
                }
            }

            if (matches.empty()) {
                return std::nullopt;
            }
            std::ranges::sort(matches, [](const ScoredTarget& a, const ScoredTarget& b) {
                if (a.score != b.score) {
                    return a.score > b.score;
                }
                const auto a_path = a.path.generic_string();
                const auto b_path = b.path.generic_string();
                if (a_path != b_path) {
                    return a_path < b_path;
                }
                return a.target.line < b.target.line;
            });
            return std::make_pair(matches.front().path, matches.front().target);
        }

        std::optional<std::size_t> find_first_line_matching(
            const std::string& content,
            const std::regex& pattern
        ) {
            std::istringstream input(content);
            std::string line;
            std::size_t line_num = 0;
            while (std::getline(input, line)) {
                if (std::regex_search(line, pattern)) {
                    return line_num;
                }
                ++line_num;
            }
            return std::nullopt;
        }

        struct RuleSpan {
            std::size_t start_line = 0;
            std::size_t end_line = 0;
            bool has_key = false;
        };

        std::optional<RuleSpan> find_bazel_rule_span(const std::string& content) {
            std::regex rule_regex(R"(^\s*(cc_library|cc_binary|cc_test)\s*\()",
                                  std::regex::icase);
            std::istringstream input(content);
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(input, line)) {
                lines.push_back(line);
            }

            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (!std::regex_search(lines[i], rule_regex)) {
                    continue;
                }

                RuleSpan span;
                span.start_line = i;
                int paren_depth = 0;
                for (std::size_t j = lines[i].find('('); j < lines[i].size(); ++j) {
                    if (lines[i][j] == '(') ++paren_depth;
                    if (lines[i][j] == ')') --paren_depth;
                }

                for (std::size_t k = i + 1; k < lines.size() && paren_depth > 0; ++k) {
                    for (char c : lines[k]) {
                        if (c == '(') ++paren_depth;
                        if (c == ')') --paren_depth;
                    }
                    if (paren_depth <= 0) {
                        span.end_line = k;
                        break;
                    }
                }

                if (span.end_line == 0) {
                    span.end_line = i;
                }

                for (std::size_t k = span.start_line; k <= span.end_line && k < lines.size(); ++k) {
                    if (lines[k].find("copts") != std::string::npos) {
                        span.has_key = true;
                        break;
                    }
                }

                return span;
            }

            return std::nullopt;
        }

        std::optional<RuleSpan> find_buck2_rule_span(const std::string& content) {
            std::regex rule_regex(R"(^\s*(cxx_library|cxx_binary|cxx_test)\s*\()",
                                  std::regex::icase);
            std::istringstream input(content);
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(input, line)) {
                lines.push_back(line);
            }

            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (!std::regex_search(lines[i], rule_regex)) {
                    continue;
                }

                RuleSpan span;
                span.start_line = i;
                int paren_depth = 0;
                for (std::size_t j = lines[i].find('('); j < lines[i].size(); ++j) {
                    if (lines[i][j] == '(') ++paren_depth;
                    if (lines[i][j] == ')') --paren_depth;
                }

                for (std::size_t k = i + 1; k < lines.size() && paren_depth > 0; ++k) {
                    for (char c : lines[k]) {
                        if (c == '(') ++paren_depth;
                        if (c == ')') --paren_depth;
                    }
                    if (paren_depth <= 0) {
                        span.end_line = k;
                        break;
                    }
                }

                if (span.end_line == 0) {
                    span.end_line = i;
                }

                for (std::size_t k = span.start_line; k <= span.end_line && k < lines.size(); ++k) {
                    if (lines[k].find("compiler_flags") != std::string::npos ||
                        lines[k].find("cxx_compiler_flags") != std::string::npos ||
                        lines[k].find("cxx_flags") != std::string::npos) {
                        span.has_key = true;
                        break;
                    }
                }

                return span;
            }

            return std::nullopt;
        }

        Priority calculate_priority(
            const analyzers::DependencyAnalysisResult::HeaderInfo& header,
            const Duration total_build_time,
            const heuristics::PCHConfig& config
        ) {
            double time_ratio = 0.0;
            if (total_build_time.count() > 0) {
                time_ratio = static_cast<double>(header.total_parse_time.count()) /
                             static_cast<double>(total_build_time.count());
            }

            if (header.inclusion_count >= config.priority.critical_includes &&
                time_ratio > config.priority.critical_time_ratio) {
                return Priority::Critical;
                }
            if (header.inclusion_count >= config.priority.high_includes &&
                time_ratio > config.priority.high_time_ratio) {
                return Priority::High;
                }
            if (header.inclusion_count >= config.min_include_count) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        /**
         * Estimates savings from adding header to PCH.
         *
         * Model:
         * - Original: N * parse_time
         * - With PCH: 1 * parse_time + N * load_time (load_time ≈ 15% of parse_time)
         * - Savings = (N-1) * parse_time * 0.85
         */
        Duration estimate_pch_savings(
            const Duration total_parse_time,
            const std::size_t inclusion_count
        ) {
            if (inclusion_count <= 1) {
                return Duration::zero();
            }

            const Duration per_unit = total_parse_time / inclusion_count;

            constexpr double load_overhead = 0.15;
            constexpr double effective_savings = 1.0 - load_overhead;

            const auto savings_ns = static_cast<Duration::rep>(
                static_cast<double>(per_unit.count()) *
                static_cast<double>(inclusion_count - 1) *
                effective_savings
            );

            return Duration(savings_ns);
        }

        std::optional<Suggestion> build_unreal_module_pch_suggestion(const SuggestionContext& context) {
            if (!context.options.heuristics.unreal.emit_pch) {
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
                if (module.stats.include_parse_time < context.options.heuristics.unreal.min_module_include_time_for_pch) {
                    continue;
                }

                const bool already_explicit =
                    module.rules.pch_usage == UnrealPCHUsageMode::UseExplicitOrSharedPCHs ||
                    module.rules.pch_usage == UnrealPCHUsageMode::UseSharedPCHs;
                if (already_explicit) {
                    continue;
                }
                candidates.push_back(module);
            }

            if (candidates.empty()) {
                return std::nullopt;
            }

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("unreal-pch", candidates.front().rules.build_cs_path);
            suggestion.type = SuggestionType::PCHOptimization;
            suggestion.priority = candidates.size() >= 3 ? Priority::High : Priority::Medium;
            suggestion.confidence = 0.81;
            suggestion.is_safe = true;
            suggestion.application_mode = SuggestionApplicationMode::Advisory;
            suggestion.title = "Unreal Module PCH Configuration (" + std::to_string(candidates.size()) + " modules)";

            std::ostringstream desc;
            desc << "Prefer explicit/shared PCH usage for Unreal modules with high include parse overhead:\n";
            for (const auto& module : candidates) {
                const auto include_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    module.stats.include_parse_time
                ).count();
                desc << "  - " << module.rules.module_name
                     << " (" << make_repo_relative(module.rules.build_cs_path)
                     << ", include parse " << include_ms << "ms)\n";
                desc << "    Set: PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n";
            }
            suggestion.description = desc.str();

            suggestion.rationale =
                "ModuleRules-level PCH configuration in Unreal Build Tool reduces repeated include parsing "
                "without requiring broad source rewrites.";

            Duration total_include_time = Duration::zero();
            std::size_t total_files = 0;
            for (const auto& module : candidates) {
                total_include_time += module.stats.include_parse_time;
                total_files += module.stats.source_files;
            }
            suggestion.estimated_savings = total_include_time / 6;
            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }
            suggestion.impact.total_files_affected = total_files;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;

            suggestion.target_file.path = candidates.front().rules.build_cs_path;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Set Unreal ModuleRules PCHUsage";
            if (candidates.front().rules.pch_usage_line.has_value()) {
                suggestion.target_file.line_start = *candidates.front().rules.pch_usage_line;
                suggestion.target_file.line_end = *candidates.front().rules.pch_usage_line;
            }
            for (std::size_t i = 1; i < candidates.size(); ++i) {
                FileTarget secondary;
                secondary.path = candidates[i].rules.build_cs_path;
                secondary.action = FileAction::Modify;
                secondary.note = "Set Unreal ModuleRules PCHUsage";
                if (candidates[i].rules.pch_usage_line.has_value()) {
                    secondary.line_start = *candidates[i].rules.pch_usage_line;
                    secondary.line_end = *candidates[i].rules.pch_usage_line;
                }
                suggestion.secondary_files.push_back(std::move(secondary));
            }

            suggestion.implementation_steps = {
                "Set PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs; in each listed <Module>.Build.cs",
                "Define or verify each module's private/public PCH header strategy",
                "Run UnrealBuildTool to confirm module rebuild correctness"
            };
            suggestion.caveats = {
                "Do not force shared PCH on modules with highly volatile umbrella headers",
                "Modules may require staged rollout if third-party headers are unstable"
            };
            suggestion.verification =
                "Build Unreal targets consuming the modules and compare clean/incremental compile times.";

            return suggestion;
        }

    }  // namespace

    Result<SuggestionResult, Error> PCHSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        if (is_unreal_mode_active(context)) {
            if (auto unreal_pch = build_unreal_module_pch_suggestion(context)) {
                result.suggestions.push_back(std::move(*unreal_pch));
                result.items_analyzed = 1;
            }
            auto end_time = std::chrono::steady_clock::now();
            result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        const auto& deps = context.analysis.dependencies;
        if (deps.headers.empty()) {
            auto end_time = std::chrono::steady_clock::now();
            result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        const auto& pch_config = context.options.heuristics.pch;
        const auto min_cpp_standard = find_min_cpp_standard(context.trace);
        const int effective_cpp_standard = min_cpp_standard.value_or(14);

        fs::path project_root = context.project_root;
        if (!project_root.empty() && project_root.is_relative()) {
            project_root = fs::absolute(project_root);
        }
            if (project_root.empty()) {
                for (const auto& unit : context.trace.units) {
                    const auto resolved = resolve_source_path(unit.source_file);
                    const auto root = find_repository_root(resolved);
                    if (!root.empty() && has_build_system_marker(root)) {
                        project_root = root;
                        break;
                    }
                }
            }
        if (project_root.empty() && !deps.headers.empty()) {
            const auto resolved = resolve_source_path(deps.headers.front().path);
            project_root = find_repository_root(resolved);
        }

        BuildSystemType active_build_system = detect_active_build_system(context.trace, project_root);
        bool has_qmake_project = has_qmake_project_file(project_root);

        if (!project_root.empty() && active_build_system == BuildSystemType::CMake) {
            const fs::path existing_pch = project_root / "pch.h";
            if (fs::exists(existing_pch) &&
                has_cmake_pch_config_in_tree(project_root, [&]() { return context.is_cancelled(); })) {
                auto end_time = std::chrono::steady_clock::now();
                result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);
                return Result<SuggestionResult, Error>::success(std::move(result));
            }
        }

        std::vector<fs::path> pch_candidates;
        for (const auto& header : deps.headers) {
            if (context.is_cancelled()) {
                break;
            }
            if (header.inclusion_count >= pch_config.min_include_count &&
                header.total_parse_time >= pch_config.min_aggregate_time)
                {
                    pch_candidates.push_back(header.path);
                }
        }

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& header : deps.headers) {
            if (context.is_cancelled()) {
                break;
            }
            ++analyzed;

            if (header.inclusion_count < pch_config.min_include_count) {
                ++skipped;
                continue;
            }
            if (header.total_parse_time < pch_config.min_aggregate_time) {
                ++skipped;
                continue;
            }

            if (effective_cpp_standard < 17 && is_cxx17_only_header(header.path)) {
                ++skipped;
                continue;
            }

            if (is_non_header_artifact(header.path)) {
                ++skipped;
                continue;
            }

            if (!has_header_extension(header.path)) {
                ++skipped;
                continue;
            }

            if (header.is_external && is_unstable_external_header(header.path)) {
                ++skipped;
                continue;
            }

            std::string filename = header.path.filename().string();
            bool is_std_header = filename.find('.') == std::string::npos ||
                                 filename.find("std") == 0;
            if (is_std_header) {
                ++skipped;
                continue;
            }

            if (!header.is_stable && !header.is_external) {
                ++skipped;
                continue;
            }

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("pch", header.path);
            suggestion.type = SuggestionType::PCHOptimization;
            suggestion.priority = calculate_priority(header, context.trace.total_time, pch_config);
            suggestion.confidence = 0.85;

            std::ostringstream title;
            title << "Add '" << header.path.filename().string() << "' to precompiled header";
            suggestion.title = title.str();

            auto parse_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                header.total_parse_time).count();

            std::ostringstream desc;
            desc << "Header '" << make_repo_relative(header.path) << "' is included in "
                 << header.inclusion_count << " translation units, spending "
                 << parse_time_ms << "ms total on parsing. ";

            if (header.is_external) {
                desc << "This is an external/third-party header (stable).\n\n";
            } else if (header.modification_count > 0) {
                auto days_since_mod = std::chrono::duration_cast<std::chrono::hours>(
                    header.time_since_modification).count() / 24;
                desc << "This header has been modified " << header.modification_count
                     << " times and hasn't changed in " << days_since_mod << " days (stable).\n\n";
            } else {
                desc << "\n\n";
            }

            desc << "Adding it to a precompiled header will parse it once and reuse the cached AST across all translation units. "
                 << "Suggested changes are listed in the **Text Edits** section below.";
            suggestion.description = desc.str();

            std::ostringstream rationale;
            rationale << "Precompiled headers (PCH) store the compiler's internal representation of "
                      << "parsed headers, eliminating redundant parsing across translation units. ";

            if (header.is_external) {
                rationale << "This external header is inherently stable. ";
            } else if (header.modification_count > 0) {
                rationale << "This header is stable (modified only " << header.modification_count
                          << " times historically). ";
            }

            rationale << "Including stable, frequently-used headers in PCH maximizes benefit while "
                      << "minimizing rebuild impact.";
            suggestion.rationale = rationale.str();

            suggestion.estimated_savings = estimate_pch_savings(
                header.total_parse_time,
                header.inclusion_count
            );

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            suggestion.target_file.path = header.path;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Add to precompiled header";

            suggestion.implementation_steps = {
                "1. Create pch.h with stable, frequently-included headers",
                "2. Add #include \"" + header.path.filename().string() + "\" to pch.h",
                "3. Apply the generated build-system edit for the detected build system",
                "4. Rebuild and verify compilation times improved"
            };
            if (active_build_system == BuildSystemType::CMake) {
                suggestion.implementation_steps.push_back(
                    "CMake note: target_precompile_headers() force-includes generated cmake_pch.h, so source files do not need explicit #include \"pch.h\""
                );
            }

            suggestion.impact.total_files_affected = header.including_files;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;

            suggestion.caveats = {
                "Only include stable headers that rarely change in PCH",
                "PCH changes trigger full rebuild of dependent files",
                "Large PCH files may increase memory usage during compilation",
                "Ensure all target source files can use the same PCH"
            };

            if (active_build_system == BuildSystemType::Meson) {
                suggestion.documentation_link =
                    "https://mesonbuild.com/Precompiled-headers.html";
            } else {
                suggestion.documentation_link =
                    "https://cmake.org/cmake/help/latest/command/target_precompile_headers.html";
            }

            suggestion.verification =
                "Run 'time make clean && make' before and after to measure improvement. "
                "Expected improvement: 10-40% reduction in build time.";
            suggestion.is_safe = true;

            // For external/system headers, we can't create pch.h in system directories.
            // Using a generic "pch.h" as a placeholder so the consolidator will handle the actual path.
            if (project_root.empty()) {
                project_root = find_repository_root(header.path);
            }
            if (project_root.empty()) {
                project_root = header.path.parent_path();
            }
            if (active_build_system == BuildSystemType::Unknown) {
                active_build_system = detect_active_build_system(context.trace, project_root);
            }
            if (!has_qmake_project) {
                has_qmake_project = has_qmake_project_file(project_root);
            }

            fs::path pch_path;
            if (header.is_external) {
                pch_path = project_root.empty() ? fs::path("pch.h") : (project_root / "pch.h");
            } else {
                pch_path = project_root / "include" / "pch.h";
                if (!fs::exists(pch_path.parent_path())) {
                    pch_path = project_root / "pch.h";
                }
            }

            std::string include_line;
            if (header.path.string().find('<') == 0) {
                include_line = "#include " + header.path.string();
            } else {
                include_line = "#include \"" + make_repo_relative(header.path) + "\"";
            }

            if (fs::exists(pch_path)) {
                if (auto insert_line = find_include_insertion_line(pch_path)) {
                    suggestion.edits.push_back(make_insert_after_line_edit(pch_path, *insert_line, include_line));
                } else {
                    suggestion.edits.push_back(make_insert_at_start_edit(pch_path, include_line));
                }
            } else {
                TextEdit create_pch;
                create_pch.file = pch_path;
                create_pch.start_line = 0;
                create_pch.start_col = 0;
                create_pch.end_line = 0;
                create_pch.end_col = 0;
                create_pch.new_text = "#pragma once\n\n" + include_line + "\n";
                suggestion.edits.push_back(create_pch);

                FileTarget pch_target;
                pch_target.path = pch_path;
                pch_target.action = FileAction::Create;
                pch_target.note = "Create precompiled header file";
                suggestion.secondary_files.push_back(pch_target);
            }

            // Build system integration: apply edits only for the active build system.
            if (!project_root.empty() && active_build_system == BuildSystemType::CMake) {
                fs::path cmake_path = project_root / "CMakeLists.txt";
                std::optional<CMakeTargetInfo> target;
                std::string cmake_content;

                const auto hinted_targets = collect_cmake_target_hints_for_header(context, header, project_root);
                for (const auto& hinted_target : hinted_targets) {
                    if (auto found = find_named_cmake_target_in_tree(
                            project_root,
                            hinted_target,
                            [&]() { return context.is_cancelled(); })) {
                        cmake_path = found->first;
                        target = found->second;
                        std::ifstream cmake_in(cmake_path);
                        cmake_content.assign((std::istreambuf_iterator<char>(cmake_in)),
                                             std::istreambuf_iterator<char>());
                        cmake_in.close();
                        break;
                    }
                }

                if (!target && fs::exists(cmake_path)) {
                    std::ifstream cmake_in(cmake_path);
                    cmake_content.assign((std::istreambuf_iterator<char>(cmake_in)),
                                         std::istreambuf_iterator<char>());
                    cmake_in.close();
                    auto candidates = find_macro_targets(cmake_content);
                    if (!candidates.empty()) {
                        target = pick_deterministic_target(std::move(candidates));
                    } else if (auto first = find_first_cmake_target(cmake_content)) {
                        target = first;
                    }
                }

                if (!target) {
                    if (auto found = find_cmake_target_in_tree(
                            project_root,
                            [&]() { return context.is_cancelled(); })) {
                        cmake_path = found->first;
                        target = found->second;
                        std::ifstream cmake_in(cmake_path);
                        cmake_content.assign((std::istreambuf_iterator<char>(cmake_in)),
                                             std::istreambuf_iterator<char>());
                        cmake_in.close();
                    }
                }

                if (target && !has_cmake_pch_for_target(cmake_content, target->name)) {
                    std::size_t insert_line = target->insert_after_line;
                    if (target->is_macro) {
                        if (auto end_line = find_cmake_block_end(cmake_content, target->line)) {
                            insert_line = *end_line;
                        }
                    }
                    fs::path rel_pch = pch_path;
                    std::error_code ec;
                    if (rel_pch.is_absolute()) {
                        auto relative = fs::relative(pch_path, cmake_path.parent_path(), ec);
                        if (!ec && !relative.empty()) {
                            rel_pch = relative;
                        }
                    }

                    std::ostringstream cmake_line;
                    cmake_line << "target_precompile_headers(" << target->name
                               << " PRIVATE \"" << rel_pch.generic_string() << "\")";

                    suggestion.edits.push_back(make_insert_after_line_edit(
                        cmake_path,
                        insert_line,
                        cmake_line.str()
                    ));

                    FileTarget cmake_target;
                    cmake_target.path = cmake_path;
                    cmake_target.action = FileAction::Modify;
                    cmake_target.line_start = insert_line + 2;
                    cmake_target.line_end = insert_line + 2;
                    cmake_target.note = "Add target_precompile_headers for PCH";
                    suggestion.secondary_files.push_back(cmake_target);
                }
            }

            if (!project_root.empty() && active_build_system == BuildSystemType::Unknown && has_qmake_project) {
                for (const auto& entry : fs::directory_iterator(project_root)) {
                    if (context.is_cancelled()) {
                        break;
                    }
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".pro") continue;

                    std::ifstream pro_in(entry.path());
                    std::string pro_content((std::istreambuf_iterator<char>(pro_in)),
                                            std::istreambuf_iterator<char>());
                    pro_in.close();

                    if (has_qmake_pch_config(pro_content)) {
                        continue;
                    }

                    const std::size_t line_num = end_of_file_insert_line(pro_content);
                    std::string rel_pch = pch_path.filename().string();
                    std::error_code rel_ec;
                    if (pch_path.is_absolute()) {
                        const auto relative = fs::relative(pch_path, entry.path().parent_path(), rel_ec);
                        if (!rel_ec && !relative.empty()) {
                            rel_pch = relative.generic_string();
                        }
                    }
                    std::ostringstream pro_lines;
                    pro_lines << "\nCONFIG += precompile_header\n"
                              << "PRECOMPILED_HEADER = " << rel_pch;

                    TextEdit pro_edit;
                    pro_edit.file = entry.path();
                    pro_edit.start_line = line_num;
                    pro_edit.start_col = 0;
                    pro_edit.end_line = line_num;
                    pro_edit.end_col = 0;
                    pro_edit.new_text = pro_lines.str() + "\n";
                    suggestion.edits.push_back(pro_edit);

                    FileTarget pro_target;
                    pro_target.path = entry.path();
                    pro_target.action = FileAction::Modify;
                    pro_target.line_start = line_num + 1;
                    pro_target.line_end = line_num + 2;
                    pro_target.note = "Enable PCH in qmake";
                    suggestion.secondary_files.push_back(pro_target);
                }
            }

            if (!project_root.empty() && active_build_system == BuildSystemType::Meson) {
                const fs::path meson_path = project_root / "meson.build";
                if (fs::exists(meson_path)) {
                    std::ifstream meson_in(meson_path);
                    std::string meson_content((std::istreambuf_iterator<char>(meson_in)),
                                              std::istreambuf_iterator<char>());
                    meson_in.close();

                    if (auto target_span = find_first_meson_target(meson_content); target_span && !target_span->has_pch) {
                        std::string rel_pch = pch_path.filename().string();
                        std::error_code rel_ec;
                        if (pch_path.is_absolute()) {
                            const auto relative = fs::relative(pch_path, meson_path.parent_path(), rel_ec);
                            if (!rel_ec && !relative.empty()) {
                                rel_pch = relative.generic_string();
                            }
                        }

                        const std::string pch_arg = "cpp_pch : '" + rel_pch + "'";
                        std::istringstream meson_lines_in(meson_content);
                        std::vector<std::string> meson_lines;
                        std::string line;
                        while (std::getline(meson_lines_in, line)) {
                            meson_lines.push_back(line);
                        }

                        if (target_span->single_line && target_span->start_line < meson_lines.size()) {
                            std::string target_line = meson_lines[target_span->start_line];
                            const std::size_t close_pos = target_line.rfind(')');
                            if (close_pos != std::string::npos) {
                                const std::string before = target_line.substr(0, close_pos);
                                const std::string after = target_line.substr(close_pos);
                                std::string updated = before;
                                if (before.find('(') != std::string::npos && before.find(',') == std::string::npos) {
                                    updated += ", " + pch_arg;
                                } else {
                                    updated += ", " + pch_arg;
                                }
                                updated += after;

                                suggestion.edits.push_back(make_replace_line_edit(
                                    meson_path,
                                    target_span->start_line,
                                    updated
                                ));
                            }
                        } else if (target_span->end_line > 0) {
                            suggestion.edits.push_back(make_insert_after_line_edit(
                                meson_path,
                                target_span->end_line - 1,
                                "  " + pch_arg + ","
                            ));
                        }

                        FileTarget meson_target;
                        meson_target.path = meson_path;
                        meson_target.action = FileAction::Modify;
                        meson_target.line_start = target_span->end_line + 1;
                        meson_target.line_end = target_span->end_line + 1;
                        meson_target.note = "Add cpp_pch to Meson target";
                        suggestion.secondary_files.push_back(meson_target);
                    }
                }
            }

            if (!project_root.empty() && active_build_system == BuildSystemType::SCons) {
                const fs::path scons_path = project_root / "SConstruct";
                if (fs::exists(scons_path)) {
                    std::ifstream scons_in(scons_path);
                    std::string scons_content((std::istreambuf_iterator<char>(scons_in)),
                                              std::istreambuf_iterator<char>());
                    scons_in.close();

                    if (!has_scons_pch_config(scons_content)) {
                        const std::size_t line_num = end_of_file_insert_line(scons_content);
                        TextEdit scons_edit;
                        scons_edit.file = scons_path;
                        scons_edit.start_line = line_num;
                        scons_edit.start_col = 0;
                        scons_edit.end_line = line_num;
                        scons_edit.end_col = 0;
                        scons_edit.new_text =
                            "\n# Precompiled header\n"
                            "pch_source = 'pch.cpp'\n"
                            "env['PCH'] = env.PCH(pch_source)[0]\n";
                        suggestion.edits.push_back(scons_edit);

                        FileTarget scons_target;
                        scons_target.path = scons_path;
                        scons_target.action = FileAction::Modify;
                        scons_target.line_start = line_num + 1;
                        scons_target.line_end = line_num + 3;
                        scons_target.note = "Enable PCH in SCons (MSVC)";
                        suggestion.secondary_files.push_back(scons_target);

                        const fs::path pch_cpp_path = project_root / "pch.cpp";
                        if (!fs::exists(pch_cpp_path)) {
                            std::string pch_include = "pch.h";
                            std::error_code rel_ec;
                            if (pch_path.is_absolute()) {
                                const auto relative = fs::relative(pch_path, pch_cpp_path.parent_path(), rel_ec);
                                if (!rel_ec && !relative.empty()) {
                                    pch_include = relative.generic_string();
                                }
                            }

                            TextEdit pch_cpp_edit;
                            pch_cpp_edit.file = pch_cpp_path;
                            pch_cpp_edit.start_line = 0;
                            pch_cpp_edit.start_col = 0;
                            pch_cpp_edit.end_line = 0;
                            pch_cpp_edit.end_col = 0;
                            pch_cpp_edit.new_text = "#include \"" + pch_include + "\"\n";
                            suggestion.edits.push_back(pch_cpp_edit);

                            FileTarget pch_cpp_target;
                            pch_cpp_target.path = pch_cpp_path;
                            pch_cpp_target.action = FileAction::Create;
                            pch_cpp_target.note = "Create PCH source file for SCons";
                            suggestion.secondary_files.push_back(pch_cpp_target);
                        }
                    }
                }
            }

            if (!project_root.empty() && active_build_system == BuildSystemType::Make) {
                const fs::path makefile_path = project_root / "Makefile";
                if (fs::exists(makefile_path)) {
                    std::ifstream mk_in(makefile_path);
                    std::string mk_content((std::istreambuf_iterator<char>(mk_in)),
                                           std::istreambuf_iterator<char>());
                    mk_in.close();

                    if (!has_make_pch_config(mk_content)) {
                        std::string rel_pch = pch_path.filename().string();
                        std::error_code rel_ec;
                        if (pch_path.is_absolute()) {
                            const auto relative = fs::relative(pch_path, makefile_path.parent_path(), rel_ec);
                            if (!rel_ec && !relative.empty()) {
                                rel_pch = relative.generic_string();
                            }
                        }

                        const std::size_t line_num = end_of_file_insert_line(mk_content);
                        TextEdit mk_edit;
                        mk_edit.file = makefile_path;
                        mk_edit.start_line = line_num;
                        mk_edit.start_col = 0;
                        mk_edit.end_line = line_num;
                        mk_edit.end_col = 0;
                        mk_edit.new_text =
                            "\n# Precompiled header\n"
                            "PCH_HEADER := " + rel_pch + "\n"
                            "PCH_FILE := $(PCH_HEADER).gch\n"
                            "CPPFLAGS += -include $(PCH_HEADER)\n\n"
                            "$(PCH_FILE): $(PCH_HEADER)\n"
                            "\t$(CXX) $(CPPFLAGS) -x c++-header $< -o $@\n";
                        suggestion.edits.push_back(mk_edit);

                        FileTarget mk_target;
                        mk_target.path = makefile_path;
                        mk_target.action = FileAction::Modify;
                        mk_target.line_start = line_num + 1;
                        mk_target.line_end = line_num + 6;
                        mk_target.note = "Add PCH rule and -include in Makefile";
                        suggestion.secondary_files.push_back(mk_target);
                    }
                }
            }

            if (!project_root.empty() && active_build_system == BuildSystemType::Ninja) {
                const fs::path ninja_path = project_root / "build.ninja";
                if (fs::exists(ninja_path)) {
                    std::ifstream ninja_in(ninja_path);
                    std::string ninja_content((std::istreambuf_iterator<char>(ninja_in)),
                                              std::istreambuf_iterator<char>());
                    ninja_in.close();

                    if (!has_ninja_pch_config(ninja_content)) {
                        std::string rel_pch = pch_path.filename().string();
                        std::error_code rel_ec;
                        if (pch_path.is_absolute()) {
                            const auto relative = fs::relative(pch_path, ninja_path.parent_path(), rel_ec);
                            if (!rel_ec && !relative.empty()) {
                                rel_pch = relative.generic_string();
                            }
                        }

                        std::regex cxxflags_regex(R"(^\s*cxxflags\s*=)");
                        if (auto line_idx = find_first_line_matching(ninja_content, cxxflags_regex)) {
                            std::istringstream ninja_lines_in(ninja_content);
                            std::vector<std::string> ninja_lines;
                            std::string line;
                            while (std::getline(ninja_lines_in, line)) {
                                ninja_lines.push_back(line);
                            }
                            if (*line_idx < ninja_lines.size()) {
                                ninja_lines[*line_idx] += " -include " + rel_pch;
                                suggestion.edits.push_back(make_replace_line_edit(
                                    ninja_path,
                                    *line_idx,
                                    ninja_lines[*line_idx]
                                ));
                                FileTarget ninja_target;
                                ninja_target.path = ninja_path;
                                ninja_target.action = FileAction::Modify;
                                ninja_target.line_start = *line_idx + 1;
                                ninja_target.line_end = *line_idx + 1;
                                ninja_target.note = "Add -include to Ninja cxxflags";
                                suggestion.secondary_files.push_back(ninja_target);
                            }
                        }

                        const std::size_t line_num = end_of_file_insert_line(ninja_content);
                        TextEdit ninja_edit;
                        ninja_edit.file = ninja_path;
                        ninja_edit.start_line = line_num;
                        ninja_edit.start_col = 0;
                        ninja_edit.end_line = line_num;
                        ninja_edit.end_col = 0;
                        ninja_edit.new_text =
                            "\n# Precompiled header\n"
                            "cxxflags = -include " + rel_pch + "\n"
                            "rule pch\n"
                            "  command = $cxx -x c++-header $in -o $out $cxxflags\n"
                            "  description = PCH $out\n\n"
                            "build " + rel_pch + ".gch: pch " + rel_pch + "\n";
                        suggestion.edits.push_back(ninja_edit);

                        FileTarget ninja_target;
                        ninja_target.path = ninja_path;
                        ninja_target.action = FileAction::Modify;
                        ninja_target.line_start = line_num + 1;
                        ninja_target.line_end = line_num + 7;
                        ninja_target.note = "Add PCH rule in Ninja build";
                        suggestion.secondary_files.push_back(ninja_target);
                    }
                }
            }

            if (!project_root.empty() && active_build_system == BuildSystemType::MSBuild) {
                for (const auto& entry : fs::directory_iterator(project_root)) {
                    if (context.is_cancelled()) {
                        break;
                    }
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() == ".vcxproj") {
                        std::ifstream vcx_in(entry.path());
                        std::string vcx_content((std::istreambuf_iterator<char>(vcx_in)),
                                                std::istreambuf_iterator<char>());
                        vcx_in.close();

                        if (vcx_content.find("PrecompiledHeaderFile") == std::string::npos) {
                            std::string rel_pch = pch_path.filename().string();
                            std::error_code rel_ec;
                            if (pch_path.is_absolute()) {
                                const auto relative = fs::relative(pch_path, entry.path().parent_path(), rel_ec);
                                if (!rel_ec && !relative.empty()) {
                                    rel_pch = relative.generic_string();
                                }
                            }

                            const std::string block =
                                "  <ItemDefinitionGroup>\n"
                                "    <ClCompile>\n"
                                "      <PrecompiledHeader>Use</PrecompiledHeader>\n"
                                "      <PrecompiledHeaderFile>" + rel_pch + "</PrecompiledHeaderFile>\n"
                                "    </ClCompile>\n"
                                "  </ItemDefinitionGroup>\n"
                                "  <ItemGroup>\n"
                                "    <ClCompile Include=\"pch.cpp\">\n"
                                "      <PrecompiledHeader>Create</PrecompiledHeader>\n"
                                "    </ClCompile>\n"
                                "  </ItemGroup>\n";

                            std::regex project_end_regex(R"(^\s*</Project>)");
                            std::size_t insert_line = end_of_file_insert_line(vcx_content);
                            if (auto end_line = find_first_line_matching(vcx_content, project_end_regex)) {
                                insert_line = *end_line;
                            }

                            TextEdit vcx_edit;
                            vcx_edit.file = entry.path();
                            vcx_edit.start_line = insert_line;
                            vcx_edit.start_col = 0;
                            vcx_edit.end_line = insert_line;
                            vcx_edit.end_col = 0;
                            vcx_edit.new_text = "\n" + block;
                            suggestion.edits.push_back(vcx_edit);

                            FileTarget vcx_target;
                            vcx_target.path = entry.path();
                            vcx_target.action = FileAction::Modify;
                            vcx_target.line_start = insert_line + 1;
                            vcx_target.line_end = insert_line + 8;
                            vcx_target.note = "Enable PCH in MSBuild project";
                            suggestion.secondary_files.push_back(vcx_target);

                            const fs::path pch_cpp_path = entry.path().parent_path() / "pch.cpp";
                            if (!fs::exists(pch_cpp_path)) {
                                TextEdit pch_cpp_edit;
                                pch_cpp_edit.file = pch_cpp_path;
                                pch_cpp_edit.start_line = 0;
                                pch_cpp_edit.start_col = 0;
                                pch_cpp_edit.end_line = 0;
                                pch_cpp_edit.end_col = 0;
                                pch_cpp_edit.new_text = "#include \"" + rel_pch + "\"\n";
                                suggestion.edits.push_back(pch_cpp_edit);

                                FileTarget pch_cpp_target;
                                pch_cpp_target.path = pch_cpp_path;
                                pch_cpp_target.action = FileAction::Create;
                                pch_cpp_target.note = "Create PCH source file for MSBuild";
                                suggestion.secondary_files.push_back(pch_cpp_target);
                            }
                        }
                    }
                }
            }

            if (!project_root.empty() && active_build_system == BuildSystemType::Bazel) {
                for (const auto& entry : fs::directory_iterator(project_root)) {
                    if (context.is_cancelled()) {
                        break;
                    }
                    if (!entry.is_regular_file()) continue;
                    const std::string build_filename = entry.path().filename().string();
                    if (build_filename != "BUILD" && build_filename != "BUILD.bazel") continue;

                    std::ifstream bazel_in(entry.path());
                    std::string bazel_content((std::istreambuf_iterator<char>(bazel_in)),
                                              std::istreambuf_iterator<char>());
                    bazel_in.close();

                    if (auto span = find_bazel_rule_span(bazel_content); span && !span->has_key) {
                        std::string rel_pch = pch_path.filename().string();
                        std::error_code rel_ec;
                        if (pch_path.is_absolute()) {
                            const auto relative = fs::relative(pch_path, entry.path().parent_path(), rel_ec);
                            if (!rel_ec && !relative.empty()) {
                                rel_pch = relative.generic_string();
                            }
                        }

                        suggestion.edits.push_back(make_insert_after_line_edit(
                            entry.path(),
                            span->start_line,
                            "    copts = [\"-include\", \"" + rel_pch + "\"],"
                        ));

                        FileTarget bazel_target;
                        bazel_target.path = entry.path();
                        bazel_target.action = FileAction::Modify;
                        bazel_target.line_start = span->start_line + 2;
                        bazel_target.line_end = span->start_line + 2;
                        bazel_target.note = "Add copts with -include in Bazel rule";
                        suggestion.secondary_files.push_back(bazel_target);
                    }
                }
            }

            if (!project_root.empty() && active_build_system == BuildSystemType::Buck2) {
                for (const auto& entry : fs::directory_iterator(project_root)) {
                    if (context.is_cancelled()) {
                        break;
                    }
                    if (!entry.is_regular_file()) continue;
                    const std::string buck_filename = entry.path().filename().string();
                    if (buck_filename != "BUCK" && buck_filename != "BUCK2") continue;

                    std::ifstream buck_in(entry.path());
                    std::string buck_content((std::istreambuf_iterator<char>(buck_in)),
                                             std::istreambuf_iterator<char>());
                    buck_in.close();

                    if (has_buck2_pch_config(buck_content)) {
                        continue;
                    }

                    if (auto span = find_buck2_rule_span(buck_content); span) {
                        std::string rel_pch = pch_path.filename().string();
                        std::error_code rel_ec;
                        if (pch_path.is_absolute()) {
                            const auto relative = fs::relative(pch_path, entry.path().parent_path(), rel_ec);
                            if (!rel_ec && !relative.empty()) {
                                rel_pch = relative.generic_string();
                            }
                        }

                        suggestion.edits.push_back(make_insert_after_line_edit(
                            entry.path(),
                            span->start_line,
                            "    precompiled_header = \":pch\","
                        ));

                        FileTarget buck_target;
                        buck_target.path = entry.path();
                        buck_target.action = FileAction::Modify;
                        buck_target.line_start = span->start_line + 2;
                        buck_target.line_end = span->start_line + 2;
                        buck_target.note = "Attach precompiled_header in Buck2 rule";
                        suggestion.secondary_files.push_back(buck_target);

                        const std::size_t line_num = end_of_file_insert_line(buck_content);
                        TextEdit buck_pch_edit;
                        buck_pch_edit.file = entry.path();
                        buck_pch_edit.start_line = line_num;
                        buck_pch_edit.start_col = 0;
                        buck_pch_edit.end_line = line_num;
                        buck_pch_edit.end_col = 0;
                        buck_pch_edit.new_text =
                            "\n# Precompiled header\n"
                            "cxx_precompiled_header(\n"
                            "    name = \"pch\",\n"
                            "    src = \"" + rel_pch + "\",\n"
                            ")\n";
                        suggestion.edits.push_back(buck_pch_edit);

                        FileTarget buck_pch_target;
                        buck_pch_target.path = entry.path();
                        buck_pch_target.action = FileAction::Modify;
                        buck_pch_target.line_start = line_num + 1;
                        buck_pch_target.line_end = line_num + 4;
                        buck_pch_target.note = "Define cxx_precompiled_header rule";
                        suggestion.secondary_files.push_back(buck_pch_target);
                    }
                }
            }

            if (!project_root.empty() && active_build_system == BuildSystemType::XCode) {
                for (const auto& entry : fs::directory_iterator(project_root)) {
                    if (context.is_cancelled()) {
                        break;
                    }
                    if (!entry.is_directory()) continue;
                    if (entry.path().extension() != ".xcodeproj") continue;

                    const fs::path pbxproj_path = entry.path() / "project.pbxproj";
                    if (!fs::exists(pbxproj_path)) continue;

                    std::ifstream pbx_in(pbxproj_path);
                    std::string pbx_content((std::istreambuf_iterator<char>(pbx_in)),
                                            std::istreambuf_iterator<char>());
                    pbx_in.close();

                    if (pbx_content.find("GCC_PREFIX_HEADER") != std::string::npos ||
                        pbx_content.find("GCC_PRECOMPILE_PREFIX_HEADER") != std::string::npos) {
                        continue;
                    }

                    std::string rel_pch = pch_path.filename().string();
                    std::error_code rel_ec;
                    if (pch_path.is_absolute()) {
                        const auto relative = fs::relative(pch_path, pbxproj_path.parent_path(), rel_ec);
                        if (!rel_ec && !relative.empty()) {
                            rel_pch = relative.generic_string();
                        }
                    }

                    std::regex build_settings_regex(R"(^\s*buildSettings\s*=\s*\{)");
                    if (auto line_idx = find_first_line_matching(pbx_content, build_settings_regex)) {
                        suggestion.edits.push_back(make_insert_after_line_edit(
                            pbxproj_path,
                            *line_idx,
                            "                GCC_PRECOMPILE_PREFIX_HEADER = YES;\n"
                            "                GCC_PREFIX_HEADER = \"" + rel_pch + "\";"
                        ));

                        FileTarget pbx_target;
                        pbx_target.path = pbxproj_path;
                        pbx_target.action = FileAction::Modify;
                        pbx_target.line_start = *line_idx + 2;
                        pbx_target.line_end = *line_idx + 3;
                        pbx_target.note = "Enable PCH in Xcode build settings";
                        suggestion.secondary_files.push_back(pbx_target);
                    }
                }
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

    void register_pch_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<PCHSuggester>()
        );
    }
}  // namespace bha::suggestions
