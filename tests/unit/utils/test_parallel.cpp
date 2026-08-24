#include "bha/utils/parallel.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <vector>

namespace bha::utils::test {

    TEST(ParallelUtilsTest, ReducesWithExplicitThreadPool) {
        ThreadPool pool(2);
        const std::vector<int> values{1, 2, 3, 4, 5};

        const auto result = reduce(values, 0, std::plus<int>{}, pool);

        EXPECT_EQ(result, 15);
    }

    TEST(ParallelUtilsTest, ReducesWithAutomaticThreadPool) {
        ThreadPool pool;
        const std::vector<int> values{7, 11, 13};

        const auto result = reduce(values, 0, std::plus<int>{}, pool);

        EXPECT_EQ(result, 31);
    }

    TEST(ParallelUtilsTest, ReturnsInitialValueForEmptyInput) {
        ThreadPool pool(2);

        const auto result = reduce(std::vector<int>{}, 42, std::plus<int>{}, pool);

        EXPECT_EQ(result, 42);
    }

}  // namespace bha::utils::test
