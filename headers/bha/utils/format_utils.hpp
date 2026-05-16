#pragma once

#include "bha/types.hpp"

#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace bha::utils {

    inline std::string format_duration(const Duration d) {
        auto ns = d.count();
        if (ns < 0) ns = 0;

        const auto us = ns / 1000;
        const auto ms = us / 1000;
        const auto seconds = ms / 1000;
        const auto minutes = seconds / 60;
        const auto hours = minutes / 60;

        std::ostringstream ss;
        if (hours > 0) {
            ss << hours << "h " << (minutes % 60) << "m " << (seconds % 60) << "s";
        } else if (minutes > 0) {
            ss << minutes << "m " << (seconds % 60) << "." << ((ms % 1000) / 100) << "s";
        } else if (seconds > 0) {
            ss << seconds << "." << std::setfill('0') << std::setw(2) << ((ms % 1000) / 10) << "s";
        } else if (ms > 0) {
            ss << ms << "." << ((us % 1000) / 100) << "ms";
        } else if (us > 0) {
            ss << us << "μs";
        } else {
            ss << ns << "ns";
        }
        return ss.str();
    }

    inline std::string format_ms(const double ms) {
        std::ostringstream ss;
        if (ms >= 1000.0) {
            ss << std::fixed << std::setprecision(2) << (ms / 1000.0) << "s";
        } else if (ms >= 1.0) {
            ss << std::fixed << std::setprecision(1) << ms << "ms";
        } else {
            ss << std::fixed << std::setprecision(2) << (ms * 1000.0) << "μs";
        }
        return ss.str();
    }

    inline std::string format_percent(const double pct) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << pct << "%";
        return ss.str();
    }

    inline std::string format_size(const std::size_t bytes) {
        int unit_idx = 0;
        auto size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit_idx < 4) {
            size /= 1024.0;
            ++unit_idx;
        }

        std::ostringstream ss;
        if (unit_idx == 0) {
            ss << bytes << " B";
        } else {
            static constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
            ss << std::fixed << std::setprecision(1) << size << " " << units[unit_idx];
        }
        return ss.str();
    }

    inline std::string format_count(const std::size_t count) {
        std::string result = std::to_string(count);
        int insert_pos = static_cast<int>(result.length()) - 3;
        while (insert_pos > 0) {
            result.insert(static_cast<std::size_t>(insert_pos), ",");
            insert_pos -= 3;
        }
        return result;
    }

    inline std::string format_path(const std::filesystem::path& path, const std::size_t max_width = 60) {
        std::string str = path.string();
        if (str.length() <= max_width) {
            return str;
        }
        static constexpr const char* ellipsis = "...";
        return ellipsis + str.substr(str.length() - max_width + 3);
    }

    inline std::string format_timestamp(const Timestamp ts) {
        auto time_t = std::chrono::system_clock::to_time_t(ts);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &time_t);
#else
        localtime_r(&time_t, &tm);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    inline std::string format_timestamp_iso8601(const Timestamp ts) {
        const auto time_t_val = std::chrono::system_clock::to_time_t(ts);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &time_t_val);
#else
        gmtime_r(&time_t_val, &tm);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }

}  // namespace bha::utils
