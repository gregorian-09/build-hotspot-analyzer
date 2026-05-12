#pragma once

#include "bha/suggestions/suggester.hpp"

#include <algorithm>
#include <optional>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace bha::utils {

    [[nodiscard]] inline std::string strip_attribute_sequences(std::string text) {
        const auto erase_balanced = [&](const std::string& open, const std::string& close) {
            std::size_t pos = 0;
            while ((pos = text.find(open, pos)) != std::string::npos) {
                const std::size_t end = text.find(close, pos + open.size());
                if (end == std::string::npos) {
                    text.erase(pos);
                    break;
                }
                text.erase(pos, end + close.size() - pos);
            }
        };

        erase_balanced("[[", "]]");

        static const std::regex attribute_regex(R"(\b(?:__attribute__|alignas)\s*\([^)]*\))");
        text = std::regex_replace(text, attribute_regex, " ");
        return text;
    }

    [[nodiscard]] inline std::optional<std::string> extract_declared_type_name(
        const std::string& declaration_tail
    ) {
        const std::string sanitized = strip_attribute_sequences(declaration_tail);
        static const std::regex identifier_regex(R"([A-Za-z_][A-Za-z0-9_]*)");
        std::vector<std::string> tokens;
        for (auto begin = std::sregex_iterator(sanitized.begin(), sanitized.end(), identifier_regex),
                  end = std::sregex_iterator();
             begin != end;
             ++begin) {
            tokens.push_back((*begin).str());
        }

        auto is_non_name_token = [](const std::string& token) {
            static const std::unordered_set<std::string> blocked{
                "class",
                "struct",
                "final",
                "override",
                "alignas",
                "__attribute__",
                "__declspec",
                "declspec",
                "nodiscard",
                "maybe_unused"
            };
            return blocked.contains(token);
        };

        tokens.erase(
            std::remove_if(tokens.begin(), tokens.end(), is_non_name_token),
            tokens.end()
        );

        while (tokens.size() > 1 && suggestions::looks_like_macro_identifier(tokens.front())) {
            tokens.erase(tokens.begin());
        }
        while (tokens.size() > 1 && suggestions::looks_like_macro_identifier(tokens.back())) {
            tokens.pop_back();
        }

        if (tokens.empty()) {
            return std::nullopt;
        }

        return tokens.front();
    }

}  // namespace bha::utils
