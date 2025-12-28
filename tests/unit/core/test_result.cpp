//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/result.hpp"
#include "bha/error.hpp"

#include <gtest/gtest.h>
#include <string>

namespace bha
{
    TEST(ResultTest, SuccessConstruction) {
        auto result = Result<int, Error>::success(42);

        EXPECT_TRUE(result.is_ok());
        EXPECT_FALSE(result.is_err());
        EXPECT_TRUE(static_cast<bool>(result));
        EXPECT_EQ(result.value(), 42);
    }

    TEST(ResultTest, FailureConstruction) {
        auto result = Result<int, Error>::failure(Error::not_found("item not found"));

        EXPECT_FALSE(result.is_ok());
        EXPECT_TRUE(result.is_err());
        EXPECT_FALSE(static_cast<bool>(result));
        EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
    }

    TEST(ResultTest, ValueThrowsOnError) {
        auto result = Result<int, Error>::failure(Error::invalid_argument("bad arg"));

        EXPECT_THROW(result.value(), std::logic_error);
    }

    TEST(ResultTest, ErrorThrowsOnSuccess) {
        auto result = Result<int, Error>::success(10);

        EXPECT_THROW(result.error(), std::logic_error);
    }

    TEST(ResultTest, ValueOr) {
        const auto success = Result<int, Error>::success(42);
        const auto failure = Result<int, Error>::failure(Error::internal_error("oops"));

        EXPECT_EQ(success.value_or(0), 42);
        EXPECT_EQ(failure.value_or(0), 0);
    }

    TEST(ResultTest, MapOnSuccess) {
        const auto result = Result<int, Error>::success(10);
        auto mapped = result.map([](const int x) { return x * 2; });

        EXPECT_TRUE(mapped.is_ok());
        EXPECT_EQ(mapped.value(), 20);
    }

    TEST(ResultTest, MapOnFailure) {
        const auto result = Result<int, Error>::failure(Error::parse_error("invalid"));
        auto mapped = result.map([](int x) { return x * 2; });

        EXPECT_TRUE(mapped.is_err());
        EXPECT_EQ(mapped.error().code(), ErrorCode::ParseError);
    }

    TEST(ResultTest, AndThenOnSuccess) {
        const auto result = Result<int, Error>::success(10);
        auto chained = result.and_then([](const int x) {
            return Result<std::string, Error>::success(std::to_string(x));
        });

        EXPECT_TRUE(chained.is_ok());
        EXPECT_EQ(chained.value(), "10");
    }

    TEST(ResultTest, AndThenOnFailure) {
        const auto result = Result<int, Error>::failure(Error::io_error("read failed"));
        auto chained = result.and_then([](int x) {
            return Result<std::string, Error>::success(std::to_string(x));
        });

        EXPECT_TRUE(chained.is_err());
        EXPECT_EQ(chained.error().code(), ErrorCode::IoError);
    }

    TEST(ResultTest, OrElseOnSuccess) {
        const auto result = Result<int, Error>::success(42);
        auto recovered = result.or_else([](const Error&) {
            return Result<int, Error>::success(0);
        });

        EXPECT_TRUE(recovered.is_ok());
        EXPECT_EQ(recovered.value(), 42);
    }

    TEST(ResultTest, OrElseOnFailure) {
        const auto result = Result<int, Error>::failure(Error::not_found("missing"));
        auto recovered = result.or_else([](const Error&) {
            return Result<int, Error>::success(0);
        });

        EXPECT_TRUE(recovered.is_ok());
        EXPECT_EQ(recovered.value(), 0);
    }

    TEST(ResultTest, MoveSemantics) {
        auto result = Result<std::string, Error>::success("hello");
        const std::string value = std::move(result).value();

        EXPECT_EQ(value, "hello");
    }

    TEST(VoidResultTest, SuccessConstruction) {
        const auto result = Result<void, Error>::success();

        EXPECT_TRUE(result.is_ok());
        EXPECT_FALSE(result.is_err());
    }

    TEST(VoidResultTest, FailureConstruction) {
        auto result = Result<void, Error>::failure(Error::config_error("bad config"));

        EXPECT_FALSE(result.is_ok());
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code(), ErrorCode::ConfigError);
    }

    TEST(VoidResultTest, AndThenOnSuccess) {
        const auto result = Result<void, Error>::success();
        int counter = 0;
        const auto chained = result.and_then([&counter]() {
            ++counter;
            return Result<void, Error>::success();
        });

        EXPECT_TRUE(chained.is_ok());
        EXPECT_EQ(counter, 1);
    }

    TEST(VoidResultTest, AndThenOnFailure) {
        const auto result = Result<void, Error>::failure(Error::git_error("not a repo"));
        int counter = 0;
        const auto chained = result.and_then([&counter]() {
            ++counter;
            return Result<void, Error>::success();
        });

        EXPECT_TRUE(chained.is_err());
        EXPECT_EQ(counter, 0);
    }

}  // namespace bha