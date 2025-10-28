//
// Created by gregorian on 25/10/2025.
//

#ifndef SQLITE_BACKEND_H
#define SQLITE_BACKEND_H

#pragma once

#include <functional>

#include "bha/storage/database.h"
#include <sqlite3.h>
#include <mutex>

    namespace bha::storage {

    /**
     * SQLite-based implementation of the DatabaseBackend interface.
     *
     * Provides persistent storage for build traces, compilation data, dependency
     * relationships, and performance hotspots using an embedded SQLite database.
     * All schema management and data access are handled internally.
     *
     * Thread safety is ensured through internal mutex locking.
     */
    class SQLiteBackend final : public DatabaseBackend {
    public:
        /**
         * Construct a new SQLiteBackend object.
         *
         * @param db_path Path to the SQLite database file. The file is created if it does not exist.
         */
        explicit SQLiteBackend(std::string  db_path);

        /// Destructor that closes the connection and frees all SQLite resources.
        ~SQLiteBackend() override;

        /**
         * Initialize the SQLite database.
         *
         * Opens the database connection and ensures that the required schema
         * (tables, indexes, and version information) is present.
         *
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> initialize() override;

        /**
         * Close the database connection.
         *
         * Safely closes any open SQLite handles.
         *
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> close() override;

        /**
         * Insert a build record.
         *
         * @param build Build metadata and timing information.
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> store_build(const BuildRecord& build) override;

        /**
         * Insert multiple compilation unit records.
         *
         * @param units List of compilation unit data entries.
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> store_compilation_units(
            const std::vector<CompilationRecord>& units) override;

        /**
         * Insert dependency information for a build.
         *
         * @param deps List of dependency edges between source and target files.
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> store_dependencies(
            const std::vector<DependencyRecord>& deps) override;

        /**
         * Insert hotspot metrics for a build.
         *
         * @param hotspots Vector of hotspot records to store.
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> store_hotspots(
            const std::vector<HotspotRecord>& hotspots) override;

        /**
         * Retrieve a specific build record.
         *
         * @param build_id Unique identifier of the build.
         * @return Result containing an optional BuildRecord if found.
         */
        core::Result<std::optional<BuildRecord>> get_build(
            const std::string& build_id) override;

        /**
         * Retrieve the most recent build for a branch.
         *
         * @param branch Branch name filter.
         * @return Result containing the latest build or std::nullopt if not found.
         */
        core::Result<std::optional<BuildRecord>> get_latest_build(
            const std::string& branch) override;

        /**
         * Retrieve a build by commit SHA and configuration.
         *
         * @param commit_sha Commit hash to search.
         * @param configuration Optional configuration name (Debug, Release, etc.).
         * @return Result containing the build record or std::nullopt.
         */
        core::Result<std::optional<BuildRecord>> get_build_by_commit(
            const std::string& commit_sha,
            const std::string& configuration) override;

        /**
         * List multiple build records.
         *
         * @param limit Maximum number of builds to return.
         * @param branch Branch name filter.
         * @return Result containing a vector of BuildRecord entries.
         */
        core::Result<std::vector<BuildRecord>> list_builds(
            int limit, const std::string& branch) override;

        /**
         * Retrieve compilation records for a build.
         *
         * @param build_id Build identifier.
         * @return Result containing a vector of CompilationRecord entries.
         */
        core::Result<std::vector<CompilationRecord>> get_compilation_units(
            const std::string& build_id) override;

        /**
         * Retrieve dependency edges for a build.
         *
         * @param build_id Build identifier.
         * @return Result containing a vector of DependencyRecord entries.
         */
        core::Result<std::vector<DependencyRecord>> get_dependencies(
            const std::string& build_id) override;

        /**
         * Retrieve the most significant hotspots for a build.
         *
         * @param build_id Build identifier.
         * @param limit Maximum number of hotspot entries.
         * @return Result containing a vector of HotspotRecord entries.
         */
        core::Result<std::vector<HotspotRecord>> get_hotspots(
            const std::string& build_id, int limit) override;

        /**
         * Compare performance metrics between two builds.
         *
         * @param baseline_id Baseline build ID.
         * @param current_id Current build ID.
         * @return Result containing a ComparisonResult summarizing differences.
         */
        core::Result<ComparisonResult> compare_builds(
            const std::string& baseline_id,
            const std::string& current_id) override;

        /**
         * Delete builds older than a given retention period.
         *
         * @param retention_days Number of days to retain build history.
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> cleanup_old_builds(int retention_days) override;

        /**
         * Begin a database transaction.
         *
         * Used to group multiple write operations atomically.
         *
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> begin_transaction() override;

        /**
         * Commit the current transaction.
         *
         * Persists all operations since begin_transaction().
         *
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> commit_transaction() override;

        /**
         * Roll back the current transaction.
         *
         * Reverts all changes made since begin_transaction().
         *
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> rollback_transaction() override;

    private:
        /// Path to the SQLite database file.
        std::string db_path_;

        /// SQLite database handle pointer.
        sqlite3* db_ = nullptr;

        /// Mutex to ensure thread-safe access.
        std::mutex mutex_;

        /**
         * Execute a raw SQL command without returning rows.
         *
         * @param sql SQL statement to execute.
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> execute_sql(const std::string& sql);

        /**
         * Create or migrate the database schema.
         *
         * Ensures that all required tables and indices exist.
         *
         * @return core::Result<void> indicating success or error.
         */
        [[nodiscard]] core::Result<void> execute_schema() const;

        /**
         * Validate and update schema version metadata.
         *
         * @return core::Result<void> indicating success or error.
         */
        core::Result<void> check_schema_version();

        /**
         * Execute a query and process results with a user-provided handler.
         *
         * @tparam T Type of the processed query result.
         * @param sql SQL statement to prepare and execute.
         * @param handler Function invoked for each prepared statement result.
         * @return core::Result<T> containing handler output.
         */
        template<typename T>
        core::Result<T> execute_query(
            const std::string& sql,
            std::function<core::Result<T>(sqlite3_stmt*)> handler) {

            std::scoped_lock lock(mutex_);

            sqlite3_stmt* stmt;
            int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                return core::Result<T>::failure(core::Error{
                    core::ErrorCode::DATABASE_ERROR,
                    "Failed to prepare query: " +
                              std::string(sqlite3_errmsg(db_))
                });
            }

            rc = sqlite3_step(stmt);

            if (rc == SQLITE_ROW) {
                auto result = handler(stmt);
                sqlite3_finalize(stmt);
                return result;
            }
            if (rc == SQLITE_DONE) {
                sqlite3_finalize(stmt);
                return core::Result<T>::failure(core::Error{
                    core::ErrorCode::NOT_FOUND,
                    "No results found"
                });
            }
            const std::string error = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return core::Result<T>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Query execution failed: " + error
            });
        }
    };

} // namespace bha::storage


#endif //SQLITE_BACKEND_H
