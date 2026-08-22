// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_CMAKE_FILE_API_HPP
#define BHA_CMAKE_FILE_API_HPP

#include "bha/error.hpp"
#include "bha/result.hpp"
#include "bha/types.hpp"

#include <string_view>

namespace bha::build_sessions {

    /**
     * Parser for CMake File API v1 reply indexes and codemodel v2 objects.
     */
    class CMakeFileApiParser {
    public:
        [[nodiscard]] Result<BuildTargetGraph, Error> parse_reply_index(
            const fs::path& index_path,
            std::string_view configuration = {}
        ) const;
    };

}  // namespace bha::build_sessions

#endif // BHA_CMAKE_FILE_API_HPP
