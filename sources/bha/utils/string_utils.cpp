//
// Created by gregorian on 14/10/2025.
//

#include "bha/utils/string_utils.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace bha::utils {

std::vector<std::string> split(const std::string_view str, const char delimiter) {
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = str.find(delimiter);

    while (end != std::string_view::npos) {
        result.emplace_back(str.substr(start, end - start));
        start = end + 1;
        end = str.find(delimiter, start);
    }

    result.emplace_back(str.substr(start));
    return result;
}

std::vector<std::string> split(const std::string_view str, const std::string_view delimiter) {
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = str.find(delimiter);

    while (end != std::string_view::npos) {
        result.emplace_back(str.substr(start, end - start));
        start = end + delimiter.length();
        end = str.find(delimiter, start);
    }

    result.emplace_back(str.substr(start));
    return result;
}

std::string join(const std::vector<std::string>& strings, const std::string_view separator) {
    if (strings.empty()) return "";

    std::ostringstream oss;
    oss << strings[0];

    for (size_t i = 1; i < strings.size(); ++i) {
        oss << separator << strings[i];
    }

    return oss.str();
}

std::string join(const std::vector<std::string_view>& strings, const std::string_view separator) {
    if (strings.empty()) return "";

    std::ostringstream oss;
    oss << strings[0];

    for (size_t i = 1; i < strings.size(); ++i) {
        oss << separator << strings[i];
    }

    return oss.str();
}

std::string trim(const std::string_view str) {
    const auto start = std::ranges::find_if_not(str, [](const unsigned char ch) {
        return std::isspace(ch);
    });

    const auto end = std::find_if_not(str.rbegin(), str.rend(), [](const unsigned char ch) {
        return std::isspace(ch);
    }).base();

    return start < end ? std::string(start, end) : std::string();
}

std::string trim_left(const std::string_view str) {
    const auto start = std::ranges::find_if_not(str, [](const unsigned char ch) {
        return std::isspace(ch);
    });

    return std::string{start, str.end()};
}

std::string trim_right(const std::string_view str) {
    const auto end = std::find_if_not(str.rbegin(), str.rend(), [](const unsigned char ch) {
        return std::isspace(ch);
    }).base();

    return std::string{str.begin(), end};
}

bool starts_with(const std::string_view str, const std::string_view prefix) {
    return str.size() >= prefix.size() &&
           str.substr(0, prefix.size()) == prefix;
}

bool ends_with(const std::string_view str, const std::string_view suffix) {
    return str.size() >= suffix.size() &&
           str.substr(str.size() - suffix.size()) == suffix;
}

bool contains(const std::string_view str, const std::string_view substr) {
    return str.find(substr) != std::string_view::npos;
}

bool contains(const std::string_view str, const char ch) {
    return str.find(ch) != std::string_view::npos;
}

std::string to_lower(const std::string_view str) {
    std::string result(str);
    std::ranges::transform(result, result.begin(),
                           [](const unsigned char c) { return std::tolower(c); });
    return result;
}

std::string to_upper(const std::string_view str) {
    std::string result(str);
    std::ranges::transform(result, result.begin(),
                           [](const unsigned char c) { return std::toupper(c); });
    return result;
}

std::string replace_all(const std::string_view str, const std::string_view from, const std::string_view to) {
    if (from.empty()) return std::string(str);

    std::string result(str);
    size_t pos = 0;

    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }

    return result;
}

std::string replace_first(const std::string_view str, const std::string_view from, const std::string_view to) {
    std::string result(str);

    if (const size_t pos = result.find(from); pos != std::string::npos) {
        result.replace(pos, from.length(), to);
    }

    return result;
}

std::optional<size_t> find(const std::string_view str, const std::string_view substr) {
    size_t pos = str.find(substr);
    return pos != std::string_view::npos ? std::optional(pos) : std::nullopt;
}

std::optional<size_t> find_last(const std::string_view str, const std::string_view substr) {
    size_t pos = str.rfind(substr);
    return pos != std::string_view::npos ? std::optional(pos) : std::nullopt;
}

bool is_empty_or_whitespace(const std::string_view str) {
    return std::ranges::all_of(str, [](const unsigned char ch) {
        return std::isspace(ch);
    });
}

std::string remove_prefix(const std::string_view str, const std::string_view prefix) {
    if (starts_with(str, prefix)) {
        return std::string(str.substr(prefix.size()));
    }
    return std::string(str);
}

std::string remove_suffix(const std::string_view str, const std::string_view suffix) {
    if (ends_with(str, suffix)) {
        return std::string(str.substr(0, str.size() - suffix.size()));
    }
    return std::string(str);
}

std::vector<std::string> split_lines(const std::string_view str) {
    std::vector<std::string> result;
    size_t start = 0;

    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\n') {
            size_t end = i;
            if (end > start && str[end - 1] == '\r') {
                --end;
            }
            result.emplace_back(str.substr(start, end - start));
            start = i + 1;
        }
    }

    if (start < str.size()) {
        result.emplace_back(str.substr(start));
    }

    return result;
}

bool equals_ignore_case(const std::string_view str1, const std::string_view str2) {
    if (str1.size() != str2.size()) return false;

    return std::equal(str1.begin(), str1.end(), str2.begin(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

} // namespace bha::utils