#pragma once

#include "types.hpp"
#include "bha/types.hpp"
#include <string>
#include <vector>
#include <map>
#include <list>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace bha::lsp
{
    namespace fs = std::filesystem;

    /**
     * Progress callback for long-running operations.
     * @param message Current status message
     * @param percentage Progress percentage (0-100), or -1 for indeterminate
     */
    using ProgressCallback = std::function<void(const std::string& message, int percentage)>;

    /**
     * Configuration for SuggestionManager resource limits.
     *
     * These limits prevent unbounded memory growth in long-running LSP sessions.
     * When limits are exceeded, the oldest entries are evicted (LRU policy).
     */
    struct SuggestionManagerConfig {
        /// Maximum number of backups to retain (0 = unlimited)
        std::size_t max_backups = 10;

        /// Maximum total size of backup content in bytes (0 = unlimited)
        std::size_t max_backup_bytes = 100 * 1024 * 1024;  // 100 MB

        /// Maximum number of analysis results to cache (0 = unlimited)
        std::size_t max_analysis_cache = 5;

        /// Maximum number of suggestions to store (0 = unlimited)
        std::size_t max_suggestions = 1000;

        /// Enable disk-based backups (recommended for safety)
        bool use_disk_backups = true;

        /// Directory for disk-based backups (relative to workspace root)
        fs::path backup_directory = ".lsp-optimization-backup";

        /// Keep backups after successful validation
        bool keep_backups = false;

        /// Workspace root for resolving backup directory
        fs::path workspace_root;

        double min_confidence = 0.5;
        bool include_unsafe_suggestions = false;

        bool allow_missing_compile_commands = false;
        bool enforce_compile_command_syntax_gate = true;
        int compile_command_validation_timeout_seconds = 120;
        std::size_t max_compile_command_validation_units = 3;
        bool enable_expensive_include_cleanup_fallbacks = false;
        bool rerank_remaining_after_each_apply = false;
        std::vector<std::string> protected_include_patterns;

        static SuggestionManagerConfig defaults() {
            return SuggestionManagerConfig{};
        }
    };

    /**
     * @brief Result payload returned by project analysis.
     */
    struct AnalysisResult {
        /// Unique identifier for this analysis run.
        std::string analysis_id;
        /// Suggestions converted to LSP wire-friendly form.
        std::vector<Suggestion> suggestions;
        /// Baseline build metrics used for trust-loop comparisons.
        BuildMetrics baseline_metrics;
        /// Number of files analyzed in this run.
        int files_analyzed;
        /// Analysis wall-clock duration in milliseconds.
        int duration_ms;
    };

    /**
     * @brief Optional toggles that refine analysis/suggestion generation behavior.
     */
    struct AnalyzeSuggestionOptions {
        /// Restrict output to explicit suggestion types.
        std::vector<bha::SuggestionType> enabled_types;
        /// Override unsafe-suggestion inclusion policy.
        std::optional<bool> include_unsafe;
        /// Override confidence threshold.
        std::optional<double> min_confidence;
        /// Enable post-generation consolidation of overlapping suggestions.
        std::optional<bool> enable_consolidation;
        /// Loosen strict safety heuristics for exploratory workflows.
        bool relax_heuristics = false;
    };

    /**
     * @brief Result of applying a single suggestion/edit bundle.
     */
    struct ApplySuggestionResult {
        /// True when apply and required validation completed successfully.
        bool success;
        /// Files changed by the operation.
        std::vector<std::string> changed_files;
        /// Optional rebuild execution result.
        std::optional<BuildResult> build_result;
        /// Optional trust-loop validation summary.
        std::optional<ValidationResult> validation;
        /// Diagnostics explaining failures or advisory downgrades.
        std::vector<Diagnostic> errors;
        /// Backup identifier created prior to apply.
        std::optional<std::string> backup_id;
    };

    /**
     * @brief Aggregate result when applying multiple suggestions.
     */
    struct ApplyAllResult {
        /// True when overall batch operation is successful.
        bool success;
        /// Number of suggestions successfully applied.
        std::size_t applied_count;
        /// Number of suggestions skipped by filters or failures.
        std::size_t skipped_count;
        /// Union of changed files across applied suggestions.
        std::vector<std::string> changed_files;
        /// Suggestion identifiers applied successfully.
        std::vector<std::string> applied_suggestion_ids;
        /// Collected diagnostics across the batch.
        std::vector<Diagnostic> errors;
        /// Backup identifier for the batch operation.
        std::string backup_id;
    };

    /**
     * @brief Detailed revert operation result.
     */
    struct RevertResult {
        /// True when all requested files were restored.
        bool success;
        /// Files restored by the revert operation.
        std::vector<std::string> restored_files;
        /// Diagnostics for restore errors.
        std::vector<Diagnostic> errors;
    };

    /**
     * @brief Lightweight backup descriptor for list operations.
     */
    struct BackupSummary {
        std::string id;
        std::chrono::system_clock::time_point timestamp{};
        std::size_t file_count = 0;
        bool on_disk = false;
    };

    /**
     * @brief Snapshot of one file captured in a backup.
     */
    struct FileBackup {
        fs::path path;
        bool existed_before = false;
        std::string content;
    };

    /**
     * @brief Full backup payload used for restore operations.
     */
    struct Backup {
        std::string id;
        std::vector<FileBackup> files;
        std::chrono::system_clock::time_point timestamp;
    };

    /**
     * Manages build optimization suggestions for IDE integration.
     *
     * Thread Safety: This class is NOT thread-safe. All methods must be called
     * from the same thread (the LSP message processing thread). If async
     * operations are needed in the future, external synchronization or a
     * work queue pattern should be used.
     *
     * Memory Management: Uses LRU eviction for backups and caches to prevent
     * unbounded memory growth. Configure limits via SuggestionManagerConfig.
     */
    class SuggestionManager {
    public:
        /**
         * @brief Construct manager with resource/safety configuration.
         */
        explicit SuggestionManager(const SuggestionManagerConfig& config = SuggestionManagerConfig::defaults());

        /**
         * @brief Analyze a project and populate suggestion caches.
         *
         * @param project_root Project source root.
         * @param build_dir Optional build directory override.
         * @param trace_dir Optional trace directory override.
         * @param rebuild Whether trace recording should trigger a rebuild.
         * @param on_progress Optional progress callback.
         * @param analyze_options Optional suggester/heuristic overrides.
         * @param is_cancelled Optional cancellation predicate for long operations.
         * @return Analysis payload including generated suggestions.
         */
        AnalysisResult analyze_project(
            const fs::path& project_root,
            const std::optional<fs::path>& build_dir = std::nullopt,
            const std::optional<fs::path>& trace_dir = std::nullopt,
            bool rebuild = false,
            const ProgressCallback& on_progress = nullptr,
            const AnalyzeSuggestionOptions& analyze_options = {},
            const std::function<bool()>& is_cancelled = {}
        );

        /**
         * @brief Fetch detailed information for one suggestion.
         *
         * @param suggestion_id Suggestion identifier from current cache.
         * @return Detailed suggestion payload.
         */
        DetailedSuggestion get_suggestion_details(const std::string& suggestion_id);

        /**
         * @brief Apply one suggestion by identifier.
         *
         * @param suggestion_id Suggestion identifier.
         * @param skip_validation Skip syntax/rebuild validation checks.
         * @param skip_rebuild Skip rebuild even when policy requests it.
         * @param create_backup Create rollback backup before editing files.
         * @return Apply result including diagnostics.
         */
        ApplySuggestionResult apply_suggestion(
            const std::string& suggestion_id,
            bool skip_validation = false,
            bool skip_rebuild = false,
            bool create_backup = true
        );

        /**
         * @brief Apply an ad-hoc edit bundle without requiring a suggestion id.
         *
         * @param edits Text edits to apply.
         * @param create_backup Create rollback backup before editing files.
         * @return Apply result including diagnostics.
         */
        ApplySuggestionResult apply_edit_bundle(
            const std::vector<bha::TextEdit>& edits,
            bool create_backup = true
        );

        /**
         * @brief Apply selected suggestion IDs in explicit order.
         *
         * @param suggestion_ids Suggestion IDs to apply.
         * @param stop_on_error Stop batch at first failure when true.
         * @return Aggregate apply result.
         */
        ApplySuggestionResult apply_all_suggestions(
            const std::vector<std::string>& suggestion_ids,
            bool stop_on_error = true
        );

        /**
         * Apply all suggestions from the last analysis.
         * @param min_priority Optional minimum priority filter (e.g., "high", "medium")
         * @param safe_only If true, only apply suggestions that expose an automatic apply path
         * @return Result with counts and changed files
         */
        ApplyAllResult apply_all_suggestions(
            const std::optional<std::string>& min_priority = std::nullopt,
            bool safe_only = true,
            const std::function<void(const std::string&)>& progress_log = {}
        );

        /**
         * @brief Revert backup contents to restore previous file states.
         *
         * @param backup_id Backup identifier from list/apply result.
         * @param preserve_backup Keep backup metadata after successful restore.
         * @return True when restore succeeds fully.
         */
        bool revert_changes(const std::string& backup_id, bool preserve_backup = false);

        /**
         * Revert changes with detailed result.
         */
        RevertResult revert_changes_detailed(const std::string& backup_id, bool preserve_backup = false);
        /**
         * @brief List known backups in recency order.
         */
        [[nodiscard]] std::vector<BackupSummary> list_backups() const;

        /**
         * @brief Return all cached suggestions from the last analysis.
         */
        [[nodiscard]] std::vector<Suggestion> get_all_suggestions() const;
        /**
         * @brief Lookup one suggestion in LSP-converted cache.
         */
        [[nodiscard]] std::optional<Suggestion> get_suggestion(const std::string& id) const;
        /**
         * @brief Lookup raw BHA suggestion by id.
         */
        [[nodiscard]] const bha::Suggestion* get_bha_suggestion(const std::string& id) const;
        /**
         * @brief Return latest baseline metrics used for trust-loop reporting.
         */
        [[nodiscard]] std::optional<BuildMetrics> get_last_baseline_metrics() const;

    private:
        friend class SuggestionManagerTestAccess;

        /// Convert build trace into baseline metrics payload.
        static BuildMetrics extract_build_metrics(const BuildTrace& trace);

        /// Map improvement percentage to priority bucket.
        static Priority calculate_priority(double improvement_percentage) ;

        /// Convert core BHA suggestion to LSP surface model.
        static Suggestion convert_suggestion(const bha::Suggestion& bha_sug);
        /// Convert core BHA suggestion to detailed LSP model.
        static DetailedSuggestion convert_to_detailed(const bha::Suggestion& bha_sug);
        /**
         * @brief Collect compile-command-backed translation units for syntax validation.
         */
        static std::optional<std::vector<fs::path>> collect_compile_command_validation_sources(
            const std::optional<fs::path>& compile_commands_path,
            const BuildTrace& analysis_trace,
            const std::optional<fs::path>& project_root,
            const bha::Suggestion& suggestion,
            const std::vector<fs::path>& changed_files,
            const std::string& validation_label,
            std::vector<Diagnostic>& errors
        );
        /// Gather newly generated forward headers from apply change sets.
        static std::vector<fs::path> collect_generated_forward_headers(
            const bha::Suggestion& suggestion,
            const std::vector<fs::path>& changed_files
        );
        /**
         * @brief Validate generated forward headers with compile-command-derived flags.
         */
        static bool validate_generated_forward_headers_against_compile_commands(
            const bha::Suggestion& suggestion,
            const std::optional<fs::path>& compile_commands_path,
            const std::vector<fs::path>& changed_files,
            const std::vector<fs::path>& candidate_sources,
            const std::string& validation_label,
            int timeout_seconds,
            std::vector<Diagnostic>& errors
        );
        /**
         * @brief Downgrade/guard PCH suggestions when validation prerequisites are missing.
         */
        static void enforce_pch_auto_apply_validation_readiness(
            std::vector<bha::Suggestion>& suggestions,
            const std::optional<fs::path>& compile_commands_path,
            const BuildTrace& analysis_trace,
            const std::optional<fs::path>& project_root,
            bool enforce_compile_command_syntax_gate
        );

        /// Generate monotonic analysis identifier.
        std::string generate_analysis_id();
        /// Generate monotonic backup identifier.
        std::string generate_backup_id();

        /// Create backup using configured backing store policy.
        std::string create_backup(const std::vector<fs::path>& files);
        /// Capture in-memory backup snapshot.
        std::string create_memory_backup(const std::vector<fs::path>& files);
        /// Verify all files exist before apply operations.
        static bool validate_files_exist(const std::vector<fs::path>& files);
        /// Apply file edits for one suggestion and report touched files.
        static bool apply_file_changes(const bha::Suggestion& suggestion, std::vector<fs::path>& changed_files);
        /// Capture transactional snapshot used for rollback-on-failure.
        static bool capture_transactional_snapshot(
            const std::vector<fs::path>& files,
            std::vector<FileBackup>& snapshot,
            std::vector<Diagnostic>& errors
        );
        /// Restore transactional snapshot content after failed apply.
        static bool restore_transactional_snapshot(
            const std::vector<FileBackup>& snapshot,
            std::vector<Diagnostic>& errors
        );
        /// Append normalized changed-file paths to apply result.
        static void append_changed_files(
            ApplySuggestionResult& result,
            const std::vector<fs::path>& changed_files
        );
        /// Merge one successful single-apply result into batch aggregate.
        static void merge_apply_all_success(
            ApplyAllResult& result,
            const std::string& suggestion_id,
            const ApplySuggestionResult& apply_result,
            std::unordered_set<std::string>& changed_file_set
        );
        /// Merge one failed single-apply result into batch aggregate.
        static void merge_apply_all_failure(
            ApplyAllResult& result,
            const ApplySuggestionResult& apply_result
        );
        /// Create backup and attach failure diagnostics when it cannot be created.
        bool create_backup_for_files(
            const std::vector<fs::path>& files,
            std::string_view failure_message,
            ApplySuggestionResult& result
        );
        /// Check whether compile-command syntax validation can run for a suggestion.
        bool can_prepare_compile_command_validation(
            const bha::Suggestion& suggestion,
            std::vector<Diagnostic>& errors
        ) const;
        /// Execute external refactor apply flow.
        bool apply_external_refactor_suggestion(
            const std::string& suggestion_id,
            const bha::Suggestion& suggestion,
            bool create_backup_flag,
            ApplySuggestionResult& result,
            std::vector<fs::path>& changed_files
        );
        /// Execute direct text-edit apply flow.
        bool apply_direct_edit_suggestion(
            const std::string& suggestion_id,
            const bha::Suggestion& suggestion,
            bool create_backup_flag,
            bool enforce_syntax_validation,
            ApplySuggestionResult& result,
            std::vector<FileBackup>& transactional_snapshot,
            std::vector<fs::path>& changed_files
        );
        /// Roll back suggestion application after validation/build failure.
        bool rollback_apply_suggestion(
            ApplySuggestionResult& result,
            const std::vector<FileBackup>& transactional_snapshot,
            std::string_view rollback_failure_message
        );
        /// Run post-apply rebuild validation according to manager policy.
        bool validate_post_apply_rebuild(ApplySuggestionResult& result);
        /// Run compile-command-backed syntax validation for a suggestion.
        bool validate_compile_command_backed_suggestion(
            const bha::Suggestion& suggestion,
            const std::vector<fs::path>& changed_files,
            std::vector<Diagnostic>& errors
        ) const;

        /// Persist backup on disk and return identifier.
        std::string create_disk_backup(const std::vector<fs::path>& files);
        /// Restore a disk-backed backup by id.
        bool restore_disk_backup(const std::string& backup_id) const;
        /// Remove disk backup artifacts.
        void cleanup_disk_backup(const std::string& backup_id) const;
        /// Resolve backup directory path for one backup id.
        [[nodiscard]] fs::path get_backup_path(const std::string& backup_id) const;
        /// Write backup metadata file.
        static bool write_backup_metadata(const fs::path& backup_dir, const Backup& backup);
        /// Read backup metadata file.
        static std::optional<Backup> read_backup_metadata(const fs::path& backup_dir);

        /// Evicts oldest backups until under limit
        void evict_old_backups();

        /// Evicts oldest analysis cache entries until under limit
        void evict_old_analysis_cache();

        /// Calculate total backup size in bytes
        [[nodiscard]] std::size_t calculate_backup_size() const;

        SuggestionManagerConfig config_;

        std::map<std::string, DetailedSuggestion> suggestions_;
        std::map<std::string, BuildTrace> analysis_cache_;
        std::map<std::string, bha::Suggestion> bha_suggestions_;
        std::map<std::string, Backup> backups_;
        std::string last_analysis_id_;
        std::optional<fs::path> last_compile_commands_path_;
        std::optional<fs::path> last_project_root_;
        std::optional<fs::path> last_build_dir_;
        std::optional<fs::path> last_trace_dir_;
        AnalyzeSuggestionOptions last_analyze_options_;

        /// LRU tracking: front = oldest, back = newest
        std::list<std::string> backup_lru_;
        std::list<std::string> analysis_lru_;

        int analysis_counter_{0};
        int backup_counter_{0};
    };
}
