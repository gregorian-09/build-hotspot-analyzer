//
// Created by gregorian-rayne on 04/19/26.
//

#include "bha/lsp/suggestion_manager.hpp"
#include "bha/lsp/diagnostic_parser.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::lsp
{
    namespace fs = std::filesystem;

    std::string stable_suggestion_key(const bha::Suggestion& suggestion);

    TEST(CompilerDiagnosticParserTest, ParsesClangAndGccLocationsWithColonsInPath) {
        const auto diagnostics = parse_compiler_diagnostics(
            "C:/work:tree/source.cpp:12:7: error: expected ';'\n"
            "/tmp/source.cpp:4:2: warning: unused variable\n"
            "not a compiler diagnostic\n"
        );

        ASSERT_EQ(diagnostics.size(), 2u);
        EXPECT_EQ(diagnostics[0].range.start.line, 11);
        EXPECT_EQ(diagnostics[0].range.start.character, 6);
        EXPECT_EQ(diagnostics[0].severity, DiagnosticSeverity::Error);
        EXPECT_EQ(diagnostics[0].message, "expected ';'");
        EXPECT_EQ(diagnostics[1].range.start.line, 3);
        EXPECT_EQ(diagnostics[1].range.start.character, 1);
        EXPECT_EQ(diagnostics[1].severity, DiagnosticSeverity::Warning);
    }

    TEST(CompilerDiagnosticParserTest, ParsesMsvcLocationsAndRejectsMalformedCoordinates) {
        const auto diagnostics = parse_compiler_diagnostics(
            "C:\\work\\source.cpp(21,9): error C2143: missing token\r\n"
            "C:\\work\\source.cpp(22): warning C4100: unused parameter\n"
            "C:\\work\\source.cpp(x,3): error C0000: malformed\n"
        );

        ASSERT_EQ(diagnostics.size(), 2u);
        EXPECT_EQ(diagnostics[0].range.start.line, 20);
        EXPECT_EQ(diagnostics[0].range.start.character, 8);
        EXPECT_EQ(diagnostics[0].severity, DiagnosticSeverity::Error);
        EXPECT_EQ(diagnostics[0].message, "C2143: missing token");
        EXPECT_EQ(diagnostics[1].range.start.line, 21);
        EXPECT_EQ(diagnostics[1].range.start.character, 0);
        EXPECT_EQ(diagnostics[1].severity, DiagnosticSeverity::Warning);
    }

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

        static void enforce_pch_auto_apply_validation_readiness(
            std::vector<bha::Suggestion>& suggestions,
            const std::optional<fs::path>& compile_commands_path,
            const BuildTrace& analysis_trace,
            const std::optional<fs::path>& project_root,
            const bool enforce_compile_command_syntax_gate
        ) {
            SuggestionManager::enforce_pch_auto_apply_validation_readiness(
                suggestions,
                compile_commands_path,
                analysis_trace,
                project_root,
                enforce_compile_command_syntax_gate
            );
        }

        static std::vector<fs::path> collect_generated_forward_headers(
            const bha::Suggestion& suggestion,
            const std::vector<fs::path>& changed_files
        ) {
            return SuggestionManager::collect_generated_forward_headers(
                suggestion,
                changed_files
            );
        }

        static std::optional<SuggestionManager::FileState> read_file_state(const fs::path& file) {
            return SuggestionManager::read_file_state(file);
        }

        static bool validate_suggestion_source_state(
            SuggestionManager& manager,
            const bha::Suggestion& suggestion,
            std::vector<Diagnostic>& errors
        ) {
            return manager.validate_suggestion_source_state(suggestion, errors);
        }

        static bool validate_post_apply_rebuild(
            SuggestionManager& manager,
            ApplySuggestionResult& result
        ) {
            return manager.validate_post_apply_rebuild(result);
        }

        static void seed_build_trace(
            SuggestionManager& manager,
            const fs::path& project_root,
            const fs::path& source
        ) {
            manager.last_project_root_ = project_root;
            manager.last_analysis_id_ = "analysis-1";
            manager.analysis_cache_.clear();
            BuildTrace trace;
            CompilationUnit unit;
            unit.source_file = source;
            trace.units.push_back(std::move(unit));
            manager.analysis_cache_.emplace(manager.last_analysis_id_, std::move(trace));
        }

        static bool apply_file_changes(
            const bha::Suggestion& suggestion,
            std::vector<fs::path>& changed_files
        ) {
            return SuggestionManager::apply_file_changes(suggestion, changed_files);
        }

        static void seed_source_state(
            SuggestionManager& manager,
            const fs::path& project_root,
            const fs::path& file
        ) {
            manager.last_project_root_ = project_root;
            const auto state = SuggestionManager::read_file_state(file);
            ASSERT_TRUE(state.has_value());
            manager.last_file_states_.clear();
            manager.last_file_states_.emplace(file.lexically_normal().generic_string(), *state);
        }

        static void add_source_state(
            SuggestionManager& manager,
            const fs::path& project_root,
            const fs::path& file
        ) {
            manager.last_project_root_ = project_root;
            const auto state = SuggestionManager::read_file_state(file);
            ASSERT_TRUE(state.has_value());
            manager.last_file_states_.emplace(file.lexically_normal().generic_string(), *state);
        }

        static void set_bha_suggestion(
            SuggestionManager& manager,
            const std::string& id,
            bha::Suggestion suggestion
        ) {
            suggestion.id = id;
            manager.bha_suggestions_[id] = std::move(suggestion);
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

    TEST(SuggestionManagerIdentityTest, CanonicalizesEditOrderAndPreservesExactText) {
        bha::Suggestion first;
        first.type = bha::SuggestionType::IncludeRemoval;
        first.target_file.path = fs::path("include/main.hpp");
        first.edits.push_back(bha::TextEdit{
            .file = fs::path("src/main.cpp"),
            .start_line = 4,
            .start_col = 2,
            .end_line = 4,
            .end_col = 8,
            .new_text = "#include <vector>\n"
        });
        first.edits.push_back(bha::TextEdit{
            .file = fs::path("include/main.hpp"),
            .start_line = 1,
            .start_col = 0,
            .end_line = 1,
            .end_col = 0,
            .new_text = "#pragma once\n"
        });

        bha::Suggestion reordered = first;
        std::ranges::reverse(reordered.edits);
        EXPECT_EQ(stable_suggestion_key(first), stable_suggestion_key(reordered));

        bha::Suggestion different_text = first;
        different_text.edits.front().new_text = "#include <string>\n";
        EXPECT_NE(stable_suggestion_key(first), stable_suggestion_key(different_text));
    }

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
            std::ofstream out(existing_file, std::ios::binary);
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
            std::ofstream out(existing_file, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(out.good());
            out << "after\n";
        }

        ASSERT_TRUE(SuggestionManagerTestAccess::restore_transactional_snapshot(snapshot, errors));
        EXPECT_TRUE(errors.empty());

        std::ifstream in(existing_file, std::ios::binary);
        ASSERT_TRUE(in.good());
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content, "before\n");
    }

    TEST_F(SuggestionManagerRollbackTest, AppliesCompilerByteRangeWithoutUsingStaleLspPosition) {
        const fs::path source = temp_root_ / "source.cpp";
        {
            std::ofstream out(source);
            ASSERT_TRUE(out.good());
            out << "alpha\nbeta\n";
        }

        SuggestionManagerConfig config;
        config.workspace_root = temp_root_;
        SuggestionManager manager(config);
        bha::TextEdit edit;
        edit.file = source;
        edit.start_line = 0;
        edit.start_col = 0;
        edit.end_line = 0;
        edit.end_col = 0;
        edit.new_text = "BETA";
        edit.byte_offset = 6;
        edit.byte_length = 4;

        const auto result = manager.apply_edit_bundle({edit}, false);
        ASSERT_TRUE(result.success);

        std::ifstream in(source);
        const std::string content(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        EXPECT_EQ(content, "alpha\nBETA\n");
    }

    TEST_F(SuggestionManagerRollbackTest, RejectsMixedByteAndLineRangesInOneFile) {
        const fs::path source = temp_root_ / "source.cpp";
        {
            std::ofstream out(source);
            ASSERT_TRUE(out.good());
            out << "alpha\nbeta\n";
        }

        SuggestionManagerConfig config;
        config.workspace_root = temp_root_;
        SuggestionManager manager(config);
        bha::TextEdit byte_edit;
        byte_edit.file = source;
        byte_edit.new_text = "A";
        byte_edit.byte_offset = 0;
        byte_edit.byte_length = 1;

        bha::TextEdit line_edit;
        line_edit.file = source;
        line_edit.start_line = 1;
        line_edit.end_line = 1;
        line_edit.start_col = 0;
        line_edit.end_col = 4;
        line_edit.new_text = "BETA";

        const auto result = manager.apply_edit_bundle({byte_edit, line_edit}, false);
        EXPECT_FALSE(result.success);

        std::ifstream in(source);
        const std::string content(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        EXPECT_EQ(content, "alpha\nbeta\n");
    }

    TEST_F(SuggestionManagerRollbackTest, AppliesDifferentFilesInCanonicalPathOrder) {
        const fs::path z_file = temp_root_ / "z.hpp";
        const fs::path a_file = temp_root_ / "a.hpp";
        {
            std::ofstream out(z_file);
            ASSERT_TRUE(out.good());
            out << "z\n";
        }
        {
            std::ofstream out(a_file);
            ASSERT_TRUE(out.good());
            out << "a\n";
        }

        bha::Suggestion suggestion;
        suggestion.edits.push_back(bha::TextEdit{
            .file = z_file,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "z-header\n"
        });
        suggestion.edits.push_back(bha::TextEdit{
            .file = a_file,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "a-header\n"
        });

        std::vector<fs::path> changed_files;
        ASSERT_TRUE(SuggestionManagerTestAccess::apply_file_changes(suggestion, changed_files));
        ASSERT_EQ(changed_files.size(), 2u);
        EXPECT_EQ(changed_files[0], a_file.lexically_normal());
        EXPECT_EQ(changed_files[1], z_file.lexically_normal());

        std::ifstream a_in(a_file);
        ASSERT_TRUE(a_in.good());
        EXPECT_EQ(
            std::string((std::istreambuf_iterator<char>(a_in)), std::istreambuf_iterator<char>()),
            "a-header\na\n"
        );
        std::ifstream z_in(z_file);
        ASSERT_TRUE(z_in.good());
        EXPECT_EQ(
            std::string((std::istreambuf_iterator<char>(z_in)), std::istreambuf_iterator<char>()),
            "z-header\nz\n"
        );
    }

    TEST_F(SuggestionManagerRollbackTest, ExplicitSuggestionBatchRollsBackEarlierEdits) {
        const fs::path valid_file = temp_root_ / "valid.hpp";
        const fs::path invalid_file = temp_root_ / "invalid.hpp";
        {
            std::ofstream out(valid_file);
            ASSERT_TRUE(out.good());
            out << "valid\n";
        }
        {
            std::ofstream out(invalid_file);
            ASSERT_TRUE(out.good());
            out << "invalid\n";
        }

        SuggestionManagerConfig config;
        config.workspace_root = temp_root_;
        config.use_disk_backups = false;
        SuggestionManager manager(config);
        SuggestionManagerTestAccess::seed_source_state(manager, temp_root_, valid_file);
        SuggestionManagerTestAccess::add_source_state(manager, temp_root_, invalid_file);

        bha::Suggestion valid;
        valid.type = bha::SuggestionType::UnityBuild;
        valid.priority = bha::Priority::High;
        valid.is_safe = true;
        valid.edits.push_back(bha::TextEdit{
            .file = valid_file,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "valid-header\n"
        });
        SuggestionManagerTestAccess::set_bha_suggestion(manager, "ana-1", std::move(valid));

        bha::Suggestion invalid;
        invalid.type = bha::SuggestionType::UnityBuild;
        invalid.priority = bha::Priority::High;
        invalid.is_safe = true;
        bha::TextEdit invalid_byte_edit;
        invalid_byte_edit.file = invalid_file;
        invalid_byte_edit.byte_offset = 0;
        invalid_byte_edit.byte_length = 1;
        invalid_byte_edit.new_text = "I";
        invalid.edits.push_back(std::move(invalid_byte_edit));
        invalid.edits.push_back(bha::TextEdit{
            .file = invalid_file,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 1,
            .new_text = "I"
        });
        SuggestionManagerTestAccess::set_bha_suggestion(manager, "ana-2", std::move(invalid));

        const auto result = manager.apply_all_suggestions({"ana-1", "ana-2"});
        EXPECT_FALSE(result.success);
        EXPECT_TRUE(result.changed_files.empty());
        EXPECT_FALSE(result.backup_id.has_value());

        std::ifstream valid_in(valid_file);
        ASSERT_TRUE(valid_in.good());
        EXPECT_EQ(
            std::string((std::istreambuf_iterator<char>(valid_in)), std::istreambuf_iterator<char>()),
            "valid\n"
        );
        std::ifstream invalid_in(invalid_file);
        ASSERT_TRUE(invalid_in.good());
        EXPECT_EQ(
            std::string((std::istreambuf_iterator<char>(invalid_in)), std::istreambuf_iterator<char>()),
            "invalid\n"
        );
    }

    TEST_F(SuggestionManagerRollbackTest, FilteredApplyAllRollsBackEarlierEdits) {
        const fs::path valid_file = temp_root_ / "valid.hpp";
        const fs::path invalid_file = temp_root_ / "invalid.hpp";
        {
            std::ofstream out(valid_file);
            ASSERT_TRUE(out.good());
            out << "valid\n";
        }
        {
            std::ofstream out(invalid_file);
            ASSERT_TRUE(out.good());
            out << "invalid\n";
        }

        SuggestionManagerConfig config;
        config.workspace_root = temp_root_;
        config.use_disk_backups = false;
        config.rerank_remaining_after_each_apply = false;
        SuggestionManager manager(config);
        SuggestionManagerTestAccess::seed_source_state(manager, temp_root_, valid_file);
        SuggestionManagerTestAccess::add_source_state(manager, temp_root_, invalid_file);

        bha::Suggestion valid;
        valid.type = bha::SuggestionType::UnityBuild;
        valid.priority = bha::Priority::High;
        valid.is_safe = true;
        valid.edits.push_back(bha::TextEdit{
            .file = valid_file,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "valid-header\n"
        });
        SuggestionManagerTestAccess::set_bha_suggestion(manager, "ana-1", std::move(valid));

        bha::Suggestion invalid;
        invalid.type = bha::SuggestionType::UnityBuild;
        invalid.priority = bha::Priority::High;
        invalid.is_safe = true;
        bha::TextEdit invalid_byte_edit;
        invalid_byte_edit.file = invalid_file;
        invalid_byte_edit.byte_offset = 0;
        invalid_byte_edit.byte_length = 1;
        invalid_byte_edit.new_text = "I";
        invalid.edits.push_back(std::move(invalid_byte_edit));
        invalid.edits.push_back(bha::TextEdit{
            .file = invalid_file,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 1,
            .new_text = "I"
        });
        SuggestionManagerTestAccess::set_bha_suggestion(manager, "ana-2", std::move(invalid));

        const auto result = manager.apply_all_suggestions(std::nullopt, true);
        EXPECT_FALSE(result.success);
        EXPECT_EQ(result.applied_count, 0u);
        EXPECT_TRUE(result.applied_suggestion_ids.empty());
        EXPECT_TRUE(result.changed_files.empty());
        EXPECT_TRUE(result.backup_id.empty());

        std::ifstream valid_in(valid_file);
        ASSERT_TRUE(valid_in.good());
        EXPECT_EQ(
            std::string((std::istreambuf_iterator<char>(valid_in)), std::istreambuf_iterator<char>()),
            "valid\n"
        );
        std::ifstream invalid_in(invalid_file);
        ASSERT_TRUE(invalid_in.good());
        EXPECT_EQ(
            std::string((std::istreambuf_iterator<char>(invalid_in)), std::istreambuf_iterator<char>()),
            "invalid\n"
        );
    }

    TEST_F(SuggestionManagerRollbackTest, UsesConfiguredBuildValidationExecutor) {
        const fs::path source = temp_root_ / "source.cpp";
        {
            std::ofstream out(source);
            ASSERT_TRUE(out.good());
            out << "int value = 1;\n";
        }

        bool callback_called = false;
        SuggestionManagerConfig config;
        config.workspace_root = temp_root_;
        config.build_validation_callback = [&](const fs::path& project_root) {
            callback_called = true;
            EXPECT_EQ(project_root, temp_root_);
            BuildValidationResult result;
            result.success = true;
            result.duration_ms = 37;
            return result;
        };
        SuggestionManager manager(config);
        SuggestionManagerTestAccess::seed_build_trace(manager, temp_root_, source);

        ApplySuggestionResult result;
        EXPECT_TRUE(SuggestionManagerTestAccess::validate_post_apply_rebuild(manager, result));
        EXPECT_TRUE(callback_called);
        ASSERT_TRUE(result.build_result.has_value());
        EXPECT_TRUE(result.build_result->success);
        ASSERT_TRUE(result.build_validation_duration_ms.has_value());
        EXPECT_EQ(*result.build_validation_duration_ms, 37);
        EXPECT_TRUE(result.errors.empty());
    }

    TEST_F(SuggestionManagerRollbackTest, FilteredApplyAllBuildFailureRollsBackBatch) {
        const fs::path source = temp_root_ / "source.cpp";
        {
            std::ofstream out(source);
            ASSERT_TRUE(out.good());
            out << "int value = 1;\n";
        }

        SuggestionManagerConfig config;
        config.workspace_root = temp_root_;
        config.use_disk_backups = false;
        config.rerank_remaining_after_each_apply = false;
        config.build_validation_callback = [](const fs::path&) {
            BuildValidationResult result;
            result.duration_ms = 19;
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "test-build";
            diag.message = "injected build failure";
            result.errors.push_back(std::move(diag));
            return result;
        };
        SuggestionManager manager(config);
        SuggestionManagerTestAccess::seed_source_state(manager, temp_root_, source);
        SuggestionManagerTestAccess::seed_build_trace(manager, temp_root_, source);

        bha::Suggestion suggestion;
        suggestion.type = bha::SuggestionType::UnityBuild;
        suggestion.priority = bha::Priority::High;
        suggestion.is_safe = true;
        suggestion.edits.push_back(bha::TextEdit{
            .file = source,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "// generated\n"
        });
        SuggestionManagerTestAccess::set_bha_suggestion(manager, "ana-1", std::move(suggestion));

        const auto result = manager.apply_all_suggestions(std::nullopt, true);
        EXPECT_FALSE(result.success);
        EXPECT_TRUE(result.build_validation_ran);
        EXPECT_FALSE(result.build_validation_success);
        ASSERT_TRUE(result.build_validation_duration_ms.has_value());
        EXPECT_EQ(*result.build_validation_duration_ms, 19);
        EXPECT_EQ(result.build_validation_errors.size(), 1u);
        EXPECT_TRUE(result.rollback_attempted);
        EXPECT_TRUE(result.rollback_success);
        EXPECT_TRUE(result.backup_id.empty());
        EXPECT_TRUE(result.changed_files.empty());

        std::ifstream in(source);
        ASSERT_TRUE(in.good());
        EXPECT_EQ(
            std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()),
            "int value = 1;\n"
        );
    }

    TEST_F(SuggestionManagerRollbackTest, ApplyPreconditionAcceptsUnchangedSourceState) {
        const fs::path source = temp_root_ / "source.cpp";
        {
            std::ofstream out(source, std::ios::binary);
            ASSERT_TRUE(out.good());
            out << "int value = 1;\n";
        }

        SuggestionManagerConfig config;
        config.workspace_root = temp_root_;
        SuggestionManager manager(config);
        SuggestionManagerTestAccess::seed_source_state(manager, temp_root_, source);

        bha::Suggestion suggestion;
        suggestion.edits.push_back(bha::TextEdit{
            .file = source,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "// generated\n"
        });

        std::vector<Diagnostic> errors;
        EXPECT_TRUE(SuggestionManagerTestAccess::validate_suggestion_source_state(
            manager,
            suggestion,
            errors
        ));
        EXPECT_TRUE(errors.empty());
    }

    TEST_F(SuggestionManagerRollbackTest, ApplyPreconditionRejectsModifiedSourceState) {
        const fs::path source = temp_root_ / "source.cpp";
        {
            std::ofstream out(source, std::ios::binary);
            ASSERT_TRUE(out.good());
            out << "int value = 1;\n";
        }

        SuggestionManagerConfig config;
        config.workspace_root = temp_root_;
        SuggestionManager manager(config);
        SuggestionManagerTestAccess::seed_source_state(manager, temp_root_, source);
        {
            std::ofstream out(source, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(out.good());
            out << "int value = 2;\n";
        }

        bha::Suggestion suggestion;
        suggestion.edits.push_back(bha::TextEdit{
            .file = source,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "// generated\n"
        });

        std::vector<Diagnostic> errors;
        EXPECT_FALSE(SuggestionManagerTestAccess::validate_suggestion_source_state(
            manager,
            suggestion,
            errors
        ));
        ASSERT_EQ(errors.size(), 1u);
        EXPECT_NE(errors.front().message.find("changed since analysis"), std::string::npos);
    }

    TEST_F(SuggestionManagerRollbackTest, ApplyPreconditionRejectsDeletedSourceState) {
        const fs::path source = temp_root_ / "source.cpp";
        {
            std::ofstream out(source, std::ios::binary);
            ASSERT_TRUE(out.good());
            out << "int value = 1;\n";
        }

        SuggestionManagerConfig config;
        config.workspace_root = temp_root_;
        SuggestionManager manager(config);
        SuggestionManagerTestAccess::seed_source_state(manager, temp_root_, source);
        std::error_code ec;
        ASSERT_TRUE(fs::remove(source, ec));
        ASSERT_FALSE(ec);

        bha::Suggestion suggestion;
        suggestion.edits.push_back(bha::TextEdit{
            .file = source,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "// generated\n"
        });

        std::vector<Diagnostic> errors;
        EXPECT_FALSE(SuggestionManagerTestAccess::validate_suggestion_source_state(
            manager,
            suggestion,
            errors
        ));
        ASSERT_EQ(errors.size(), 1u);
        EXPECT_NE(errors.front().message.find("changed since analysis"), std::string::npos);
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
            out << "[{\"directory\":\"" << temp_root_.generic_string()
                << "\",\"file\":\"" << source_path.generic_string()
                << "\",\"command\":\"clang++ -c " << source_path.generic_string() << "\"}]";
        }

        BuildTrace trace;
        CompilationUnit unit;
        unit.source_file = source_path;
        unit.includes.push_back(IncludeInfo{
            .header = header_path,
            .parse_time = Duration::zero(),
            .depth = 0,
            .included_by = {},
            .symbols_used = {},
            .self_parse_time = std::nullopt
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

    TEST_F(SuggestionManagerRollbackTest, PCHSuggestionWithoutValidationTuIsAdvisoryOnly) {
        const fs::path include_dir = temp_root_ / "include";
        const fs::path src_dir = temp_root_ / "src";
        const fs::path header_path = include_dir / "hot.hpp";
        const fs::path unrelated_source = src_dir / "main.cpp";
        const fs::path compile_commands_path = temp_root_ / "compile_commands.json";
        const fs::path generated_pch = temp_root_ / "pch.h";

        fs::create_directories(include_dir);
        fs::create_directories(src_dir);
        {
            std::ofstream out(header_path);
            ASSERT_TRUE(out.good());
            out << "#pragma once\n";
        }
        {
            std::ofstream out(unrelated_source);
            ASSERT_TRUE(out.good());
            out << "int main() { return 0; }\n";
        }
        {
            std::ofstream out(compile_commands_path);
            ASSERT_TRUE(out.good());
            out << "[{\"directory\":\"" << temp_root_.generic_string()
                << "\",\"file\":\"" << unrelated_source.generic_string()
                << "\",\"command\":\"clang++ -c " << unrelated_source.generic_string() << "\"}]";
        }

        BuildTrace trace;
        CompilationUnit unit;
        unit.source_file = unrelated_source;
        trace.units.push_back(std::move(unit));

        bha::Suggestion suggestion;
        suggestion.type = bha::SuggestionType::PCHOptimization;
        suggestion.is_safe = true;
        suggestion.application_mode = bha::SuggestionApplicationMode::DirectEdits;
        suggestion.target_file.path = header_path;
        suggestion.target_file.action = bha::FileAction::Modify;
        suggestion.secondary_files.push_back(
            bha::FileTarget{.path = generated_pch, .action = bha::FileAction::Create}
        );
        suggestion.edits.push_back(bha::TextEdit{
            .file = generated_pch,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "#pragma once\n#include \"include/hot.hpp\"\n"
        });

        std::vector<bha::Suggestion> suggestions;
        suggestions.push_back(std::move(suggestion));

        SuggestionManagerTestAccess::enforce_pch_auto_apply_validation_readiness(
            suggestions,
            compile_commands_path,
            trace,
            temp_root_,
            true
        );

        ASSERT_EQ(suggestions.size(), 1u);
        EXPECT_TRUE(suggestions.front().edits.empty());
        EXPECT_EQ(
            suggestions.front().application_mode,
            bha::SuggestionApplicationMode::Advisory
        );
        ASSERT_TRUE(suggestions.front().auto_apply_blocked_reason.has_value());
        EXPECT_NE(
            suggestions.front().auto_apply_blocked_reason->find("compile-command-backed translation unit"),
            std::string::npos
        );
    }

    TEST_F(SuggestionManagerRollbackTest, CollectsGeneratedForwardHeadersForStandaloneValidation) {
        const fs::path source_header = temp_root_ / "include" / "widget.h";
        const fs::path forward_header = temp_root_ / "include" / "widget_fwd.h";
        const fs::path other_header = temp_root_ / "include" / "other.h";

        bha::Suggestion suggestion;
        suggestion.type = bha::SuggestionType::HeaderSplit;
        suggestion.edits.push_back(bha::TextEdit{
            .file = forward_header,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "#pragma once\nclass Widget;\n"
        });
        suggestion.edits.push_back(bha::TextEdit{
            .file = source_header,
            .start_line = 0,
            .start_col = 0,
            .end_line = 0,
            .end_col = 0,
            .new_text = "#include \"widget_fwd.h\"\n"
        });

        const auto headers = SuggestionManagerTestAccess::collect_generated_forward_headers(
            suggestion,
            {other_header, forward_header}
        );

        ASSERT_EQ(headers.size(), 1u);
        EXPECT_EQ(headers.front(), forward_header.lexically_normal());
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
