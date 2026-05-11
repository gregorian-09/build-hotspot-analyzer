//
// Created by gregorian-rayne on 01/16/26.
//

#ifndef BHA_MEMORY_PARSER_HPP
#define BHA_MEMORY_PARSER_HPP

/**
 * @file memory_parser.hpp
 * @brief Parsers for compiler-emitted memory sidecar artifacts.
 */

#include "bha/types.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"

#include <filesystem>

namespace bha::parsers
{
    namespace fs = std::filesystem;

    /**
     * Parses a GCC/Clang stack usage (.su) file.
     *
     * Stack usage files are generated with -fstack-usage and contain per-function
     * stack consumption data. This is the only reliable memory metric available
     * from standard compiler output.
     *
     * @param su_file Path to the `.su` artifact emitted by the compiler.
     * @return Aggregated memory metrics extracted from stack-usage entries.
     */
    Result<MemoryMetrics, Error> parse_stack_usage_file(const fs::path& su_file);

} // namespace bha::parsers

#endif // BHA_MEMORY_PARSER_HPP
