//
// Created by gregorian on 13/10/2025.
//

#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <optional>
#include <string>
#include <vector>

namespace bhs::utils
{
    /**
     * Split a string into substrings separated by a delimiter.
     *
     * @param str Input text to split.
     * @param delimiter The character to split on.
     * @return A vector of substrings obtained by splitting `str` at each occurrence of `delimiter`.
     *
     * If `str` is empty, returns an empty vector.
     * Empty tokens between consecutive delimiters are preserved.
     * Leading or trailing delimiters produce empty substrings at the ends.
     */
    std::vector<std::string> split(std::string_view str, char delimiter);

    /**
     * Split a string into substrings using a multi-character delimiter.
     *
     * @param str The input string (view) to split.
     * @param delimiter The substring delimiter to split on.
     * @return A vector of substrings resulting from splitting `str` wherever `delimiter` occurs.
     *
     * If `str` is empty, returns an empty vector.
     * If `delimiter` is empty, the behavior is undefined (or you may choose to treat it as splitting into characters).
     * Overlapping occurrences are not allowed: e.g. splitting "aaa" by "aa" yields ["", "a"].
     * Leading or trailing delimiters will produce empty substrings at the ends.
     */
    std::vector<std::string> split(std::string_view str, std::string_view delimiter);

    /**
     * Join a list of strings into a single string, inserting `separator` between them.
     *
     * @param strings The vector of strings to join.
     * @param separator The string to insert between each pair.
     * @return The concatenated result.
     *
     * If `strings` is empty, returns an empty string.
     */
    std::string join(const std::vector<std::string>& strings, std::string_view separator);

    /**
     * Join a list of string views into a single string, inserting `separator` between them.
     *
     * @param strings The vector of string views to join.
     * @param separator The string to insert between each pair.
     * @return The concatenated result.
     *
     * If `strings` is empty, returns an empty string.
     */
    std::string join(const std::vector<std::string_view>& strings, std::string_view separator);

    /**
     * Remove whitespace (or other default trimming characters) from both ends of `str`.
     *
     * @param str The input string view to trim.
     * @return A new string with leading and trailing whitespace removed.
     *
     * If `str` is empty or consists entirely of whitespace, returns an empty string.
     */
    std::string trim(std::string_view str);

    /**
     * Remove whitespace from the left (start) of `str`.
     *
     * @param str The input string view to trim.
     * @return A new string with leading whitespace removed.
     */
    std::string trim_left(std::string_view str);

    /**
     * Remove whitespace from the right (end) of `str`.
     *
     * @param str The input string view to trim.
     * @return A new string with trailing whitespace removed.
     */
    std::string trim_right(std::string_view str);

    /**
     * Check whether `str` begins with the given `prefix`.
     *
     * @param str The input string view.
     * @param prefix The prefix to test.
     * @return `true` if `str` starts with `prefix`, else `false`.
     */
    bool starts_with(std::string_view str, std::string_view prefix);

    /**
     * Check whether `str` ends with the given `suffix`.
     *
     * @param str The input string view.
     * @param suffix The suffix to test.
     * @return `true` if `str` ends with `suffix`, else `false`.
     */
    bool ends_with(std::string_view str, std::string_view suffix);

    /**
     * Check whether `str` contains the substring `substr`.
     *
     * @param str The input string view.
     * @param substr The substring to search for.
     * @return `true` if `substr` is found in `str`, else `false`.
     */
    bool contains(std::string_view str, std::string_view substr);

    /**
     * Check whether `str` contains the character `ch`.
     *
     * @param str The input string view.
     * @param ch The character to search for.
     * @return `true` if `ch` appears in `str`, else `false`.
     */
    bool contains(std::string_view str, char ch);

    /**
     * Convert all characters in `str` to lowercase.
     *
     * @param str The input string view.
     * @return A new string in which every character of `str` is transformed to lowercase.
     */
    std::string to_lower(std::string_view str);

    /**
     * Convert all characters in `str` to uppercase.
     *
     * @param str The input string view.
     * @return A new string in which every character of `str` is transformed to uppercase.
     */
    std::string to_upper(std::string_view str);

    /**
     * Replace **all** occurrences of `from` with `to` in `str`.
     *
     * @param str The input string view.
     * @param from The substring to replace.
     * @param to The substring to replace with.
     * @return A new string with replacements applied.
     *
     * If `from` is empty, behavior is undefined or may be ignored.
     */
    std::string replace_all(std::string_view str, std::string_view from, std::string_view to);

    /**
     * Replace the **first** occurrence of `from` with `to` in `str`.
     *
     * @param str The input string view.
     * @param from The substring to replace.
     * @param to The substring to replace with.
     * @return A new string with the first match replaced; if no match, returns original `str`.
     *
     * If `from` is empty, behavior is undefined or may be ignored.
     */
    std::string replace_first(std::string_view str, std::string_view from, std::string_view to);

    /**
     * Search for `substr` in `str` from beginning to end.
     *
     * @param str The input string view.
     * @param substr The substring to search for.
     * @return The zero-based index of the first occurrence, wrapped in `std::optional`; `std::nullopt` if not found.
     */
    std::optional<size_t> find(std::string_view str, std::string_view substr);

    /**
     * Search for `substr` in `str` from end to beginning.
     *
     * @param str The input string view.
     * @param substr The substring to search for.
     * @return The zero-based index of the last occurrence, wrapped in `std::optional`; `std::nullopt` if not found.
     */
    std::optional<size_t> find_last(std::string_view str, std::string_view substr);

    /**
     * Check whether `str` is empty or contains only whitespace (spaces, tabs, etc.).
     *
     * @param str The input string view.
     * @return `true` if `str` is empty or only whitespace, else `false`.
     */
    bool is_empty_or_whitespace(std::string_view str);

    /**
     * Remove `prefix` from the start of `str`, if present; otherwise return `str` unchanged.
     *
     * @param str The input string view.
     * @param prefix The prefix to remove.
     * @return A new string with `prefix` removed if `str` starts with it, else original `str`.
     */
    std::string remove_prefix(std::string_view str, std::string_view prefix);

    /**
     * Remove `suffix` from the end of `str`, if present; otherwise return `str` unchanged.
     *
     * @param str The input string view.
     * @param suffix The suffix to remove.
     * @return A new string with `suffix` removed if `str` ends with it, else original `str`.
     */
    std::string remove_suffix(std::string_view str, std::string_view suffix);

    /**
     * Split `str` into lines (by newline characters).
     *
     * @param str The input string view (which may contain `\\n` or `\\r\\n`).
     * @return A vector of lines, without newline separators.
     *
     * An empty `str` returns an empty vector. Trailing newline yields an empty last line.
     */
    std::vector<std::string> split_lines(std::string_view str);

    /**
     * Compare two string views ignoring case.
     *
     * @param str1 First string view.
     * @param str2 Second string view.
     * @return `true` if `str1` equals `str2` ignoring character case, else `false`.
     */
    bool equals_ignore_case(std::string_view str1, std::string_view str2);

}

#endif //STRING_UTILS_H
