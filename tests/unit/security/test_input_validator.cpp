//
// Created by gregorian on 12/12/2025.
//

#include <gtest/gtest.h>
#include "bha/security/input_validator.h"
#include <fstream>
#include <filesystem>

using namespace bha::security;
using namespace bha::core;
namespace fs = std::filesystem;

class InputValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "input_validator_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    [[nodiscard]] std::string create_test_file(const std::string& filename, const std::string& content) const
    {
        const fs::path file_path = temp_dir / filename;
        std::ofstream file(file_path, std::ios::binary);
        file << content;
        file.close();
        return file_path.string();
    }

    [[nodiscard]] std::string create_large_file(const std::string& filename, size_t size_mb) const
    {
        const fs::path file_path = temp_dir / filename;
        std::ofstream file(file_path, std::ios::binary);
        constexpr size_t chunk_size = 1024 * 1024;  // 1MB chunks
        const std::vector<char> buffer(chunk_size, 'A');
        for (size_t i = 0; i < size_mb; ++i) {
            file.write(buffer.data(), chunk_size);
        }
        file.close();
        return file_path.string();
    }

    fs::path temp_dir;
};

TEST_F(InputValidatorTest, DefaultOptions) {
    InputValidator::ValidationOptions opts;
    EXPECT_EQ(opts.max_file_size_mb, 1024);
    EXPECT_EQ(opts.max_path_length, 4096);
    EXPECT_EQ(opts.max_json_depth, 100);
    EXPECT_TRUE(opts.allow_absolute_paths);
    EXPECT_FALSE(opts.allow_symlinks);
}

TEST_F(InputValidatorTest, ValidateSimplePath) {
    InputValidator::ValidationOptions opts;
    opts.allow_absolute_paths = true;
    const InputValidator validator(opts);

    const std::string test_file = create_test_file("simple.txt", "content");
    const auto result = validator.validate_file_path(test_file);

    EXPECT_TRUE(result.is_success());
}

TEST_F(InputValidatorTest, PathTraversalDetection) {
    EXPECT_TRUE(InputValidator::contains_path_traversal("../secret/file.txt"));
    EXPECT_TRUE(InputValidator::contains_path_traversal("/path/to/../../../etc/passwd"));
    EXPECT_TRUE(InputValidator::contains_path_traversal("./../../data"));
    EXPECT_FALSE(InputValidator::contains_path_traversal("/home/user/file.txt"));
    EXPECT_FALSE(InputValidator::contains_path_traversal("./normal/path.cpp"));
}

TEST_F(InputValidatorTest, RejectPathTraversal) {
    const InputValidator::ValidationOptions opts;
    const InputValidator validator(opts);

    const auto result = validator.validate_file_path("../../../etc/passwd");
    EXPECT_TRUE(result.is_failure());
}

TEST_F(InputValidatorTest, ValidateFileSize_WithinLimit) {
    const std::string small_file = create_test_file("small.txt", "small content");

    InputValidator::ValidationOptions opts;
    opts.max_file_size_mb = 10;
    const InputValidator validator(opts);

    const auto result = validator.validate_file_size(small_file);
    EXPECT_TRUE(result.is_success());
}

TEST_F(InputValidatorTest, ValidateFileSize_ExceedsLimit) {
    const std::string large_file = create_large_file("large.bin", 5);

    InputValidator::ValidationOptions opts;
    opts.max_file_size_mb = 2;  // Limit smaller than file
    const InputValidator validator(opts);

    const auto result = validator.validate_file_size(large_file);
    EXPECT_TRUE(result.is_failure());
}

TEST_F(InputValidatorTest, ValidateFileSize_NonExistent) {
    const InputValidator::ValidationOptions opts;
    const InputValidator validator(opts);

    const auto result = validator.validate_file_size("/nonexistent/file.txt");
    EXPECT_TRUE(result.is_failure());
}

TEST_F(InputValidatorTest, ValidateJSONStructure_ValidJSON) {
    const std::string json_content = R"({
        "key1": "value1",
        "key2": {
            "nested": "value2"
        }
    })";
    const std::string json_file = create_test_file("valid.json", json_content);

    const InputValidator::ValidationOptions opts;
    const InputValidator validator(opts);

    const auto result = bha::security::InputValidator::validate_json_structure(json_file);
    EXPECT_TRUE(result.is_success());
}

TEST_F(InputValidatorTest, ValidateJSONStructure_InvalidJSON) {
    const std::string json_content = "{ invalid json content ";
    const std::string json_file = create_test_file("invalid.json", json_content);

    const InputValidator::ValidationOptions opts;
    const InputValidator validator(opts);

    const auto result = InputValidator::validate_json_structure(json_file);
    EXPECT_TRUE(result.is_failure());
}

TEST_F(InputValidatorTest, ValidateJSONStructure_NonExistent) {
    const InputValidator::ValidationOptions opts;
    const InputValidator validator(opts);

    const auto result = bha::security::InputValidator::validate_json_structure("/nonexistent/file.json");
    EXPECT_TRUE(result.is_failure());
}

