#pragma once

#include "bha/types.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace bha::utils {

    /**
     * Formats a timestamp to ISO 8601 (UTC, trailing Z).
     */
    [[nodiscard]] inline std::string format_timestamp_iso8601(const Timestamp ts) {
        const auto time_t_val = std::chrono::system_clock::to_time_t(ts);
        std::ostringstream ss;

#ifdef _WIN32
        std::tm time_info{};
        gmtime_s(&time_info, &time_t_val);
        ss << std::put_time(&time_info, "%Y-%m-%dT%H:%M:%SZ");
#else
        ss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");
#endif

        return ss.str();
    }

}  // namespace bha::utils
