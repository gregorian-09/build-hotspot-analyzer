// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_LLD_TRACE_PARSER_HPP
#define BHA_LLD_TRACE_PARSER_HPP

#include "bha/error.hpp"
#include "bha/result.hpp"
#include "bha/types.hpp"

#include <string_view>

namespace bha::parsers {

    /**
     * Parser for LLD Chrome Trace Event JSON produced by --time-trace.
     */
    class LLDTimeTraceParser {
    public:
        [[nodiscard]] Result<LinkerTrace, Error> parse_content(
            std::string_view content,
            const fs::path& source_hint
        ) const;

        [[nodiscard]] Result<LinkerTrace, Error> parse_file(
            const fs::path& path
        ) const;
    };

}  // namespace bha::parsers

#endif // BHA_LLD_TRACE_PARSER_HPP
