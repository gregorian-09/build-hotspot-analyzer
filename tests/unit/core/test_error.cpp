//
// Created by gregorian on 08/12/2025.
//

#include <gtest/gtest.h>
#include "bha/core/error.h"
#include <cstring>

using namespace bha::core;

TEST(ErrorTest, DefaultConstruct) {
    const Error err;
    EXPECT_EQ(err.code, ErrorCode::SUCCESS);
    EXPECT_TRUE(err.message.empty());
    EXPECT_EQ(err.severity, ErrorSeverity::WARNING);
    EXPECT_TRUE(err.suggestions.empty());
}

TEST(ErrorTest, ConstructWithCodeAndMessage) {
    const Error err(ErrorCode::FILE_NOT_FOUND, "test.txt not found");
    EXPECT_EQ(err.code, ErrorCode::FILE_NOT_FOUND);
    EXPECT_EQ(err.message, "test.txt not found");
    EXPECT_EQ(err.severity, ErrorSeverity::ERROR);
    EXPECT_FALSE(err.file.empty());
    EXPECT_GT(err.line, 0u);
}

TEST(ErrorTest, ConstructWithSuggestions) {
    const std::vector<std::string> suggestions = {"Check file path", "Verify permissions"};
    const Error err(ErrorCode::FILE_READ_ERROR, "Cannot read file", suggestions);
    EXPECT_EQ(err.code, ErrorCode::FILE_READ_ERROR);
    EXPECT_EQ(err.message, "Cannot read file");
    ASSERT_EQ(err.suggestions.size(), 2);
    EXPECT_EQ(err.suggestions[0], "Check file path");
    EXPECT_EQ(err.suggestions[1], "Verify permissions");
}

TEST(ErrorTest, ConstructWithCustomSeverity) {
    const Error err(ErrorCode::INTERNAL_ERROR, "Critical failure", ErrorSeverity::FATAL);
    EXPECT_EQ(err.severity, ErrorSeverity::FATAL);
    EXPECT_TRUE(err.is_fatal());
    EXPECT_FALSE(err.is_recoverable());
}

TEST(ErrorTest, ToString) {
    const Error err(ErrorCode::PARSE_ERROR, "Invalid JSON");
    const auto str = err.to_string();
    EXPECT_FALSE(str.empty());
    EXPECT_TRUE(str.find("Parse error") != std::string::npos);
    EXPECT_TRUE(str.find("Invalid JSON") != std::string::npos);
}

TEST(ErrorTest, ToStringWithSuggestions) {
    const std::vector<std::string> suggestions = {"Check syntax", "Validate format"};
    const Error err(ErrorCode::JSON_PARSE_ERROR, "Malformed JSON", suggestions);
    const auto str = err.to_string();
    EXPECT_TRUE(str.find("Check syntax") != std::string::npos);
    EXPECT_TRUE(str.find("Validate format") != std::string::npos);
}

TEST(ErrorTest, IsFatal) {
    Error fatal(ErrorCode::OUT_OF_MEMORY, "Memory exhausted", ErrorSeverity::FATAL);
    EXPECT_TRUE(fatal.is_fatal());

    Error warning(ErrorCode::FILE_NOT_FOUND, "Optional file missing", ErrorSeverity::WARNING);
    EXPECT_FALSE(warning.is_fatal());

    Error error(ErrorCode::PARSE_ERROR, "Parse failed", ErrorSeverity::ERROR);
    EXPECT_FALSE(error.is_fatal());
}

TEST(ErrorTest, IsRecoverable) {
    Error warning(ErrorCode::VALIDATION_ERROR, "Minor issue", ErrorSeverity::WARNING);
    EXPECT_TRUE(warning.is_recoverable());

    Error error(ErrorCode::INVALID_ARGUMENT, "Bad input", ErrorSeverity::ERROR);
    EXPECT_TRUE(error.is_recoverable());

    Error fatal(ErrorCode::INTERNAL_ERROR, "Fatal error", ErrorSeverity::FATAL);
    EXPECT_FALSE(fatal.is_recoverable());
}

TEST(ErrorCodeTest, ToString) {
    EXPECT_STREQ(error_code_to_string(ErrorCode::SUCCESS), "Success");
    EXPECT_STREQ(error_code_to_string(ErrorCode::FILE_NOT_FOUND), "File not found");
    EXPECT_STREQ(error_code_to_string(ErrorCode::PARSE_ERROR), "Parse error");
    EXPECT_STREQ(error_code_to_string(ErrorCode::OUT_OF_MEMORY), "Out of memory");
    EXPECT_STREQ(error_code_to_string(ErrorCode::UNKNOWN_ERROR), "Unknown error");
}

TEST(ErrorCodeTest, ToSeverity) {
    EXPECT_EQ(error_code_to_severity(ErrorCode::SUCCESS), ErrorSeverity::WARNING);
    EXPECT_EQ(error_code_to_severity(ErrorCode::FILE_NOT_FOUND), ErrorSeverity::ERROR);
    EXPECT_EQ(error_code_to_severity(ErrorCode::OUT_OF_MEMORY), ErrorSeverity::FATAL);
    EXPECT_EQ(error_code_to_severity(ErrorCode::PERMISSION_DENIED), ErrorSeverity::ERROR);
}

