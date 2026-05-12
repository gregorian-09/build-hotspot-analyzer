#pragma once

#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace bha::utils {

    struct MesonTargetSpanInfo {
        std::size_t start_line = 0;
        std::size_t end_line = 0;
        bool single_line = false;
    };

    [[nodiscard]] inline std::optional<MesonTargetSpanInfo> find_first_meson_target_span(
        const std::string& content
    ) {
        const std::regex target_regex(
            R"(^\s*(executable|library|shared_library|static_library)\s*\()",
            std::regex::icase
        );

        std::vector<std::string> lines;
        lines.reserve(128);
        std::istringstream input(content);
        std::string line;
        while (std::getline(input, line)) {
            lines.push_back(line);
        }

        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (!std::regex_search(lines[i], target_regex)) {
                continue;
            }

            const std::size_t open_pos = lines[i].find('(');
            if (open_pos == std::string::npos) {
                continue;
            }

            int paren_depth = 0;
            for (std::size_t j = open_pos; j < lines[i].size(); ++j) {
                if (lines[i][j] == '(') {
                    ++paren_depth;
                }
                if (lines[i][j] == ')') {
                    --paren_depth;
                }
            }

            MesonTargetSpanInfo span;
            span.start_line = i;

            if (paren_depth <= 0) {
                span.end_line = i;
                span.single_line = true;
            } else {
                for (std::size_t k = i + 1; k < lines.size(); ++k) {
                    for (const char c : lines[k]) {
                        if (c == '(') {
                            ++paren_depth;
                        }
                        if (c == ')') {
                            --paren_depth;
                        }
                    }
                    if (paren_depth <= 0) {
                        span.end_line = k;
                        break;
                    }
                }
            }

            return span;
        }

        return std::nullopt;
    }

}  // namespace bha::utils
