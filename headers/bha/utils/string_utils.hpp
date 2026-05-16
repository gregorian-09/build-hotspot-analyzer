#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace bha::utils {

    inline std::string_view trim_left(std::string_view s) noexcept {
        const auto it = std::ranges::find_if(s, [](const unsigned char c) {
            return !std::isspace(c);
        });
        return s.substr(static_cast<std::size_t>(it - s.begin()));
    }

    inline std::string_view trim_right(std::string_view s) noexcept {
        const auto it = std::find_if(s.rbegin(), s.rend(), [](const unsigned char c) {
            return !std::isspace(c);
        });
        return s.substr(0, static_cast<std::size_t>(s.rend() - it));
    }

    inline std::string_view trim(const std::string_view s) noexcept {
        return trim_left(trim_right(s));
    }

    inline std::string trim_copy(std::string_view s) {
        return std::string(trim(s));
    }

    inline std::string trim_whitespace_copy(std::string value) {
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

    inline std::vector<std::string_view> split(std::string_view s, const char delimiter) {
        std::vector<std::string_view> result;
        std::size_t start = 0;
        std::size_t end = s.find(delimiter);
        while (end != std::string_view::npos) {
            result.push_back(s.substr(start, end - start));
            start = end + 1;
            end = s.find(delimiter, start);
        }
        result.push_back(s.substr(start));
        return result;
    }

    inline std::vector<std::string_view> split(std::string_view s, const std::string_view delimiter) {
        std::vector<std::string_view> result;
        if (delimiter.empty()) {
            result.push_back(s);
            return result;
        }
        std::size_t start = 0;
        std::size_t end = s.find(delimiter);
        while (end != std::string_view::npos) {
            result.push_back(s.substr(start, end - start));
            start = end + delimiter.size();
            end = s.find(delimiter, start);
        }
        result.push_back(s.substr(start));
        return result;
    }

    template<typename Container>
    std::string join(const Container& parts, const std::string_view delimiter) {
        if (parts.empty()) return "";
        std::ostringstream oss;
        auto it = parts.begin();
        oss << *it;
        ++it;
        for (; it != parts.end(); ++it) {
            oss << delimiter << *it;
        }
        return oss.str();
    }

    inline bool starts_with(const std::string_view s, const std::string_view prefix) noexcept {
        return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
    }

    inline bool ends_with(const std::string_view s, const std::string_view suffix) noexcept {
        return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
    }

    inline bool contains(const std::string_view s, const std::string_view needle) noexcept {
        return s.find(needle) != std::string_view::npos;
    }

    inline std::string to_lower(const std::string_view s) {
        std::string result(s);
        std::ranges::transform(result, result.begin(),
                               [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    inline std::string to_upper(const std::string_view s) {
        std::string result(s);
        std::ranges::transform(result, result.begin(),
                               [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return result;
    }

    inline std::string replace_all(std::string_view s, const std::string_view from, const std::string_view to) {
        if (from.empty()) return std::string(s);
        std::string result;
        result.reserve(s.size());
        std::size_t pos = 0;
        std::size_t found;
        while ((found = s.find(from, pos)) != std::string_view::npos) {
            result.append(s, pos, found - pos);
            result.append(to);
            pos = found + from.size();
        }
        result.append(s, pos, s.size() - pos);
        return result;
    }

    inline bool is_identifier_char(const char ch) {
        const unsigned char value = static_cast<unsigned char>(ch);
        return std::isalnum(value) || ch == '_';
    }

    inline bool contains_identifier_token(
        const std::string& text,
        const std::string& symbol
    ) {
        if (text.empty() || symbol.empty()) return false;
        std::size_t pos = text.find(symbol);
        while (pos != std::string::npos) {
            const bool left_ok = (pos == 0) || !is_identifier_char(text[pos - 1]);
            const std::size_t end = pos + symbol.size();
            const bool right_ok = (end >= text.size()) || !is_identifier_char(text[end]);
            if (left_ok && right_ok) return true;
            pos = text.find(symbol, pos + 1);
        }
        return false;
    }

    inline bool looks_like_macro_identifier(const std::string& identifier) {
        bool saw_alpha = false;
        for (const char c : identifier) {
            if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
                saw_alpha = true;
                if (std::isupper(static_cast<unsigned char>(c)) == 0 && c != '_') {
                    return false;
                }
            }
        }
        return saw_alpha;
    }

    inline std::string strip_comments_and_strings(
        const std::string& line,
        bool& in_block_comment
    ) {
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

            if (ch == '/' && next == '/') break;
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

    inline std::string lowercase_ascii(std::string_view input) {
        std::string lowered;
        lowered.reserve(input.size());
        for (const char c : input) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return lowered;
    }

    inline std::optional<std::pair<std::size_t, std::size_t>> find_outer_paren_span(
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

}  // namespace bha::utils

