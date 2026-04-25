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

        static std::optional<std::vector<fs::path>> collect_compile_command_validation_sources(
            const std::optional<fs::path>& compile_commands_path,
            const BuildTrace& analysis_trace,
            const std::optional<fs::path>& project_root,
            const bha::Suggestion& suggestion,
            const std::vector<fs::path>& changed_files,
            const std::string& validation_label,
            std::vector<Diagnostic>& errors
        ) {
            return SuggestionManager::collect_compile_command_validation_sources(
                compile_commands_path,
                analysis_trace,
                project_root,
                suggestion,
                changed_files,
                validation_label,
                errors
            );
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

    TEST_F(SuggestionManagerRollbackTest, PCHValidationUsesCompileBackedIncludersOfTargetHeader) {
        const fs::path include_dir = temp_root_ / "include";
        const fs::path src_dir = temp_root_ / "src";
        const fs::path header_path = include_dir / "hot.hpp";
        const fs::path source_path = src_dir / "main.cpp";
        const fs::path compile_commands_path = temp_root_ / "compile_commands.json";
        const fs::path generated_pch = temp_root_ / "pch.h";
        const fs::path cmake_lists = temp_root_ / "CMakeLists.txt";

        fs::create_directories(include_dir);
        fs::create_directories(src_dir);
        {
            std::ofstream out(header_path);
            ASSERT_TRUE(out.good());
            out << "#pragma once\n";
        }
        {
            std::ofstream out(source_path);
            ASSERT_TRUE(out.good());
            out << "#include \"../include/hot.hpp\"\n";
        }
        {
            std::ofstream out(compile_commands_path);
            ASSERT_TRUE(out.good());
            out << "[{\"directory\":\"" << temp_root_.string()
                << "\",\"file\":\"" << source_path.string()
                << "\",\"command\":\"clang++ -c " << source_path.string() << "\"}]";
        }

        BuildTrace trace;
        CompilationUnit unit;
        unit.source_file = source_path;
        unit.includes.push_back(IncludeInfo{
            .header = header_path,
            .parse_time = Duration::zero(),
            .depth = 0,
            .included_by = {},
            .symbols_used = {}
        });
        trace.units.push_back(std::move(unit));

        bha::Suggestion suggestion;
        suggestion.type = bha::SuggestionType::PCHOptimization;
        suggestion.target_file.path = header_path;
        suggestion.target_file.action = bha::FileAction::Modify;
        suggestion.secondary_files.push_back(
            bha::FileTarget{.path = generated_pch, .action = bha::FileAction::Create}
        );
        suggestion.secondary_files.push_back(
            bha::FileTarget{.path = cmake_lists, .action = bha::FileAction::Modify}
        );

        std::vector<Diagnostic> errors;
        const auto sources = SuggestionManagerTestAccess::collect_compile_command_validation_sources(
            compile_commands_path,
            trace,
            temp_root_,
            suggestion,
            {generated_pch, cmake_lists},
            "PCH",
            errors
        );

        ASSERT_TRUE(sources.has_value());
        ASSERT_TRUE(errors.empty());
        ASSERT_EQ(sources->size(), 1u);
        EXPECT_EQ(sources->front(), source_path.lexically_normal());
    }

    TEST_F(SuggestionManagerRollbackTest, ListsDiskBackupsAcrossSessionsInNewestFirstOrder) {
        const fs::path backup_root = temp_root_ / ".lsp-optimization-backup";
        const fs::path newer_backup = backup_root / "20260424-220000-1";
        const fs::path older_backup = backup_root / "20260423-220000-1";

        fs::create_directories(newer_backup);
        fs::create_directories(older_backup);

        {
            std::ofstream out(newer_backup / "metadata.txt");
            ASSERT_TRUE(out.good());
            out << "id=20260424-220000-1\n";
            out << "timestamp=1714082400\n";
            out << "file_count=2\n";
            out << "file=" << (temp_root_ / "a.cpp").string() << "\n";
            out << "existed_before=1\n";
            out << "file=" << (temp_root_ / "b.cpp").string() << "\n";
            out << "existed_before=0\n";
        }
        {
            std::ofstream out(older_backup / "metadata.txt");
            ASSERT_TRUE(out.good());
            out << "id=20260423-220000-1\n";
            out << "timestamp=1713996000\n";
            out << "file_count=1\n";
            out << "file=" << (temp_root_ / "c.cpp").string() << "\n";
            out << "existed_before=1\n";
        }

        SuggestionManagerConfig config;
        config.use_disk_backups = true;
        config.workspace_root = temp_root_;
        SuggestionManager manager(config);

        const auto backups = manager.list_backups();
        ASSERT_EQ(backups.size(), 2u);
        EXPECT_EQ(backups[0].id, "20260424-220000-1");
        EXPECT_EQ(backups[0].file_count, 2u);
        EXPECT_TRUE(backups[0].on_disk);
        EXPECT_EQ(backups[1].id, "20260423-220000-1");
        EXPECT_EQ(backups[1].file_count, 1u);
        EXPECT_TRUE(backups[1].on_disk);
    }
}
