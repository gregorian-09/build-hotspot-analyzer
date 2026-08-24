#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bha::utils {

    namespace detail {

        [[nodiscard]] inline bool skip_include_trivia(
            const std::string_view line,
            std::size_t& position
        ) noexcept {
            while (position < line.size()) {
                if (std::isspace(static_cast<unsigned char>(line[position])) != 0) {
                    ++position;
                    continue;
                }
                if (line[position] != '/' || position + 1 >= line.size()) {
                    return true;
                }
                if (line[position + 1] == '/') {
                    position = line.size();
                    return true;
                }
                if (line[position + 1] != '*') {
                    return true;
                }

                const std::size_t comment_end = line.find("*/", position + 2);
                if (comment_end == std::string_view::npos) {
                    return false;
                }
                position = comment_end + 2;
            }
            return true;
        }

    }  // namespace detail

    struct ParsedIncludeDirective {
        std::string header_name;
        bool is_system = false;
    };

    [[nodiscard]] inline std::optional<ParsedIncludeDirective> parse_include_directive_line(
        const std::string_view line
    ) {
        std::size_t pos = 0;

        if (!detail::skip_include_trivia(line, pos) || pos >= line.size() || line[pos] != '#') {
            return std::nullopt;
        }
        ++pos;

        if (!detail::skip_include_trivia(line, pos)) return std::nullopt;

        constexpr std::string_view include_kwd = "include";
        if (pos + include_kwd.size() > line.size() ||
            line.substr(pos, include_kwd.size()) != include_kwd) {
            return std::nullopt;
        }
        pos += include_kwd.size();

        if (!detail::skip_include_trivia(line, pos)) return std::nullopt;
        if (pos >= line.size() || (line[pos] != '"' && line[pos] != '<')) {
            return std::nullopt;
        }

        const char delimiter = line[pos++];
        const char closing_delimiter = delimiter == '<' ? '>' : '"';
        const std::size_t start = pos;
        while (pos < line.size() && line[pos] != closing_delimiter) {
            ++pos;
        }
        if (pos == start || pos >= line.size()) {
            return std::nullopt;
        }

        const std::size_t end = pos;
        ++pos;
        if (!detail::skip_include_trivia(line, pos) || pos != line.size()) {
            return std::nullopt;
        }

        return ParsedIncludeDirective{
            std::filesystem::path(line.substr(start, end - start)).lexically_normal().generic_string(),
            delimiter == '<'
        };
    }

    [[nodiscard]] inline std::vector<ParsedIncludeDirective> parse_include_directives_from_file(
        const std::filesystem::path& file_path
    ) {
        std::vector<ParsedIncludeDirective> directives;

        std::ifstream in(file_path);
        if (!in.is_open()) {
            return directives;
        }

        std::string line;
        while (std::getline(in, line)) {
            if (auto directive = parse_include_directive_line(line)) {
                directives.push_back(std::move(*directive));
            }
        }

        return directives;
    }

}  // namespace bha::utils
