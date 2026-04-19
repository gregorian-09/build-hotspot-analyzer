//
// Created by gregorian-rayne on 04/19/26.
//

#include "bha/lsp/suggestion_manager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::lsp
{
    namespace fs = std::filesystem;

    class SuggestionManagerTestAccess {
    public:
        static bool capture_transactional_snapshot(
            const std::vector<fs::path>& files,
            std::vector<FileBackup>& snapshot,
            std::vector<Diagnostic>& errors
        ) {
            return SuggestionManager::capture_transactional_snapshot(files, snapshot, errors);
        }

        static bool restore_transactional_snapshot(
            const std::vector<FileBackup>& snapshot,
            std::vector<Diagnostic>& errors
        ) {
            return SuggestionManager::restore_transactional_snapshot(snapshot, errors);
        }
    };

    class SuggestionManagerRollbackTest : public ::testing::Test {
    protected:
        void SetUp() override {
            const auto unique_suffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            );
            temp_root_ = fs::temp_directory_path() / ("bha-lsp-rollback-test-" + unique_suffix);
            std::error_code ec;
            fs::remove_all(temp_root_, ec);
            fs::create_directories(temp_root_, ec);
        }

        void TearDown() override {
            std::error_code ec;
            fs::remove_all(temp_root_, ec);
        }

        fs::path temp_root_;
    };

    TEST_F(SuggestionManagerRollbackTest, SnapshotCapturesMissingCreatedFileAndRestoresByRemovingIt) {
        const fs::path created_file = temp_root_ / "pch.h";
        ASSERT_FALSE(fs::exists(created_file));

        std::vector<FileBackup> snapshot;
        std::vector<Diagnostic> errors;
        ASSERT_TRUE(SuggestionManagerTestAccess::capture_transactional_snapshot(
            {created_file},
            snapshot,
            errors
        ));
        ASSERT_TRUE(errors.empty());
        ASSERT_EQ(snapshot.size(), 1u);
        EXPECT_FALSE(snapshot.front().existed_before);
        EXPECT_EQ(snapshot.front().path, created_file.lexically_normal());

        {
            std::ofstream out(created_file);
            ASSERT_TRUE(out.good());
            out << "#pragma once\n";
        }
        ASSERT_TRUE(fs::exists(created_file));

        ASSERT_TRUE(SuggestionManagerTestAccess::restore_transactional_snapshot(snapshot, errors));
        EXPECT_TRUE(errors.empty());
        EXPECT_FALSE(fs::exists(created_file));
    }

    TEST_F(SuggestionManagerRollbackTest, SnapshotRestoresExistingFileContents) {
        const fs::path existing_file = temp_root_ / "existing.hpp";
        {
            std::ofstream out(existing_file);
            ASSERT_TRUE(out.good());
            out << "before\n";
        }

        std::vector<FileBackup> snapshot;
        std::vector<Diagnostic> errors;
        ASSERT_TRUE(SuggestionManagerTestAccess::capture_transactional_snapshot(
            {existing_file},
            snapshot,
            errors
        ));
        ASSERT_TRUE(errors.empty());
        ASSERT_EQ(snapshot.size(), 1u);
        EXPECT_TRUE(snapshot.front().existed_before);
        EXPECT_EQ(snapshot.front().content, "before\n");

        {
            std::ofstream out(existing_file, std::ios::trunc);
            ASSERT_TRUE(out.good());
            out << "after\n";
        }

        ASSERT_TRUE(SuggestionManagerTestAccess::restore_transactional_snapshot(snapshot, errors));
        EXPECT_TRUE(errors.empty());

        std::ifstream in(existing_file);
        ASSERT_TRUE(in.good());
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content, "before\n");
    }
}