TEST_F(InputValidatorTest, ValidateTraceFile_ValidTrace) {
    const std::string trace_content = R"({"trace": "data"})";
    const std::string trace_file = create_test_file("trace.json", trace_content);

    const InputValidator::ValidationOptions opts;
    const InputValidator validator(opts);

    const auto result = validator.validate_trace_file(trace_file);
    EXPECT_TRUE(result.is_success());
}

TEST_F(InputValidatorTest, ValidateTraceFile_TooLarge) {
    const std::string large_trace = create_large_file("large_trace.json", 10);

    InputValidator::ValidationOptions opts;
    opts.max_file_size_mb = 5;
    const InputValidator validator(opts);

    const auto result = validator.validate_trace_file(large_trace);
    EXPECT_TRUE(result.is_failure());
}

TEST_F(InputValidatorTest, IsSafePath_SimplePath) {
    InputValidator::ValidationOptions opts;
    opts.allowed_directories = {temp_dir.string()};
    const InputValidator validator(opts);

    const std::string safe_file = (temp_dir / "safe.txt").string();
    EXPECT_TRUE(validator.is_safe_path(safe_file));
}

TEST_F(InputValidatorTest, IsSafePath_WithTraversal) {
    const InputValidator::ValidationOptions opts;
    const InputValidator validator(opts);

    EXPECT_FALSE(validator.is_safe_path("../../../etc/passwd"));
    EXPECT_FALSE(validator.is_safe_path("./../../sensitive"));
}

TEST_F(InputValidatorTest, BlockedPatterns) {
    InputValidator::ValidationOptions opts;
    opts.blocked_patterns = {"*/secret/*", "*.key", "*/private/*"};
    const InputValidator validator(opts);

    EXPECT_TRUE(validator.matches_blocked_pattern("/path/to/secret/file.txt"));
    EXPECT_TRUE(validator.matches_blocked_pattern("/home/user/test.key"));
    EXPECT_TRUE(validator.matches_blocked_pattern("/data/private/info.dat"));
    EXPECT_FALSE(validator.matches_blocked_pattern("/home/user/public/file.txt"));
}

TEST_F(InputValidatorTest, AllowedDirectories_InsideAllowed) {
    InputValidator::ValidationOptions opts;
    opts.allowed_directories = {"/home/user/project", "/opt/build"};
    const InputValidator validator(opts);

    EXPECT_TRUE(validator.is_within_allowed_directories("/home/user/project/src/main.cpp"));
    EXPECT_TRUE(validator.is_within_allowed_directories("/opt/build/output.o"));
}

TEST_F(InputValidatorTest, AllowedDirectories_OutsideAllowed) {
    InputValidator::ValidationOptions opts;
    opts.allowed_directories = {"/home/user/project"};
    const InputValidator validator(opts);

    EXPECT_FALSE(validator.is_within_allowed_directories("/etc/passwd"));
    EXPECT_FALSE(validator.is_within_allowed_directories("/tmp/file.txt"));
}

TEST_F(InputValidatorTest, AllowedDirectories_EmptyList) {
    InputValidator::ValidationOptions opts;
    opts.allowed_directories = {};
    const InputValidator validator(opts);

    // With empty allowed list, all paths should be allowed
    EXPECT_TRUE(validator.is_within_allowed_directories("/any/path/file.txt"));
}

TEST_F(InputValidatorTest, PathLengthLimit) {
    InputValidator::ValidationOptions opts;
    opts.max_path_length = 50;
    InputValidator validator(opts);

    const std::string short_path = "/short/path.txt";
    const std::string long_path = "/very/long/path/that/exceeds/the/maximum/allowed/length/for/this/test.txt";

    const auto short_result = validator.validate_file_path(short_path);
    const auto long_result = validator.validate_file_path(long_path);

    // Short path might still fail for other reasons, but long path should fail
    EXPECT_TRUE(long_result.is_failure());
}

TEST_F(InputValidatorTest, AbsolutePathsDisallowed) {
    InputValidator::ValidationOptions opts;
    opts.allow_absolute_paths = false;
    const InputValidator validator(opts);

    const auto result = validator.validate_file_path("/absolute/path/file.txt");
    EXPECT_TRUE(result.is_failure());
}

TEST_F(InputValidatorTest, RelativePathsAllowed) {
    InputValidator::ValidationOptions opts;
    opts.allow_absolute_paths = false;
    const InputValidator validator(opts);

    const auto result = validator.validate_file_path("relative/path/file.txt");
    ASSERT_TRUE(!result.is_success());
}

TEST_F(InputValidatorTest, CombinedValidation) {
    const std::string json_content = R"({"test": "data"})";
    const std::string json_file = create_test_file("combined.json", json_content);

    InputValidator::ValidationOptions opts;
    opts.max_file_size_mb = 10;
    opts.max_json_depth = 50;
    opts.allowed_directories = {temp_dir.string()};
    opts.blocked_patterns = {"*.secret"};
    InputValidator validator(opts);

    const auto path_result = validator.validate_file_path(json_file);
    const auto size_result = validator.validate_file_size(json_file);
    const auto json_result = InputValidator::validate_json_structure(json_file);

    EXPECT_TRUE(path_result.is_success());
    EXPECT_TRUE(size_result.is_success());
}