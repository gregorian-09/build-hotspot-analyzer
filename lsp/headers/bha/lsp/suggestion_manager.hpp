#pragma once

#include "types.hpp"
#include "bha/types.hpp"
#include <string>
#include <vector>
#include <map>
#include <list>
#include <filesystem>
#include <functional>

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

        static SuggestionManagerConfig defaults() {
            return SuggestionManagerConfig{};
        }
    };

    struct AnalysisResult {
        std::string analysis_id;
        std::vector<Suggestion> suggestions;
        BuildMetrics baseline_metrics;
        int files_analyzed;
        int duration_ms;
    };

    struct AnalyzeSuggestionOptions {
        std::vector<bha::SuggestionType> enabled_types;
        std::optional<bool> include_unsafe;
        std::optional<double> min_confidence;
        std::optional<bool> enable_consolidation;
        bool relax_heuristics = false;
    };

    struct ApplySuggestionResult {
        bool success;
        std::vector<std::string> changed_files;
        std::optional<BuildResult> build_result;
        std::optional<ValidationResult> validation;
        std::vector<Diagnostic> errors;
        std::optional<std::string> backup_id;
    };

    struct ApplyAllResult {
        bool success;
        std::size_t applied_count;
        std::size_t skipped_count;
        std::vector<std::string> changed_files;
        std::vector<std::string> applied_suggestion_ids;
        std::vector<Diagnostic> errors;
        std::string backup_id;
    };

    struct RevertResult {
        bool success;
        std::vector<std::string> restored_files;
        std::vector<Diagnostic> errors;
    };

    struct FileBackup {
        fs::path path;
        std::string content;
    };

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
        explicit SuggestionManager(const SuggestionManagerConfig& config = SuggestionManagerConfig::defaults());

        AnalysisResult analyze_project(
            const fs::path& project_root,
            const std::optional<fs::path>& build_dir = std::nullopt,
            bool rebuild = false,
            const ProgressCallback& on_progress = nullptr,
            const AnalyzeSuggestionOptions& analyze_options = {}
        );

        DetailedSuggestion get_suggestion_details(const std::string& suggestion_id);

        ApplySuggestionResult apply_suggestion(
            const std::string& suggestion_id,
            bool skip_validation = false,
            bool skip_rebuild = false,
            bool create_backup = true
        );

        ApplySuggestionResult apply_edit_bundle(
            const std::vector<bha::TextEdit>& edits,
            bool create_backup = true
        );

        ApplySuggestionResult apply_all_suggestions(
            const std::vector<std::string>& suggestion_ids,
            bool stop_on_error = true
        );

        /**
         * Apply all suggestions from the last analysis.
         * @param min_priority Optional minimum priority filter (e.g., "high", "medium")
         * @param safe_only If true, only apply suggestions marked as safe
         * @return Result with counts and changed files
         */
        ApplyAllResult apply_all_suggestions(
            const std::optional<std::string>& min_priority = std::nullopt,
            bool safe_only = true
        );

        bool revert_changes(const std::string& backup_id);

        /**
         * Revert changes with detailed result.
         */
        RevertResult revert_changes_detailed(const std::string& backup_id);

        [[nodiscard]] std::vector<Suggestion> get_all_suggestions() const;
        [[nodiscard]] std::optional<Suggestion> get_suggestion(const std::string& id) const;
        [[nodiscard]] const bha::Suggestion* get_bha_suggestion(const std::string& id) const;
        [[nodiscard]] std::optional<BuildMetrics> get_last_baseline_metrics() const;

    private:
        static BuildMetrics extract_build_metrics(const BuildTrace& trace);

        static Priority calculate_priority(double improvement_percentage) ;

        static Suggestion convert_suggestion(const bha::Suggestion& bha_sug);
        static DetailedSuggestion convert_to_detailed(const bha::Suggestion& bha_sug);

        std::string generate_analysis_id();
        std::string generate_backup_id();

        std::string create_backup(const std::vector<fs::path>& files);
        static bool validate_files_exist(const std::vector<fs::path>& files);
        static bool apply_file_changes(const bha::Suggestion& suggestion, std::vector<fs::path>& changed_files);

        std::string create_disk_backup(const std::vector<fs::path>& files);
        bool restore_disk_backup(const std::string& backup_id) const;
        void cleanup_disk_backup(const std::string& backup_id) const;
        [[nodiscard]] fs::path get_backup_path(const std::string& backup_id) const;
        static bool write_backup_metadata(const fs::path& backup_dir, const Backup& backup);
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

        /// LRU tracking: front = oldest, back = newest
        std::list<std::string> backup_lru_;
        std::list<std::string> analysis_lru_;

        int analysis_counter_{0};
        int backup_counter_{0};
    };
}
