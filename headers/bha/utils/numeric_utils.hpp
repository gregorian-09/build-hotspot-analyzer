#pragma once

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

}  // namespace bha::utils
