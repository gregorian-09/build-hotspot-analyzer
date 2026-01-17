//
// Created by gregorian-rayne on 01/16/26.
//

#ifndef BHA_MEMORY_PARSER_HPP
#define BHA_MEMORY_PARSER_HPP

#include "bha/types.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"

#include <filesystem>
#include <string>

namespace bha::parsers
{
    namespace fs = std::filesystem;

    Result<MemoryMetrics, Error> parse_gcc_mem_report(const std::string& stderr_output);

    Result<MemoryMetrics, Error> parse_gcc_stack_usage(const fs::path& su_file);

    Result<MemoryMetrics, Error> parse_msvc_map_file(const fs::path& map_file);

    Result<MemoryMetrics, Error> parse_memory_file(const fs::path& file);

} // namespace bha::parsers

#endif // BHA_MEMORY_PARSER_HPP
