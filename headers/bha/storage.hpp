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
        struct CategoryDelta {
            Duration old_time = Duration::zero();
            Duration new_time = Duration::zero();
            Duration delta = Duration::zero();
            /// Undefined when the old value is zero or unavailable.
            std::optional<double> percent_change;
        };

        /**
         * Empirical distribution of positive deltas across matched files.
         *
         * The values are computed from every matched translation unit, before
         * the user-configured significance threshold filters the detail list.
         */
        struct RegressionDistribution {
            std::size_t matched_files = 0;
            std::size_t regressed_files = 0;
            Duration total_delta = Duration::zero();
            Duration min_delta = Duration::zero();
            Duration median_delta = Duration::zero();
            Duration p90_delta = Duration::zero();
            Duration p99_delta = Duration::zero();
            Duration max_delta = Duration::zero();
        };

        /**
         * Empirical distribution of total build times from explicit repeated
         * observations. No threshold or confidence claim is attached to these
         * descriptive statistics.
         */
        struct RepeatedRunDistribution {
            std::size_t run_count = 0;
            Duration min_build_time = Duration::zero();
            Duration mean_build_time = Duration::zero();
            Duration median_build_time = Duration::zero();
            Duration p90_build_time = Duration::zero();
            Duration p99_build_time = Duration::zero();
            Duration max_build_time = Duration::zero();
            std::optional<Duration> sample_standard_deviation;
        };

        // Overall changes
        Duration build_time_delta;           // Positive = slower, negative = faster
        /// Undefined when the old build time is zero or unavailable.
        std::optional<double> build_time_percent_change;
        int64_t file_count_delta;            // Change in file count
        double significance_threshold_percent = 5.0;

        CategoryDelta translation_unit;
        CategoryDelta headers;
        CategoryDelta templates;
        RegressionDistribution translation_unit_regressions;

        // Performance regressions (files that got slower)
        struct FileChange {
            fs::path file;
            Duration old_time;
            Duration new_time;
            Duration delta;
            /// Undefined when the old file time is zero or unavailable.
            std::optional<double> percent_change;
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
            return build_time_percent_change.has_value() &&
                   std::abs(*build_time_percent_change) > significance_threshold_percent;
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
            const std::string& new_name,
            double significance_threshold = 0.10
        ) const;

        /**
         * Compares analysis results against a snapshot.
         */
        Result<ComparisonResult, Error> compare_with_analysis(
            const std::string& snapshot_name,
            const analyzers::AnalysisResult& current,
            double significance_threshold = 0.10
        ) const;

        /**
         * Summarizes total build times from explicitly named snapshots.
         * Duplicate names are rejected so one stored observation cannot be
         * counted as multiple repeated measurements.
         */
        Result<ComparisonResult::RepeatedRunDistribution, Error> summarize_repeated_runs(
            const std::vector<std::string>& snapshot_names
        ) const;

        /**
         * Gets the storage root directory.
         */
        const fs::path& root() const { return root_; }

    private:
        fs::path root_;
        fs::path baseline_file() const { return root_ / ".baseline"; }

        Result<void, Error> ensure_directory() const;
    };

    /**
     * Compares two analysis results directly.
     */
    ComparisonResult compare_analyses(
        const analyzers::AnalysisResult& old_result,
        const analyzers::AnalysisResult& new_result,
        double significance_threshold = 0.10  // 10% change is significant
    );

    /**
     * Summarizes observed total build times from at least two repeated analyses.
     */
    Result<ComparisonResult::RepeatedRunDistribution, Error> summarize_repeated_analyses(
        const std::vector<analyzers::AnalysisResult>& analyses
    );

}

#endif //BHA_STORAGE_HPP
