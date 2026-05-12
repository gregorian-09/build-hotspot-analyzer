#pragma once

#include "bha/utils/cmake_classification_utils.hpp"

#include <optional>
#include <string>
#include <vector>

namespace bha::utils {

    [[nodiscard]] inline std::optional<std::string> extract_cmake_macro_target_name(
        const std::vector<std::string>& tokens,
        const CMakeTargetNameMode mode
    ) {
        if (tokens.empty()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (to_lower_ascii(tokens[i]) != "name") {
                continue;
            }
            if (is_probable_cmake_target_name(tokens[i + 1], mode)) {
                return tokens[i + 1];
            }
            return std::nullopt;
        }
        if (is_probable_cmake_target_name(tokens.front(), mode)) {
            return tokens.front();
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::vector<std::string> extract_cmake_macro_sources(
        const std::vector<std::string>& tokens,
        const CMakeSourceTokenMode mode
    ) {
        std::vector<std::string> sources;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const std::string key = to_lower_ascii(tokens[i]);
            if (key != "srcs" && key != "sources" && key != "src" && key != "source") {
                continue;
            }
            for (std::size_t j = i + 1; j < tokens.size(); ++j) {
                if (is_macro_keyword_lower(tokens[j])) {
                    break;
                }
                if (is_probable_source_token(tokens[j], mode)) {
                    sources.push_back(tokens[j]);
                }
            }
        }
        return sources;
    }

}  // namespace bha::utils
