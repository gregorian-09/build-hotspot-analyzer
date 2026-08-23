// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_CMAKE_INSTRUMENTATION_HPP
#define BHA_CMAKE_INSTRUMENTATION_HPP

#include "bha/error.hpp"
#include "bha/result.hpp"
#include "bha/types.hpp"

#include <string_view>

namespace bha::build_sessions {

    /**
     * Parser for CMake Instrumentation API v1 snippet files.
     */
    class CMakeInstrumentationParser {
    public:
        [[nodiscard]] Result<BuildCommandEvent, Error> parse_content(
            std::string_view content,
            const fs::path& source_hint
        ) const;

        [[nodiscard]] Result<BuildCommandEvent, Error> parse_file(
            const fs::path& path
        ) const;

        /**
         * Parses one API v1 index file and the snippet files it references.
         * The index is the producer-defined session boundary.
         */
        [[nodiscard]] Result<BuildSession, Error> parse_index_file(
            const fs::path& path
        ) const;

        /**
         * Attaches one producer-defined index and its referenced Clang traces.
         *
         * References are consumed exactly as emitted by CMake; no build-tree
         * scan or filename-based trace matching is performed.
         */
        [[nodiscard]] Result<void, Error> attach_to_trace(
            BuildTrace& trace,
            const fs::path& path
        ) const;

        /**
         * Parses valid snippet JSON files directly in one directory. For a
         * producer-defined session boundary, use parse_index_file().
         */
        [[nodiscard]] Result<BuildSession, Error> parse_directory(
            const fs::path& directory
        ) const;
    };

}  // namespace bha::build_sessions

#endif // BHA_CMAKE_INSTRUMENTATION_HPP
