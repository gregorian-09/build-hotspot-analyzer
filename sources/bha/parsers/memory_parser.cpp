//
// Created by gregorian-rayne on 01/16/26.
//

#include "bha/parsers/memory_parser.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

namespace bha::parsers
{
    namespace {

        Result<std::vector<std::string_view>, Error> split_record(
            const std::string_view line,
            const fs::path& source_hint
        ) {
            std::vector<std::string_view> fields;
            std::size_t start = 0;
            while (true) {
                const std::size_t separator = line.find('\t', start);
                fields.push_back(line.substr(
                    start,
                    separator == std::string_view::npos ? line.size() - start : separator - start
                ));
                if (separator == std::string_view::npos) {
                    break;
                }
                start = separator + 1;
            }

            if (fields.size() != 3 || fields[0].empty() || fields[1].empty() || fields[2].empty()) {
                return Result<std::vector<std::string_view>, Error>::failure(
                    Error::parse_error(
                        "Stack usage record must contain function, bytes, and qualifier fields",
                        source_hint.string()
                    )
                );
            }
            return Result<std::vector<std::string_view>, Error>::success(std::move(fields));
        }

        Result<std::size_t, Error> parse_bytes(
            const std::string_view value,
            const fs::path& source_hint
        ) {
            std::size_t bytes = 0;
            const auto [end, error] = std::from_chars(
                value.data(),
                value.data() + value.size(),
                bytes
            );
            if (error != std::errc{} || end != value.data() + value.size()) {
                return Result<std::size_t, Error>::failure(
                    Error::parse_error("Invalid stack usage byte count", source_hint.string())
                );
            }
            return Result<std::size_t, Error>::success(bytes);
        }

        bool is_reliable_qualifier(const std::string_view qualifier) {
            return qualifier == "static" ||
                   qualifier == "bounded" ||
                   qualifier == "dynamic,bounded";
        }

    }  // namespace

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
        std::size_t line_number = 0;

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

            const auto fields = split_record(line, su_file);
            if (fields.is_err()) {
                return Result<MemoryMetrics, Error>::failure(
                    Error::parse_error(
                        "Invalid stack usage record at line " + std::to_string(line_number) +
                            ": " + fields.error().message(),
                        su_file.string()
                    )
                );
            }

            const auto stack_size = parse_bytes(fields.value()[1], su_file);
            if (stack_size.is_err()) {
                return Result<MemoryMetrics, Error>::failure(
                    Error::parse_error(
                        "Invalid stack usage record at line " + std::to_string(line_number) +
                            ": " + stack_size.error().message(),
                        su_file.string()
                    )
                );
            }

            if (is_reliable_qualifier(fields.value()[2])) {
                max_stack = std::max(max_stack, stack_size.value());
            } else if (fields.value()[2] != "dynamic") {
                return Result<MemoryMetrics, Error>::failure(
                    Error::parse_error(
                        "Unsupported stack usage qualifier at line " + std::to_string(line_number),
                        su_file.string()
                    )
                );
            }
        }

        metrics.max_stack_bytes = max_stack;
        return Result<MemoryMetrics, Error>::success(metrics);
    }

} // namespace bha::parsers
