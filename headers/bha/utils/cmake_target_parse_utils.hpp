#pragma once

#include "bha/utils/cmake_classification_utils.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace bha::utils {

    [[nodiscard]] inline bool is_scope_or_target_keyword(std::string_view token) {
        const std::string key = to_lower_ascii(token);
        static const std::unordered_set<std::string> kKeywords = {
            "public", "private", "interface",
            "before", "after",
            "static", "shared", "module", "object", "alias",
            "exclude_from_all", "win32", "macosx_bundle"
        };
        return kKeywords.contains(key);
    }

    [[nodiscard]] inline std::optional<std::string> extract_builtin_target_name(
        std::string_view command,
        const std::vector<std::string>& tokens,
        const CMakeTargetNameMode mode
    ) {
        if (tokens.empty()) {
            return std::nullopt;
        }
        const std::string lower_command = to_lower_ascii(command);
        if (lower_command == "add_library" || lower_command == "add_executable" ||
            lower_command == "target_sources") {
            if (is_probable_cmake_target_name(tokens.front(), mode)) {
                return tokens.front();
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::vector<std::string> extract_builtin_sources(
        std::string_view command,
        const std::vector<std::string>& tokens,
        const CMakeSourceTokenMode mode
    ) {
        std::vector<std::string> sources;
        if (tokens.size() < 2) {
            return sources;
        }

        const std::string lower_command = to_lower_ascii(command);
        std::size_t i = 1;
        if (lower_command == "add_library" || lower_command == "add_executable") {
            while (i < tokens.size() && is_scope_or_target_keyword(tokens[i])) {
                ++i;
            }
            if (i < tokens.size() && to_lower_ascii(tokens[i]) == "alias") {
                return sources;
            }
            for (; i < tokens.size(); ++i) {
                if (is_probable_source_token(tokens[i], mode)) {
                    sources.push_back(tokens[i]);
                }
            }
            return sources;
        }

        if (lower_command == "target_sources") {
            for (; i < tokens.size(); ++i) {
                if (is_scope_or_target_keyword(tokens[i])) {
                    continue;
                }
                if (is_probable_source_token(tokens[i], mode)) {
                    sources.push_back(tokens[i]);
                }
            }
        }
        return sources;
    }

}  // namespace bha::utils
