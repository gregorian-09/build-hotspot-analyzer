#pragma once

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
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

        static const std::regex include_regex(R"(^\s*#\s*include\s*([<"])([^">]+)[">])");
        std::string line;
        while (std::getline(in, line)) {
            std::smatch match;
            if (!std::regex_search(line, match, include_regex)) {
                continue;
            }

            ParsedIncludeDirective directive;
            directive.is_system = match[1].str() == "<";
            directive.header_name =
                std::filesystem::path(match[2].str()).lexically_normal().generic_string();
            directives.push_back(std::move(directive));
        }

        return directives;
    }

}  // namespace bha::utils