TEST(ErrorFactoryTest, MakeError) {
    const auto err = make_error(ErrorCode::INVALID_PATH, "Invalid path specified");
    EXPECT_EQ(err.code, ErrorCode::INVALID_PATH);
    EXPECT_EQ(err.message, "Invalid path specified");
    EXPECT_EQ(err.severity, error_code_to_severity(ErrorCode::INVALID_PATH));
    EXPECT_FALSE(err.file.empty());
}

TEST(ErrorFactoryTest, MakeErrorWithSuggestions) {
    const std::vector<std::string> suggestions = {"Use absolute path", "Check directory exists"};
    const auto err = make_error_with_suggestions(
        ErrorCode::INVALID_PATH,
        "Cannot find directory",
        suggestions
    );
    EXPECT_EQ(err.code, ErrorCode::INVALID_PATH);
    EXPECT_EQ(err.message, "Cannot find directory");
    ASSERT_EQ(err.suggestions.size(), 2);
    EXPECT_EQ(err.suggestions[0], "Use absolute path");
    EXPECT_EQ(err.suggestions[1], "Check directory exists");
}

TEST(ErrorTest, AllErrorCodes) {
    const std::vector all_codes = {
        ErrorCode::SUCCESS,
        ErrorCode::FILE_NOT_FOUND,
        ErrorCode::FILE_READ_ERROR,
        ErrorCode::FILE_WRITE_ERROR,
        ErrorCode::FILE_PARSE_ERROR,
        ErrorCode::INVALID_PATH,
        ErrorCode::INVALID_FORMAT,
        ErrorCode::INVALID_ARGUMENT,
        ErrorCode::INVALID_CONFIG,
        ErrorCode::INVALID_STATE,
        ErrorCode::PARSE_ERROR,
        ErrorCode::JSON_PARSE_ERROR,
        ErrorCode::TRACE_PARSE_ERROR,
        ErrorCode::UNSUPPORTED_FORMAT,
        ErrorCode::UNSUPPORTED_COMPILER,
        ErrorCode::MALFORMED_DATA,
        ErrorCode::GRAPH_ERROR,
        ErrorCode::CIRCULAR_DEPENDENCY,
        ErrorCode::MISSING_DEPENDENCY,
        ErrorCode::INVALID_GRAPH,
        ErrorCode::DATABASE_ERROR,
        ErrorCode::STORAGE_ERROR,
        ErrorCode::QUERY_ERROR,
        ErrorCode::OUT_OF_MEMORY,
        ErrorCode::RESOURCE_EXHAUSTED,
        ErrorCode::TIMEOUT,
        ErrorCode::ANALYSIS_ERROR,
        ErrorCode::CALCULATION_ERROR,
        ErrorCode::VALIDATION_ERROR,
        ErrorCode::SECURITY_ERROR,
        ErrorCode::PERMISSION_DENIED,
        ErrorCode::NETWORK_ERROR,
        ErrorCode::CONNECTION_ERROR,
        ErrorCode::INTERNAL_ERROR,
        ErrorCode::NOT_IMPLEMENTED,
        ErrorCode::NOT_FOUND,
        ErrorCode::UNKNOWN_ERROR
    };

    for (const auto code : all_codes) {
        const char* str = error_code_to_string(code);
        EXPECT_NE(str, nullptr);
        EXPECT_GT(std::strlen(str), 0);

        const ErrorSeverity severity = error_code_to_severity(code);
        EXPECT_TRUE(severity == ErrorSeverity::WARNING ||
                   severity == ErrorSeverity::ERROR ||
                   severity == ErrorSeverity::FATAL);
    }
}

TEST(ErrorTest, SourceLocationCapture) {
    const Error err(ErrorCode::INTERNAL_ERROR, "Test error");
    EXPECT_FALSE(err.file.empty());
    EXPECT_TRUE(err.file.find("test_error.cpp") != std::string::npos ||
                err.file.find(__FILE__) != std::string::npos);
    EXPECT_GT(err.line, 0u);
    EXPECT_FALSE(err.function.empty());
}

TEST(ErrorTest, ContextField) {
    Error err(ErrorCode::DATABASE_ERROR, "Connection failed");
    err.context = "While connecting to SQLite database at /path/to/db.sqlite";
    const auto str = err.to_string();
    EXPECT_TRUE(str.find("While connecting") != std::string::npos);
}

TEST(ErrorTest, MultipleSuggestions) {
    const std::vector<std::string> suggestions = {
        "Suggestion 1",
        "Suggestion 2",
        "Suggestion 3",
        "Suggestion 4"
    };
    const Error err(ErrorCode::VALIDATION_ERROR, "Multiple issues found", suggestions);
    ASSERT_EQ(err.suggestions.size(), 4);
    for (size_t i = 0; i < suggestions.size(); ++i) {
        EXPECT_EQ(err.suggestions[i], suggestions[i]);
    }
}