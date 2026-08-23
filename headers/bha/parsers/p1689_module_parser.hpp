// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_P1689_MODULE_PARSER_HPP
#define BHA_P1689_MODULE_PARSER_HPP

#include "bha/error.hpp"
#include "bha/result.hpp"
#include "bha/types.hpp"

#include <string_view>

namespace bha::parsers {

    /**
     * Parses the P1689 JSON emitted by clang-scan-deps -format=p1689.
     *
     * The parser accepts only the versioned producer schema. It does not
     * recover module ownership from paths, filenames, or compiler commands.
     */
    class P1689ModuleParser {
    public:
        [[nodiscard]] Result<ModuleDependencyGraph, Error> parse_content(
            std::string_view content,
            const fs::path& source_hint = {}
        ) const;

        [[nodiscard]] Result<ModuleDependencyGraph, Error> parse_file(
            const fs::path& path
        ) const;

        [[nodiscard]] Result<void, Error> attach_to_trace(
            BuildTrace& trace,
            const fs::path& path
        ) const;
    };

}  // namespace bha::parsers

#endif // BHA_P1689_MODULE_PARSER_HPP
