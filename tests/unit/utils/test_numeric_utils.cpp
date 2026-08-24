#include <chrono>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "bha/utils/numeric_utils.hpp"

namespace bha::utils::test {

    TEST(NumericUtilsTest, CheckedAddReturnsExactUnsignedSum) {
        const auto result = checked_add<std::uint64_t>(12, 30);

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, 42u);
    }

    TEST(NumericUtilsTest, CheckedAddRejectsUnsignedOverflow) {
        const auto result = checked_add<std::uint64_t>(
            std::numeric_limits<std::uint64_t>::max(),
            1
        );

        EXPECT_FALSE(result.has_value());
    }

    TEST(NumericUtilsTest, CheckedAddDurationReturnsExactSignedSum) {
        const auto result = checked_add_duration(
            std::chrono::nanoseconds(12),
            std::chrono::nanoseconds(-5)
        );

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, std::chrono::nanoseconds(7));
    }

    TEST(NumericUtilsTest, CheckedAddDurationRejectsSignedOverflow) {
        const auto result = checked_add_duration(
            std::chrono::nanoseconds::max(),
            std::chrono::nanoseconds(1)
        );

        EXPECT_FALSE(result.has_value());
    }

}  // namespace bha::utils::test
