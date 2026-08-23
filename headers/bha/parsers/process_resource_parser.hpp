// Created by gregorian-rayne on 8/23/26.

#ifndef BHA_PROCESS_RESOURCE_PARSER_HPP
#define BHA_PROCESS_RESOURCE_PARSER_HPP

#include "bha/error.hpp"
#include "bha/result.hpp"
#include "bha/types.hpp"

#include <string_view>

namespace bha::parsers {

    /**
     * Parses the strict CSV emitted by Clang's -fproc-stat-report=FILE.
     */
    class ProcessResourceParser {
    public:
        [[nodiscard]] Result<ProcessResourceReport, Error> parse_content(
            std::string_view content,
            const fs::path& source_hint = {}
        ) const;

        [[nodiscard]] Result<ProcessResourceReport, Error> parse_file(
            const fs::path& path
        ) const;

        [[nodiscard]] Result<void, Error> attach_to_trace(
            BuildTrace& trace,
            const fs::path& path
        ) const;
    };

}  // namespace bha::parsers

#endif // BHA_PROCESS_RESOURCE_PARSER_HPP
