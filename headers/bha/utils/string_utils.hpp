//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_STRING_UTILS_HPP
#define BUILDTIMEHOTSPOTANALYZER_STRING_UTILS_HPP

/**
 * @file string_utils.hpp
 * @brief String manipulation utilities.
 *
 * Provides common string operations like trimming, splitting, joining,
 * and format conversion. All functions are designed to be efficient
 * and avoid unnecessary allocations where possible.
 */

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace bha::string_utils {

    /**
     * Trims whitespace from the beginning of a string.
     *
     * @param s The string view to trim.
     * @return A string view with leading whitespace removed.
     */
    inline std::string_view trim_left(std::string_view s) noexcept {
        const auto it = std::ranges::find_if(s, [](const unsigned char c) {
            return !std::isspace(c);
        });
        return s.substr(static_cast<std::size_t>(it - s.begin()));
    }

    /**
     * Trims whitespace from the end of a string.
     *
     * @param s The string view to trim.
     * @return A string view with trailing whitespace removed.
     */
    inline std::string_view trim_right(std::string_view s) noexcept {
        const auto it = std::find_if(s.rbegin(), s.rend(), [](const unsigned char c) {
            return !std::isspace(c);
        });
        return s.substr(0, static_cast<std::size_t>(s.rend() - it));
    }

    /**
     * Trims whitespace from both ends of a string.
     *
     * @param s The string view to trim.
     * @return A string view with leading and trailing whitespace removed.
     */
    inline std::string_view trim(const std::string_view s) noexcept {
        return trim_left(trim_right(s));
    }

    /**
     * Splits a string by a delimiter.
     *
     * @param s The string to split.
     * @param delimiter The character to split on.
     * @return A vector of string views representing the parts.
     */
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

    /**
     * Splits a string by a delimiter string.
     *
     * @param s The string to split.
     * @param delimiter The string to split on.
     * @return A vector of string views representing the parts.
     */
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

    /**
     * Joins strings with a delimiter.
     *
     * @param parts The strings to join.
     * @param delimiter The delimiter to insert between parts.
     * @return The joined string.
     */
    template<typename Container>
    std::string join(const Container& parts, const std::string_view delimiter) {
        if (parts.empty()) {
            return "";
        }

        std::ostringstream oss;
        auto it = parts.begin();
        oss << *it;
        ++it;

        for (; it != parts.end(); ++it) {
            oss << delimiter << *it;
        }

        return oss.str();
    }

    /**
     * Checks if a string starts with a prefix.
     *
     * @param s The string to check.
     * @param prefix The prefix to look for.
     * @return True if s starts with prefix.
     */
    inline bool starts_with(const std::string_view s, const std::string_view prefix) noexcept {
        return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
    }

    /**
     * Checks if a string ends with a suffix.
     *
     * @param s The string to check.
     * @param suffix The suffix to look for.
     * @return True if s ends with suffix.
     */
    inline bool ends_with(const std::string_view s, const std::string_view suffix) noexcept {
        return s.size() >= suffix.size() &&
               s.substr(s.size() - suffix.size()) == suffix;
    }

    /**
     * Checks if a string contains a substring.
     *
     * @param s The string to search in.
     * @param needle The substring to search for.
     * @return True if s contains needle.
     */
    inline bool contains(const std::string_view s, const std::string_view needle) noexcept {
        return s.find(needle) != std::string_view::npos;
    }

    /**
     * Converts a string to lowercase.
     *
     * @param s The string to convert.
     * @return A new string with all characters in lowercase.
     */
    inline std::string to_lower(const std::string_view s) {
        std::string result(s);
        std::ranges::transform(result, result.begin(),
                               [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    /**
     * Converts a string to uppercase.
     *
     * @param s The string to convert.
     * @return A new string with all characters in uppercase.
     */
    inline std::string to_upper(const std::string_view s) {
        std::string result(s);
        std::ranges::transform(result, result.begin(),
                               [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return result;
    }

    /**
     * Replaces all occurrences of a substring.
     *
     * @param s The original string.
     * @param from The substring to find.
     * @param to The replacement string.
     * @return A new string with all replacements made.
     */
    inline std::string replace_all(std::string_view s, const std::string_view from, const std::string_view to) {
        if (from.empty()) {
            return std::string(s);
        }

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

    /**
     * Formats a duration in human-readable form.
     *
     * @param nanoseconds Duration in nanoseconds.
     * @return Human-readable string like "1.5s", "250ms", "42us".
     */
    inline std::string format_duration(const long long nanoseconds) {
        constexpr long long ns_per_s = 1000000000LL;
        constexpr long long ns_per_min = 60LL * ns_per_s;
        constexpr long long ns_per_hour = 60LL * ns_per_min;

        std::ostringstream oss;
        oss.precision(2);
        oss << std::fixed;

        if (nanoseconds >= ns_per_hour) {
            oss << static_cast<double>(nanoseconds) / static_cast<double>(ns_per_hour) << "h";
        } else if (nanoseconds >= ns_per_min) {
            oss << static_cast<double>(nanoseconds) / static_cast<double>(ns_per_min) << "min";
        } else if (nanoseconds >= ns_per_s) {
            oss << static_cast<double>(nanoseconds) / static_cast<double>(ns_per_s) << "s";
        } else if (constexpr long long ns_per_ms = 1000000LL; nanoseconds >= ns_per_ms) {
            oss << static_cast<double>(nanoseconds) / static_cast<double>(ns_per_ms) << "ms";
        } else if (constexpr long long ns_per_us = 1000LL; nanoseconds >= ns_per_us) {
            oss << static_cast<double>(nanoseconds) / static_cast<double>(ns_per_us) << "us";
        } else {
            oss << nanoseconds << "ns";
        }

        return oss.str();
    }

    /**
     * Formats a byte count in human-readable form.
     *
     * @param bytes The number of bytes.
     * @return Human-readable string like "1.5GB", "250MB", "42KB".
     */
    inline std::string format_bytes(const std::size_t bytes) {
        constexpr std::size_t KB = 1024;
        constexpr std::size_t MB = KB * 1024;
        constexpr std::size_t GB = MB * 1024;

        std::ostringstream oss;
        oss.precision(2);
        oss << std::fixed;

        if (bytes >= GB) {
            oss << static_cast<double>(bytes) / static_cast<double>(GB) << " GB";
        } else if (bytes >= MB) {
            oss << static_cast<double>(bytes) / static_cast<double>(MB) << " MB";
        } else if (bytes >= KB) {
            oss << static_cast<double>(bytes) / static_cast<double>(KB) << " KB";
        } else {
            oss << bytes << " B";
        }

        return oss.str();
    }

}  // namespace bha::string_utils

#endif //BUILDTIMEHOTSPOTANALYZER_STRING_UTILS_HPP