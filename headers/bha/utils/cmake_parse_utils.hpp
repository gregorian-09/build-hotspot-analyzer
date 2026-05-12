#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bha::utils {

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

    [[nodiscard]] inline std::vector<std::string> tokenize_cmake_args(std::string_view args) {
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

}  // namespace bha::utils
