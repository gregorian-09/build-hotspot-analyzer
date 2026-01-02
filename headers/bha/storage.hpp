//
// Created by gregorian-rayne on 1/2/26.
//

#ifndef BHA_STORAGE_HPP
#define BHA_STORAGE_HPP

/**
 * @file storage.hpp
 * @brief Snapshot storage for build analysis comparison.
 *
 * Provides file-based storage for:
 * - Saving analysis results as named snapshots
 * - Comparing builds over time
 * - Setting baselines for regression detection
 *
 * Storage location: .bha/snapshots/ (project-local)
 * Format: JSON files with metadata and analysis results
 */

#include "bha/types.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace bha::storage
{
    /**
     * Metadata for a stored snapshot.
     */
    struct SnapshotMetadata {
        std::string name;                    // User-provided name
        std::string description;             // Optional description
        Timestamp created_at;                // When the snapshot was created
        std::string git_commit;              // Git commit hash (if available)
        std::string git_branch;              // Git branch name (if available)
        std::string compiler;                // Compiler used
        std::string compiler_version;        // Compiler version
        std::size_t file_count;              // Number of files analyzed
        Duration total_build_time;           // Total build time
        std::vector<std::string> tags;       // User-defined tags
    };

    /**
     * A complete snapshot including metadata and analysis results.
     */
    struct Snapshot {
        SnapshotMetadata metadata;
        analyzers::AnalysisResult analysis;
        std::vector<Suggestion> suggestions;
    };

    /**
     * Summary of differences between two snapshots.
     */
    struct ComparisonResult {
        // Overall changes
        Duration build_time_delta;           // Positive = slower, negative = faster
        double build_time_percent_change;    // Percentage change
        int64_t file_count_delta;            // Change in file count

        // Performance regressions (files that got slower)
        struct FileChange {
            fs::path file;
            Duration old_time;
            Duration new_time;
            Duration delta;
            double percent_change;
        };
        std::vector<FileChange> regressions;  // Files that got slower
        std::vector<FileChange> improvements; // Files that got faster
        std::vector<fs::path> new_files;      // Files in new but not old
        std::vector<fs::path> removed_files;  // Files in old but not new

        struct HeaderChange {
            fs::path header;
            std::size_t old_inclusions;
            std::size_t new_inclusions;
            Duration old_time;
            Duration new_time;
        };
        std::vector<HeaderChange> header_regressions;
        std::vector<HeaderChange> header_improvements;

        struct TemplateChange {
            std::string name;
            std::size_t old_count;
            std::size_t new_count;
            Duration old_time;
            Duration new_time;
        };
        std::vector<TemplateChange> template_regressions;
        std::vector<TemplateChange> template_improvements;

        bool is_regression() const { return build_time_delta.count() > 0; }
        bool is_improvement() const { return build_time_delta.count() < 0; }
        bool is_significant() const {
            return std::abs(build_time_percent_change) > 5.0; // >5% change
        }
    };

    /**
     * Storage manager for snapshots.
     */
    class SnapshotStore {
    public:
        /**
         * Creates a store at the given root directory.
         * Defaults to .bha/snapshots in current directory.
         */
        explicit SnapshotStore(const fs::path& root = ".bha/snapshots");

        /**
         * Saves a snapshot with the given name.
         *
         * @param name Unique name for the snapshot
         * @param analysis Analysis results to save
         * @param suggestions Optional suggestions to include
         * @param description Optional description
         * @param tags Optional tags for categorization
         */
        Result<void, Error> save(
            const std::string& name,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions = {},
            const std::string& description = "",
            const std::vector<std::string>& tags = {}
        ) const;

        /**
         * Loads a snapshot by name.
         */
        Result<Snapshot, Error> load(const std::string& name) const;

        /**
         * Lists all available snapshots.
         */
        Result<std::vector<SnapshotMetadata>, Error> list() const;

        /**
         * Deletes a snapshot.
         */
        Result<void, Error> remove(const std::string& name) const;

        /**
         * Checks if a snapshot exists.
         */
        bool exists(const std::string& name) const;

        /**
         * Gets the path to a snapshot file.
         */
        fs::path snapshot_path(const std::string& name) const;

        /**
         * Sets a snapshot as the baseline for comparisons.
         */
        Result<void, Error> set_baseline(const std::string& name) const;

        /**
         * Gets the current baseline snapshot name.
         */
        std::optional<std::string> get_baseline() const;

        /**
         * Clears the baseline.
         */
        Result<void, Error> clear_baseline() const;

        /**
         * Compares two snapshots.
         */
        Result<ComparisonResult, Error> compare(
            const std::string& old_name,
            const std::string& new_name
        ) const;

        /**
         * Compares analysis results against a snapshot.
         */
        Result<ComparisonResult, Error> compare_with_analysis(
            const std::string& snapshot_name,
            const analyzers::AnalysisResult& current
        ) const;

        /**
         * Gets the storage root directory.
         */
        const fs::path& root() const { return root_; }

    private:
        fs::path root_;
        fs::path baseline_file() const { return root_ / ".baseline"; }

        Result<void, Error> ensure_directory() const;
        static std::string get_git_commit();
        static std::string get_git_branch();
    };

    /**
     * Compares two analysis results directly.
     */
    ComparisonResult compare_analyses(
        const analyzers::AnalysisResult& old_result,
        const analyzers::AnalysisResult& new_result,
        double significance_threshold = 0.10  // 10% change is significant
    );

}

#endif //BHA_STORAGE_HPP
