#pragma once

#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <type_traits>

namespace bha::utils {

    /// Parses one complete finite floating-point token.
    /// This avoids the unavailable floating-point from_chars overload in libc++ versions
    /// shipped with some AppleClang toolchains while rejecting partial or non-finite input.
    [[nodiscard]] inline std::optional<double> parse_double(const std::string_view text) {
        if (text.empty()) {
            return std::nullopt;
        }

        const std::string token(text);
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(token.c_str(), &end);
        if (errno == ERANGE || end != token.c_str() + token.size() ||
            !std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    }

    template <typename T>
    [[nodiscard]] constexpr std::optional<T> checked_add(
        const T left,
        const T right
    ) noexcept {
        static_assert(std::is_unsigned_v<T>, "checked_add requires an unsigned type");
        if (right > std::numeric_limits<T>::max() - left) {
            return std::nullopt;
        }
        return left + right;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] constexpr std::optional<std::chrono::duration<Rep, Period>> checked_add_duration(
        const std::chrono::duration<Rep, Period> left,
        const std::chrono::duration<Rep, Period> right
    ) noexcept {
        static_assert(std::is_integral_v<Rep>, "checked_add_duration requires an integral representation");

        const Rep left_count = left.count();
        const Rep right_count = right.count();
        if constexpr (std::is_signed_v<Rep>) {
            if ((right_count > 0 && left_count > std::numeric_limits<Rep>::max() - right_count) ||
                (right_count < 0 && left_count < std::numeric_limits<Rep>::min() - right_count)) {
                return std::nullopt;
            }
        } else if (right_count > std::numeric_limits<Rep>::max() - left_count) {
            return std::nullopt;
        }

        return std::chrono::duration<Rep, Period>(left_count + right_count);
    }

    template <typename Rep, typename Period>
    [[nodiscard]] constexpr std::optional<std::chrono::duration<Rep, Period>> checked_sub_duration(
        const std::chrono::duration<Rep, Period> left,
        const std::chrono::duration<Rep, Period> right
    ) noexcept {
        static_assert(std::is_integral_v<Rep>, "checked_sub_duration requires an integral representation");

        const Rep left_count = left.count();
        const Rep right_count = right.count();
        if constexpr (std::is_signed_v<Rep>) {
            if ((right_count > 0 && left_count < std::numeric_limits<Rep>::min() + right_count) ||
                (right_count < 0 && left_count > std::numeric_limits<Rep>::max() + right_count)) {
                return std::nullopt;
            }
        } else if (left_count < right_count) {
            return std::nullopt;
        }

        return std::chrono::duration<Rep, Period>(left_count - right_count);
    }

    /// Converts a floating-point duration to the nearest target tick without
    /// allowing an out-of-range conversion to reach duration's integral rep.
    template <typename ToDuration, typename FromRep, typename FromPeriod>
    [[nodiscard]] std::optional<ToDuration> checked_duration_cast(
        const std::chrono::duration<FromRep, FromPeriod> value
    ) noexcept {
        using ToRep = typename ToDuration::rep;
        using ToPeriod = typename ToDuration::period;
        static_assert(std::is_integral_v<ToRep>, "checked_duration_cast requires an integral target representation");
        static_assert(std::is_floating_point_v<FromRep>, "checked_duration_cast requires a floating source representation");

        using conversion = std::ratio_divide<FromPeriod, ToPeriod>;
        const long double target_count =
            static_cast<long double>(value.count()) *
            static_cast<long double>(conversion::num) /
            static_cast<long double>(conversion::den);
        const long double rounded_count = std::round(target_count);
        if (!std::isfinite(rounded_count) ||
            rounded_count < static_cast<long double>(std::numeric_limits<ToRep>::lowest()) ||
            rounded_count > static_cast<long double>(std::numeric_limits<ToRep>::max())) {
            return std::nullopt;
        }

        return ToDuration(static_cast<ToRep>(rounded_count));
    }

    template <typename Clock>
    [[nodiscard]] constexpr std::optional<typename Clock::time_point> checked_add_time_point(
        const typename Clock::time_point start,
        const typename Clock::duration delta
    ) noexcept {
        const auto sum = checked_add_duration(start.time_since_epoch(), delta);
        if (!sum.has_value()) {
            return std::nullopt;
        }
        return typename Clock::time_point(*sum);
    }

}  // namespace bha::utils
