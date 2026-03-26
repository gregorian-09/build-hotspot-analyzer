//
// Created by gregorian-rayne on 01/16/26.
//

#include "bha/parsers/memory_parser.hpp"

#include <fstream>
#include <regex>
#include <algorithm>
#include <iostream>

namespace bha::parsers
{
    Result<MemoryMetrics, Error> parse_stack_usage_file(const fs::path& su_file) {
        MemoryMetrics metrics;

        std::ifstream file(su_file);
        if (!file.is_open()) {
            return Result<MemoryMetrics, Error>::failure(
                Error(ErrorCode::IoError, "Failed to open .su file: " + su_file.string())
            );
        }

        std::string line;
        std::size_t max_stack = 0;
        std::size_t max_unreliable_stack = 0;
        std::size_t line_number = 0;
        std::size_t parsed_lines = 0;
        std::size_t skipped_unreliable = 0;

        static const std::regex su_regex(
            R"(^(.+?)\s+(\d+)\s+(static|dynamic|dynamic,bounded|bounded)\s*$)"
        );

        while (std::getline(file, line)) {
            ++line_number;

            if (line.empty()) {
                continue;
            }

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) {
                continue;
            }

            if (std::smatch match; std::regex_match(line, match, su_regex)) {
                const std::size_t stack_size = std::stoull(match[2].str());

                if (const std::string qualifier = match[3].str();
                    qualifier == "static" || qualifier == "dynamic,bounded" || qualifier == "bounded") {
                    max_stack = std::max(max_stack, stack_size);
                    ++parsed_lines;
                } else if (qualifier == "dynamic") {
                    max_unreliable_stack = std::max(max_unreliable_stack, stack_size);
                    ++skipped_unreliable;
                }
            } else {
                std::cerr << "Warning: Failed to parse .su file line " << line_number
                          << " in " << su_file.filename().string() << ": " << line << std::endl;
            }
        }

        if (parsed_lines == 0 && skipped_unreliable == 0 && line_number > 0) {
            std::cerr << "Warning: No valid stack usage entries found in "
                      << su_file.string() << " (" << line_number << " lines)" << std::endl;
        }

        if (skipped_unreliable > 0) {
            std::cerr << "Info: Skipped " << skipped_unreliable
                      << " unreliable 'dynamic' measurements in "
                      << su_file.filename().string()
                      << " (max unreliable: " << max_unreliable_stack << " bytes)" << std::endl;
        }

        metrics.max_stack_bytes = max_stack;
        return Result<MemoryMetrics, Error>::success(metrics);
    }

} // namespace bha::parsers
