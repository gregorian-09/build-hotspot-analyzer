//
// Created by gregorian on 25/10/2025.
//

#ifndef DATABASE_H
#define DATABASE_H

#include "bha/core/types.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "bha/core/result.h"

namespace bha::storage {

    /**
     * Represents metadata and statistics for a single build.
     */
    struct BuildRecord {
        std::string id;                   ///< Unique build identifier (UUID or hash).
        int64_t timestamp;                ///< Build timestamp in UNIX milliseconds.
        std::string commit_sha;           ///< Associated commit SHA.
        std::string branch;               ///< Git branch name.
        std::string configuration;        ///< Build configuration (e.g., Debug, Release).
        std::string platform;             ///< Target platform name.
        std::string build_system;         ///< Build system used (CMake, Ninja, etc.).
        double total_time_ms;             ///< Total build duration in milliseconds.
        bool is_clean_build;              ///< True if this was a clean build.
        int file_count;                   ///< Number of compiled files.
    };

    /**
     * Represents the timing and size metrics of a single compilation unit.
     */
    struct CompilationRecord {
        std::string build_id;             ///< Parent build ID.
        std::string file_path;            ///< Source file path.
        double total_time_ms;             ///< Total compilation time.
        double preprocessing_time_ms;     ///< Time spent in preprocessing.
        double parsing_time_ms;           ///< Time spent in parsing.
        double codegen_time_ms;           ///< Time spent in code generation.
        size_t file_size_bytes;           ///< Size of the source file in bytes.
    };

    /**
     * Represents a single dependency relationship between source files.
     */
    struct DependencyRecord {
        std::string build_id;             ///< Parent build ID.
        std::string source_file;          ///< File including another.
        std::string target_file;          ///< File being included.
        bool is_direct;                   ///< True if direct inclusion, false if transitive.
        int line_number;                  ///< Line number of inclusion (if known).
    };

    /**
     * Represents a performance hotspot in the build process.
     */
    struct HotspotRecord {
        std::string build_id;             ///< Parent build ID.
        std::string file_path;            ///< File contributing to build slowdown.
        double time_ms;                   ///< Time attributed to this file.
        double impact_score;              ///< Weighted score indicating performance impact.
        int num_dependents;               ///< Number of files depending on this file.
        std::string category;             ///< Hotspot type (e.g., header, source, template).
    };

    /**
     * Represents a comparison of build performance between two builds.
     */
    struct ComparisonResult {
        BuildRecord baseline;                     ///< Baseline build record.
        BuildRecord current;                      ///< Current build record.
        double time_delta_ms;                     ///< Absolute change in build time.
        double time_delta_percent;                ///< Relative change (%) in build time.
        std::vector<std::string> new_hotspots;    ///< Files that became new hotspots.
        std::vector<std::string> regression_files;///< Files that regressed in performance.
        std::vector<std::string> improved_files;  ///< Files that improved in performance.
    };

    /**
     * Abstract interface for persistent build data storage.
     *
     * Implementations handle the persistence and retrieval of build-related data,
     * including build metadata, compilation units, dependencies, and hotspots.
     */
    class DatabaseBackend {

        /// Retrieve the latest build (uses empty branch by default).
        core::Result<std::optional<BuildRecord>> get_latest_build() {
            return get_latest_build("");
        }

        /// Retrieve a build by commit SHA (uses empty configuration by default).
        core::Result<std::optional<BuildRecord>> get_build_by_commit(
            const std::string& commit_sha) {
            return get_build_by_commit(commit_sha, "");
        }

        /// List builds with default limit and branch.
        core::Result<std::vector<BuildRecord>> list_builds() {
            return list_builds(100, "");
        }

        /// List builds for a specific branch with default limit.
        core::Result<std::vector<BuildRecord>> list_builds(
            const std::string& branch) {
            return list_builds(100, branch);
        }

        /// Retrieve top hotspots with default limit.
        core::Result<std::vector<HotspotRecord>> get_hotspots(
            const std::string& build_id) {
            return get_hotspots(build_id, 20);
        }

        /// Delete old builds with default retention period of 90 days.
        core::Result<void> cleanup_old_builds() {
            return cleanup_old_builds(90);
        }

    public:
        virtual ~DatabaseBackend() = default;

        /// Initialize backend (create tables, open connections, etc.)
        virtual core::Result<void> initialize() = 0;

        /// Close backend and release all resources.
        virtual core::Result<void> close() = 0;

        /// Store a build record.
        virtual core::Result<void> store_build(const BuildRecord& build) = 0;

        /// Store compilation unit records.
        virtual core::Result<void> store_compilation_units(
            const std::vector<CompilationRecord>& units) = 0;

        /// Store dependency relationships.
        virtual core::Result<void> store_dependencies(
            const std::vector<DependencyRecord>& deps) = 0;

        /// Store hotspot data.
        virtual core::Result<void> store_hotspots(
            const std::vector<HotspotRecord>& hotspots) = 0;

