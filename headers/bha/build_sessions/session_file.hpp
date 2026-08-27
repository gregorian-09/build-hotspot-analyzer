#ifndef BHA_BUILD_SESSION_FILE_HPP
#define BHA_BUILD_SESSION_FILE_HPP

#include "bha/error.hpp"
#include "bha/result.hpp"
#include "bha/types.hpp"

#include <string_view>

namespace bha::build_sessions {

    inline constexpr std::string_view kBuildSessionFileName = "bha-build-session.json";

    /**
     * Reads and writes the portable BHA build-session sidecar.
     *
     * The sidecar stores adapter-observed wall time and any producer command
     * events that were available. It never infers compiler or scheduler data.
     */
    class BuildSessionFileParser {
    public:
        [[nodiscard]] Result<BuildSession, Error> parse_content(
            std::string_view content,
            const fs::path& source_hint = {}
        ) const;

        [[nodiscard]] Result<BuildSession, Error> parse_file(
            const fs::path& path
        ) const;

        [[nodiscard]] Result<void, Error> attach_to_trace(
            BuildTrace& trace,
            const fs::path& path
        ) const;

        [[nodiscard]] Result<void, Error> write_file(
            const BuildSession& session,
            const fs::path& path
        ) const;
    };

}  // namespace bha::build_sessions

#endif  // BHA_BUILD_SESSION_FILE_HPP
