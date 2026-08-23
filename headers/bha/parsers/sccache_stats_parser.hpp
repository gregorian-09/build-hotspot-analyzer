// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_SCCACHE_STATS_PARSER_HPP
#define BHA_SCCACHE_STATS_PARSER_HPP

#include "bha/error.hpp"
#include "bha/result.hpp"
#include "bha/types.hpp"

#include <string_view>

namespace bha::parsers {

    /**
     * Parses the producer-defined sccache JSON statistics output.
     *
     * Human-readable ccache/sccache output is intentionally not accepted.
     */
    class SccacheStatsParser {
    public:
        [[nodiscard]] Result<CacheStatistics, Error> parse_content(
            std::string_view content,
            const fs::path& source_hint = {}
        ) const;

        [[nodiscard]] Result<CacheStatistics, Error> parse_file(
            const fs::path& path
        ) const;

        [[nodiscard]] Result<void, Error> attach_to_trace(
            BuildTrace& trace,
            const fs::path& path
        ) const;
    };

}  // namespace bha::parsers

#endif // BHA_SCCACHE_STATS_PARSER_HPP
