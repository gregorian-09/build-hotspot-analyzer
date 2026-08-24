#pragma once

#include <chrono>
#include <limits>
#include <optional>
#include <type_traits>

namespace bha::utils {

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

}  // namespace bha::utils
