#pragma once

#include "bha/utils/file_utils.hpp"

#include <array>
#include <optional>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace bha::utils {

    inline bool callable_tail_looks_valid(std::string tail) {
        tail = bha::utils::trim_whitespace_copy(std::move(tail));
        while (!tail.empty()) {
            if (tail[0] == ';' || tail[0] == '{') {
                return true;
            }
            if (tail.rfind("noexcept", 0) == 0) {
                tail.erase(0, std::string("noexcept").size());
                tail = bha::utils::trim_whitespace_copy(std::move(tail));
                if (!tail.empty() && tail[0] == '(') {
                    if (const auto span = find_outer_paren_span(tail)) {
                        tail.erase(0, span->second + 1);
                        tail = bha::utils::trim_whitespace_copy(std::move(tail));
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
                tail = bha::utils::trim_whitespace_copy(std::move(tail));
                continue;
            }
            if (tail.rfind("__attribute__", 0) == 0) {
                tail.erase(0, std::string("__attribute__").size());
                tail = bha::utils::trim_whitespace_copy(std::move(tail));
                if (!tail.empty() && tail[0] == '(') {
                    if (const auto span = find_outer_paren_span(tail)) {
                        tail.erase(0, span->second + 1);
                        tail = bha::utils::trim_whitespace_copy(std::move(tail));
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
            const std::string token = tail.substr(0, token_end);
            if (!bha::utils::looks_like_macro_identifier(token)) {
                return false;
            }
            tail.erase(0, token_end);
            tail = bha::utils::trim_whitespace_copy(std::move(tail));
        }
        return false;
    }

    inline std::optional<std::string> extract_callable_name_from_declaration(
        const std::string& declaration
    ) {
        static constexpr std::array<std::string_view, 12> kRejectedPrefixes{
            "#",       "if",       "for",      "while",    "switch",   "return",
            "class ",  "struct ",  "enum ",    "using ",   "typedef ", "static_assert"
        };

        const std::string trimmed = bha::utils::trim_whitespace_copy(declaration);
        if (trimmed.empty()) {
            return std::nullopt;
        }
        for (const auto prefix : kRejectedPrefixes) {
            if (trimmed.rfind(prefix, 0) == 0) {
                return std::nullopt;
            }
        }

        const auto paren_span = bha::utils::find_outer_paren_span(trimmed);
        if (!paren_span.has_value()) {
            return std::nullopt;
        }
        if (!callable_tail_looks_valid(
                bha::utils::trim_whitespace_copy(trimmed.substr(paren_span->second + 1)))) {
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
        if (name == "operator" || bha::utils::looks_like_macro_identifier(name)) {
            return std::nullopt;
        }
        return name;
    }

    inline std::vector<std::string> extract_declared_callable_names(
        const std::filesystem::path& header_path
    ) {
        auto lines_result = bha::utils::read_lines(header_path);
        if (lines_result.is_err()) {
            return {};
        }

        std::vector<std::string> names;
        std::unordered_set<std::string> seen;
        bool in_block_comment = false;
        bool declaration_pending = false;
        std::string declaration;

        for (const auto& raw_line : lines_result.value()) {
            const std::string line = bha::utils::strip_comments_and_strings(raw_line, in_block_comment);
            const std::string trimmed = bha::utils::trim_whitespace_copy(line);
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

}  // namespace bha::utils
