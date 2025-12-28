//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/error.hpp"

#include <gtest/gtest.h>
#include <sstream>

namespace bha
{
    TEST(ErrorTest, BasicConstruction) {
        const Error error(ErrorCode::InvalidArgument, "invalid value");

        EXPECT_EQ(error.code(), ErrorCode::InvalidArgument);
        EXPECT_EQ(error.message(), "invalid value");
        EXPECT_FALSE(error.has_context());
    }

    TEST(ErrorTest, ConstructionWithContext) {
        const Error error(ErrorCode::NotFound, "file not found", "/path/to/file");

        EXPECT_EQ(error.code(), ErrorCode::NotFound);
        EXPECT_EQ(error.message(), "file not found");
        EXPECT_TRUE(error.has_context());
        EXPECT_EQ(error.context().value(), "/path/to/file");
    }

    TEST(ErrorTest, InvalidArgumentFactory) {
        const auto error = Error::invalid_argument("bad param");
        EXPECT_EQ(error.code(), ErrorCode::InvalidArgument);
        EXPECT_EQ(error.message(), "bad param");

        const auto with_ctx = Error::invalid_argument("bad param", "param_name");
        EXPECT_EQ(with_ctx.context().value(), "param_name");
    }

    TEST(ErrorTest, NotFoundFactory) {
        const auto error = Error::not_found("resource missing");
        EXPECT_EQ(error.code(), ErrorCode::NotFound);
    }

    TEST(ErrorTest, ParseErrorFactory) {
        const auto error = Error::parse_error("invalid JSON", "line 42");
        EXPECT_EQ(error.code(), ErrorCode::ParseError);
        EXPECT_EQ(error.context().value(), "line 42");
    }

    TEST(ErrorTest, IoErrorFactory) {
        const auto error = Error::io_error("read failed");
        EXPECT_EQ(error.code(), ErrorCode::IoError);
    }

    TEST(ErrorTest, ConfigErrorFactory) {
        const auto error = Error::config_error("missing field", "database.host");
        EXPECT_EQ(error.code(), ErrorCode::ConfigError);
        EXPECT_EQ(error.context().value(), "database.host");
    }

    TEST(ErrorTest, AnalysisErrorFactory) {
        const auto error = Error::analysis_error("no trace data");
        EXPECT_EQ(error.code(), ErrorCode::AnalysisError);
    }

    TEST(ErrorTest, PluginErrorFactory) {
        const auto error = Error::plugin_error("failed to load", "my_plugin.so");
        EXPECT_EQ(error.code(), ErrorCode::PluginError);
    }

    TEST(ErrorTest, GitErrorFactory) {
        const auto error = Error::git_error("not a git repository");
        EXPECT_EQ(error.code(), ErrorCode::GitError);
    }

    TEST(ErrorTest, InternalErrorFactory) {
        const auto error = Error::internal_error("unexpected state");
        EXPECT_EQ(error.code(), ErrorCode::InternalError);
    }

    TEST(ErrorTest, WithContext) {
        const auto error = Error::not_found("item missing");
        const auto with_ctx = error.with_context("search_id=123");

        EXPECT_EQ(with_ctx.context().value(), "search_id=123");

        const auto more_ctx = with_ctx.with_context("attempt=2");
        EXPECT_EQ(more_ctx.context().value(), "search_id=123; attempt=2");
    }

    TEST(ErrorTest, ToString) {
        const auto error = Error::parse_error("invalid syntax");
        EXPECT_EQ(error.to_string(), "[ParseError] invalid syntax");

        const auto with_ctx = Error::io_error("open failed", "/tmp/file.txt");
        EXPECT_EQ(with_ctx.to_string(), "[IoError] open failed (context: /tmp/file.txt)");
    }

    TEST(ErrorTest, StreamOutput) {
        const auto error = Error::not_found("missing", "key");
        std::ostringstream oss;
        oss << error;
        EXPECT_EQ(oss.str(), "[NotFound] missing (context: key)");
    }

    TEST(ErrorTest, ErrorCodeToString) {
        EXPECT_STREQ(error_code_to_string(ErrorCode::None), "None");
        EXPECT_STREQ(error_code_to_string(ErrorCode::InvalidArgument), "InvalidArgument");
        EXPECT_STREQ(error_code_to_string(ErrorCode::NotFound), "NotFound");
        EXPECT_STREQ(error_code_to_string(ErrorCode::ParseError), "ParseError");
        EXPECT_STREQ(error_code_to_string(ErrorCode::IoError), "IoError");
        EXPECT_STREQ(error_code_to_string(ErrorCode::ConfigError), "ConfigError");
        EXPECT_STREQ(error_code_to_string(ErrorCode::AnalysisError), "AnalysisError");
        EXPECT_STREQ(error_code_to_string(ErrorCode::PluginError), "PluginError");
        EXPECT_STREQ(error_code_to_string(ErrorCode::GitError), "GitError");
        EXPECT_STREQ(error_code_to_string(ErrorCode::InternalError), "InternalError");
    }

    TEST(ErrorTest, ErrorCodeStreamOutput) {
        std::ostringstream oss;
        oss << ErrorCode::ParseError;
        EXPECT_EQ(oss.str(), "ParseError");
    }

    TEST(ErrorTest, Equality) {
        auto e1 = Error::not_found("missing", "key");
        auto e2 = Error::not_found("missing", "key");
        auto e3 = Error::not_found("missing", "other");
        auto e4 = Error::io_error("missing", "key");

        EXPECT_EQ(e1, e2);
        EXPECT_NE(e1, e3);
        EXPECT_NE(e1, e4);
    }

}  // namespace bha