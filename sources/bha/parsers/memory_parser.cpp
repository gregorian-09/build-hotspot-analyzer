//
// Created by gregorian-rayne on 01/16/26.
//

#include "bha/parsers/memory_parser.hpp"

#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>

namespace bha::parsers
{
    Result<MemoryMetrics, Error> parse_gcc_mem_report(const std::string& stderr_output) {
        MemoryMetrics metrics;

        static const std::regex tree_nodes_regex(R"((\d+)\s+kB\s+tree\s+nodes)", std::regex::icase);
        static const std::regex ggc_regex(R"((\d+)\s+kB\s+garbage\s+collection)", std::regex::icase);
        static const std::regex total_mem_regex(R"(TOTAL\s*:\s*(\d+))", std::regex::icase);

        std::smatch match;

        if (std::regex_search(stderr_output, match, tree_nodes_regex)) {
            metrics.parsing_bytes = std::stoull(match[1].str()) * 1024;
        }

        if (std::regex_search(stderr_output, match, ggc_regex)) {
            metrics.ggc_memory = std::stoull(match[1].str()) * 1024;
        }

        if (std::regex_search(stderr_output, match, total_mem_regex)) {
            metrics.peak_memory_bytes = std::stoull(match[1].str()) * 1024;
        }

        if (metrics.peak_memory_bytes == 0 && (metrics.parsing_bytes > 0 || metrics.ggc_memory > 0)) {
            metrics.peak_memory_bytes = metrics.parsing_bytes + metrics.ggc_memory;
        }

        return Result<MemoryMetrics, Error>::success(metrics);
    }

    Result<MemoryMetrics, Error> parse_gcc_stack_usage(const fs::path& su_file) {
        MemoryMetrics metrics;

        std::ifstream file(su_file);
        if (!file.is_open()) {
            return Result<MemoryMetrics, Error>::failure(
                Error(ErrorCode::IoError, "Failed to open .su file: " + su_file.string())
            );
        }

        std::string line;
        std::size_t max_stack = 0;

        while (std::getline(file, line)) {
            static const std::regex su_regex(R"(\s+(\d+)\s+(?:static|dynamic|bounded))");

            if (std::smatch match; std::regex_search(line, match, su_regex)) {
                std::size_t stack_size = std::stoull(match[1].str());
                max_stack = std::max(max_stack, stack_size);
            }
        }

        metrics.max_stack_bytes = max_stack;
        return Result<MemoryMetrics, Error>::success(metrics);
    }

    Result<MemoryMetrics, Error> parse_msvc_map_file(const fs::path& map_file) {
        MemoryMetrics metrics;

        std::ifstream file(map_file);
        if (!file.is_open()) {
            return Result<MemoryMetrics, Error>::failure(
                Error(ErrorCode::IoError, "Failed to open .map file: " + map_file.string())
            );
        }

        std::string line;
        static const std::regex size_regex(R"(([0-9a-fA-F]+)\s+bytes)");

        while (std::getline(file, line)) {
            if (std::smatch match; std::regex_search(line, match, size_regex)) {
                if (std::size_t size = std::stoull(match[1].str(), nullptr, 16); size > metrics.peak_memory_bytes) {
                    metrics.peak_memory_bytes = size;
                }
            }
        }

        return Result<MemoryMetrics, Error>::success(metrics);
    }

    Result<MemoryMetrics, Error> parse_memory_file(const fs::path& file) {
        const std::string ext = file.extension().string();

        if (ext == ".su") {
            return parse_gcc_stack_usage(file);
        }

        if (ext == ".map") {
            return parse_msvc_map_file(file);
        }

        return Result<MemoryMetrics, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Unknown memory file type: " + ext)
        );
    }

} // namespace bha::parsers
