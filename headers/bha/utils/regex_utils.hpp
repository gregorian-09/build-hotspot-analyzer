#pragma once

#include <string>
#include <string_view>

namespace bha::utils
{
    [[nodiscard]] inline std::string regex_escape(std::string_view input) {
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
}  // namespace bha::utils

