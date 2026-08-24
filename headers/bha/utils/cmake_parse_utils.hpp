#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bha::utils {

    namespace detail {

        [[nodiscard]] inline std::optional<std::size_t> cmake_bracket_equals(
            const std::string_view text,
            const std::size_t position
        ) noexcept {
            if (position >= text.size() || text[position] != '[') {
                return std::nullopt;
            }

            std::size_t cursor = position + 1;
            while (cursor < text.size() && text[cursor] == '=') {
                ++cursor;
            }
            if (cursor >= text.size() || text[cursor] != '[') {
                return std::nullopt;
            }
            return cursor - position - 1;
        }

        [[nodiscard]] inline bool cmake_bracket_closes(
            const std::string_view text,
            const std::size_t position,
            const std::size_t equals
        ) noexcept {
            if (position >= text.size() || text[position] != ']') {
                return false;
            }

            const std::size_t closing_bracket = position + equals + 1;
            return closing_bracket < text.size() && text[closing_bracket] == ']';
        }

    }  // namespace detail

    struct CMakeCommandStart {
        std::string name;
        std::size_t open_pos = 0;
    };

    [[nodiscard]] inline std::optional<CMakeCommandStart> parse_cmake_command_start(std::string_view line) {
        if (line.empty()) {
            return std::nullopt;
        }
        const unsigned char first = static_cast<unsigned char>(line.front());
        if (!std::isalpha(first) && line.front() != '_') {
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

        std::size_t j = i;
        while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) {
            ++j;
        }
        if (j >= line.size() || line[j] != '(') {
            return std::nullopt;
        }

        return CMakeCommandStart{std::string(line.substr(0, i)), j};
    }

    [[nodiscard]] inline int count_paren_delta_outside_quotes(std::string_view text) {
        int delta = 0;
        bool in_quote = false;
        bool escaped = false;
        std::optional<std::size_t> bracket_equals;

        for (std::size_t index = 0; index < text.size(); ++index) {
            const char c = text[index];
            if (bracket_equals.has_value()) {
                if (detail::cmake_bracket_closes(text, index, *bracket_equals)) {
                    index += *bracket_equals + 1;
                    bracket_equals.reset();
                }
                continue;
            }
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (in_quote) {
                if (c == '"') {
                    in_quote = false;
                }
                continue;
            }
            if (c == '"') {
                in_quote = true;
                continue;
            }
            if (const auto equals = detail::cmake_bracket_equals(text, index); equals.has_value()) {
                bracket_equals = *equals;
                index += *equals + 1;
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

    [[nodiscard]] inline std::vector<std::string> tokenize_cmake_args(std::string_view args) {
        std::vector<std::string> tokens;
        std::string current;
        bool in_quote = false;
        bool escaped = false;
        std::optional<std::size_t> bracket_equals;
        std::size_t bracket_content_start = 0;

        auto flush = [&]() {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        };

        for (std::size_t i = 0; i < args.size(); ++i) {
            const char c = args[i];
            if (bracket_equals.has_value()) {
                if (detail::cmake_bracket_closes(args, i, *bracket_equals)) {
                    current.append(args, bracket_content_start, i - bracket_content_start);
                    if (!current.empty() && current.front() == '\n') {
                        current.erase(0, 1);
                    }
                    i += *bracket_equals + 1;
                    bracket_equals.reset();
                    flush();
                }
                continue;
            }
            if (escaped) {
                current.push_back(c);
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (in_quote) {
                if (c == '"') {
                    in_quote = false;
                } else {
                    current.push_back(c);
                }
                continue;
            }
            if (c == '"') {
                in_quote = true;
                continue;
            }
            if (const auto equals = detail::cmake_bracket_equals(args, i); equals.has_value()) {
                bracket_equals = *equals;
                bracket_content_start = i + *equals + 2;
                i += *equals + 1;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c)) || c == ';') {
                flush();
                continue;
            }
            current.push_back(c);
        }
        if (escaped) {
            current.push_back('\\');
        }
        flush();
        return tokens;
    }

}  // namespace bha::utils
