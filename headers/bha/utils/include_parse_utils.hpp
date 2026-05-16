#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bha::utils {

    struct ParsedIncludeDirective {
        std::string header_name;
        bool is_system = false;
    };

    [[nodiscard]] inline std::vector<ParsedIncludeDirective> parse_include_directives_from_file(
        const std::filesystem::path& file_path
    ) {
        std::vector<ParsedIncludeDirective> directives;

        std::ifstream in(file_path);
        if (!in.is_open()) {
            return directives;
        }

        constexpr std::string_view include_kwd = "include";
        std::string line;
        while (std::getline(in, line)) {
            const std::string_view sv(line);
            std::size_t pos = 0;

            // Skip leading whitespace
            while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t')) {
                ++pos;
            }

            // Must start with #
            if (pos >= sv.size() || sv[pos] != '#') {
                continue;
            }
            ++pos;

            // Skip whitespace after #
            while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t')) {
                ++pos;
            }

            // Check for "include" keyword
            if (pos + include_kwd.size() > sv.size()) {
                continue;
            }
            if (sv.substr(pos, include_kwd.size()) != include_kwd) {
                continue;
            }
            pos += include_kwd.size();

            // Skip whitespace before delimiter
            while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t')) {
                ++pos;
            }

            // Check for opening delimiter
            if (pos >= sv.size()) {
                continue;
            }
            const char delim = sv[pos];
            if (delim != '"' && delim != '<') {
                continue;
            }
            const char close_delim = (delim == '<') ? '>' : '"';
            ++pos;

            // Read header name until closing delimiter
            const std::size_t start = pos;
            while (pos < sv.size() && sv[pos] != close_delim) {
                ++pos;
            }
            if (pos >= sv.size()) {
                continue;
            }

            ParsedIncludeDirective directive;
            directive.is_system = (delim == '<');
            directive.header_name =
                std::filesystem::path(sv.substr(start, pos - start)).lexically_normal().generic_string();
            directives.push_back(std::move(directive));
        }

        return directives;
    }

}  // namespace bha::utils
