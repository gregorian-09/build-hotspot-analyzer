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

    TEST(NumericUtilsTest, CheckedSubDurationReturnsExactSignedDifference) {
        const auto result = checked_sub_duration(
            std::chrono::nanoseconds(12),
            std::chrono::nanoseconds(5)
        );

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, std::chrono::nanoseconds(7));
    }

    TEST(NumericUtilsTest, CheckedSubDurationRejectsSignedOverflow) {
        const auto result = checked_sub_duration(
            std::chrono::nanoseconds::min(),
            std::chrono::nanoseconds(1)
        );

        EXPECT_FALSE(result.has_value());
    }

    TEST(NumericUtilsTest, CheckedAddTimePointRejectsOverflow) {
        using Clock = std::chrono::steady_clock;

        EXPECT_FALSE(
            checked_add_time_point<Clock>(
                Clock::time_point::max(),
                Clock::duration(1)
            ).has_value()
        );
        EXPECT_TRUE(
            checked_add_time_point<Clock>(
                Clock::time_point{},
                Clock::duration(1)
            ).has_value()
        );
    }

    TEST(NumericUtilsTest, CheckedDurationCastPreservesRepresentableValue) {
        const auto result = checked_duration_cast<decltype(std::chrono::nanoseconds{})>(
            std::chrono::duration<double>(1.25)
        );

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, std::chrono::milliseconds(1250));
    }

    TEST(NumericUtilsTest, CheckedDurationCastRejectsUnrepresentableValue) {
        const auto result = checked_duration_cast<decltype(std::chrono::nanoseconds{})>(
            std::chrono::duration<double, std::micro>(1e30)
        );

        EXPECT_FALSE(result.has_value());
    }

}  // namespace bha::utils::test
