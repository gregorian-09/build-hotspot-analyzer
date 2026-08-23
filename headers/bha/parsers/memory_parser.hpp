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
     * Stack usage files are generated with -fstack-usage. Each non-empty record
     * must contain the producer's three tab-separated fields: function name,
     * byte count, and qualifier. Unbounded dynamic records are retained as
     * parsed evidence but are excluded from the bounded maximum.
     *
     * Malformed records fail the parse; valid rows are never inferred from
     * arbitrary whitespace or partially accepted text.
     *
     * @param su_file Path to the `.su` artifact emitted by the compiler.
     * @return Aggregated memory metrics extracted from stack-usage entries.
     */
    Result<MemoryMetrics, Error> parse_stack_usage_file(const fs::path& su_file);

} // namespace bha::parsers

#endif // BHA_MEMORY_PARSER_HPP
