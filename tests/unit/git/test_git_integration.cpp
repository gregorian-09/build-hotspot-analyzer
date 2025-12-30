//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/git/git_integration.hpp"

#include <gtest/gtest.h>
#include <filesystem>

namespace bha::git
{
    // =============================================================================
    // Hook Type Conversion Tests
    // =============================================================================

    TEST(HookTypeTest, HookTypeToString) {
        EXPECT_EQ(hook_type_to_string(HookType::PreCommit), "pre-commit");
        EXPECT_EQ(hook_type_to_string(HookType::PrePush), "pre-push");
        EXPECT_EQ(hook_type_to_string(HookType::PostMerge), "post-merge");
        EXPECT_EQ(hook_type_to_string(HookType::PostCheckout), "post-checkout");
        EXPECT_EQ(hook_type_to_string(HookType::PrepareCommitMsg), "prepare-commit-msg");
    }

    TEST(HookTypeTest, StringToHookType) {
        EXPECT_EQ(string_to_hook_type("pre-commit"), HookType::PreCommit);
        EXPECT_EQ(string_to_hook_type("pre-push"), HookType::PrePush);
        EXPECT_EQ(string_to_hook_type("post-merge"), HookType::PostMerge);
        EXPECT_EQ(string_to_hook_type("post-checkout"), HookType::PostCheckout);
        EXPECT_EQ(string_to_hook_type("prepare-commit-msg"), HookType::PrepareCommitMsg);
    }

    TEST(HookTypeTest, StringToHookTypeInvalid) {
        EXPECT_FALSE(string_to_hook_type("invalid-hook").has_value());
        EXPECT_FALSE(string_to_hook_type("").has_value());
        EXPECT_FALSE(string_to_hook_type("precommit").has_value());
    }

    // =============================================================================
    // Execute Git Tests
    // =============================================================================

    class GitExecutionTest : public ::testing::Test {
    protected:
        void SetUp() override {
            temp_dir_ = fs::current_path();
            while (!temp_dir_.empty() && !fs::exists(temp_dir_ / ".git")) {
                temp_dir_ = temp_dir_.parent_path();
            }
        }

        fs::path temp_dir_;
    };

    TEST_F(GitExecutionTest, ExecuteGitVersion) {
        auto result = execute_git({"--version"}, temp_dir_);
        EXPECT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().exit_code, 0);
        EXPECT_FALSE(result.value().stdout_output.empty());
    }

    TEST_F(GitExecutionTest, ExecuteGitWithInvalidCommand) {
        auto result = execute_git({"invalid-command-12345"}, temp_dir_);
        EXPECT_TRUE(result.is_ok());  // Command executed, but returns error
        EXPECT_NE(result.value().exit_code, 0);
    }

    TEST_F(GitExecutionTest, ExecuteGitWithNonexistentDir) {
        auto result = execute_git({"--version"}, "/nonexistent/path/12345");
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
    }

    TEST_F(GitExecutionTest, IsGitRepository) {
        if (fs::exists(temp_dir_ / ".git")) {
            EXPECT_TRUE(is_git_repository(temp_dir_));
        }
        EXPECT_FALSE(is_git_repository("/tmp"));
    }

    TEST_F(GitExecutionTest, GetRepositoryRoot) {
        if (fs::exists(temp_dir_ / ".git")) {
            auto result = get_repository_root(temp_dir_);
            EXPECT_TRUE(result.is_ok());
            EXPECT_EQ(fs::canonical(result.value()), fs::canonical(temp_dir_));
        }
    }

    TEST_F(GitExecutionTest, GetCurrentBranch) {
        if (fs::exists(temp_dir_ / ".git")) {
            auto result = get_current_branch(temp_dir_);
            EXPECT_TRUE(result.is_ok());
            EXPECT_FALSE(result.value().empty());
        }
    }

    TEST_F(GitExecutionTest, GetHead) {
        if (fs::exists(temp_dir_ / ".git")) {
            auto result = get_head(temp_dir_);
            EXPECT_TRUE(result.is_ok());
            EXPECT_EQ(result.value().size(), 40u);  // Full SHA is 40 chars
        }
    }

    TEST_F(GitExecutionTest, HasUncommittedChanges) {
        if (fs::exists(temp_dir_ / ".git")) {
            const auto result = has_uncommitted_changes(temp_dir_);
            EXPECT_TRUE(result.is_ok());
        }
    }

    TEST_F(GitExecutionTest, GetCommit) {
        if (fs::exists(temp_dir_ / ".git") && is_git_repository(temp_dir_)) {
            if (auto head_result = get_head(temp_dir_); head_result.is_ok()) {
                if (auto result = get_commit("HEAD", temp_dir_); result.is_ok()) {
                    EXPECT_FALSE(result.value().hash.empty());
                    EXPECT_FALSE(result.value().short_hash.empty());
                }
            }
        }
    }

    TEST_F(GitExecutionTest, GetCommits) {
        if (fs::exists(temp_dir_ / ".git") && is_git_repository(temp_dir_)) {
            if (const auto head_result = get_head(temp_dir_); head_result.is_ok()) {
                if (auto result = get_commits("HEAD", 5, temp_dir_); result.is_ok()) {
                    EXPECT_LE(result.value().size(), 5u);
                }
            }
        }
    }

    TEST_F(GitExecutionTest, ParseCommit) {
        std::string raw = "abc123def456789012345678901234567890abcd|abc123d|John Doe|john@example.com|2024-01-15T10:30:00|Jane Doe|jane@example.com|2024-01-15T10:35:00|Fix bug in parser";

        auto result = parse_commit(raw);
        EXPECT_TRUE(result.is_ok());

        const auto& info = result.value();
        EXPECT_EQ(info.hash, "abc123def456789012345678901234567890abcd");
        EXPECT_EQ(info.short_hash, "abc123d");
        EXPECT_EQ(info.author_name, "John Doe");
        EXPECT_EQ(info.author_email, "john@example.com");
        EXPECT_EQ(info.committer_name, "Jane Doe");
        EXPECT_EQ(info.committer_email, "jane@example.com");
        EXPECT_EQ(info.subject, "Fix bug in parser");
    }
}