        /// Retrieve a build by ID.
        virtual core::Result<std::optional<BuildRecord>> get_build(
            const std::string& build_id) = 0;

        /// Retrieve the latest build for a given branch.
        virtual core::Result<std::optional<BuildRecord>> get_latest_build(
            const std::string& branch) = 0;

        /// Retrieve a build by commit SHA and configuration.
        virtual core::Result<std::optional<BuildRecord>> get_build_by_commit(
            const std::string& commit_sha,
            const std::string& configuration) = 0;

        /// List multiple builds for a branch.
        virtual core::Result<std::vector<BuildRecord>> list_builds(
            int limit,
            const std::string& branch) = 0;

        /// Retrieve compilation units for a build.
        virtual core::Result<std::vector<CompilationRecord>> get_compilation_units(
            const std::string& build_id) = 0;

        /// Retrieve dependency data for a build.
        virtual core::Result<std::vector<DependencyRecord>> get_dependencies(
            const std::string& build_id) = 0;

        /// Retrieve top hotspots for a build.
        virtual core::Result<std::vector<HotspotRecord>> get_hotspots(
            const std::string& build_id,
            int limit) = 0;

        /// Compare metrics between two builds.
        virtual core::Result<ComparisonResult> compare_builds(
            const std::string& baseline_id,
            const std::string& current_id) = 0;

        /// Delete old builds beyond a retention period.
        virtual core::Result<void> cleanup_old_builds(int retention_days) = 0;

        /// Begin a transaction for grouped operations.
        virtual core::Result<void> begin_transaction() = 0;

        /// Commit current transaction.
        virtual core::Result<void> commit_transaction() = 0;

        /// Roll back current transaction.
        virtual core::Result<void> rollback_transaction() = 0;
    };

    /**
     * High-level interface for managing build data.
     *
     * The Database class provides a convenient API for storing, loading,
     * and comparing build traces using a pluggable backend (e.g., SQLite).
     */
    class Database {
    public:
        /**
         * Construct a new Database instance.
         *
         * @param backend Ownership of a backend implementation.
         */
        explicit Database(std::unique_ptr<DatabaseBackend> backend);

        /// Initialize underlying backend.
        [[nodiscard]] core::Result<void> initialize() const;

        /// Close the database connection.
        [[nodiscard]] core::Result<void> close() const;

        /**
         * Store a complete build trace into the database.
         *
         * @param trace Full build trace containing metrics, dependencies, etc.
         * @return Result containing the generated build ID.
         */
        [[nodiscard]] core::Result<std::string> store_build_trace(const core::BuildTrace& trace) const;

        /**
         * Load a stored build trace by ID.
         *
         * @param build_id Identifier of the build to load.
         * @return Optional BuildTrace if found.
         */
        [[nodiscard]] core::Result<std::optional<core::BuildTrace>> load_build_trace(const std::string& build_id) const;

        /**
         * Get the most recent baseline build for a branch/configuration.
         */
        [[nodiscard]] core::Result<std::optional<BuildRecord>> get_baseline(
            const std::string& branch = "main") const;

        /**
         * Compare a build trace with its baseline.
         *
         * @param current_trace The new build trace to compare.
         * @param branch Branch name used for baseline lookup.
         * @return Result containing a ComparisonResult summary.
         */
        [[nodiscard]] core::Result<ComparisonResult> compare_with_baseline(
            const core::BuildTrace& current_trace,
            const std::string& branch = "main") const;

        /**
         * Retrieve recent builds.
         *
         * @param limit Maximum number of builds.
         * @return List of BuildRecord objects.
         */
        [[nodiscard]] core::Result<std::vector<BuildRecord>> get_recent_builds(int limit = 10) const;

        /**
         * Clean up old builds based on retention policy.
         */
        [[nodiscard]] core::Result<void> cleanup(int retention_days = 90) const;

    private:
        /// Underlying database backend.
        std::unique_ptr<DatabaseBackend> backend_;

        /// Convert a BuildTrace to a persistent BuildRecord.
        static BuildRecord trace_to_record(const core::BuildTrace& trace);

        /// Convert compilation data to records.
        static std::vector<CompilationRecord> units_to_records(
            const core::BuildTrace& trace, const std::string& build_id);

        /// Convert dependency graph to records.
        static std::vector<DependencyRecord> graph_to_records(
            const core::DependencyGraph& graph, const std::string& build_id);

        /// Convert metrics to hotspot records.
        static std::vector<HotspotRecord> hotspots_to_records(
            const core::MetricsSummary& metrics, const std::string& build_id);
    };

    /**
     * Create a SQLite-backed database implementation.
     *
     * @param db_path Path to SQLite database file.
     * @return Unique pointer to DatabaseBackend.
     */
    std::unique_ptr<DatabaseBackend> create_sqlite_backend(
        const std::string& db_path);

} // namespace bha::storage

#endif //DATABASE_H
