//
// Created by gregorian on 25/10/2025.
//

#include "bha/storage/sqlite_backend.h"
#include <fstream>
#include <utility>

namespace bha::storage {

    namespace Schema {
        auto SCHEMA_SQL = R"(
            CREATE TABLE IF NOT EXISTS schema_version (
                version TEXT PRIMARY KEY,
                migrated_at INTEGER NOT NULL
            );

            CREATE TABLE IF NOT EXISTS builds (
                id TEXT PRIMARY KEY,
                timestamp INTEGER NOT NULL,
                commit_sha TEXT,
                branch TEXT,
                configuration TEXT,
                platform TEXT,
                build_system TEXT,
                total_time_ms REAL NOT NULL,
                is_clean_build INTEGER NOT NULL,
                file_count INTEGER NOT NULL
            );

            CREATE INDEX IF NOT EXISTS idx_builds_timestamp ON builds(timestamp DESC);
            CREATE INDEX IF NOT EXISTS idx_builds_commit ON builds(commit_sha);

            CREATE TABLE IF NOT EXISTS compilation_units (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                build_id TEXT NOT NULL,
                file_path TEXT NOT NULL,
                total_time_ms REAL NOT NULL,
                preprocessing_time_ms REAL,
                parsing_time_ms REAL,
                codegen_time_ms REAL,
                file_size_bytes INTEGER,
                FOREIGN KEY (build_id) REFERENCES builds(id) ON DELETE CASCADE
            );

            CREATE INDEX IF NOT EXISTS idx_units_build_id ON compilation_units(build_id);
            CREATE INDEX IF NOT EXISTS idx_units_time ON compilation_units(total_time_ms DESC);

            CREATE TABLE IF NOT EXISTS dependencies (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                build_id TEXT NOT NULL,
                source_file TEXT NOT NULL,
                target_file TEXT NOT NULL,
                is_direct INTEGER NOT NULL,
                line_number INTEGER,
                FOREIGN KEY (build_id) REFERENCES builds(id) ON DELETE CASCADE
            );

            CREATE INDEX IF NOT EXISTS idx_deps_build_source ON dependencies(build_id, source_file);

            CREATE TABLE IF NOT EXISTS hotspots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                build_id TEXT NOT NULL,
                file_path TEXT NOT NULL,
                time_ms REAL NOT NULL,
                impact_score REAL NOT NULL,
                num_dependents INTEGER NOT NULL,
                category TEXT NOT NULL,
                FOREIGN KEY (build_id) REFERENCES builds(id) ON DELETE CASCADE
            );

            CREATE INDEX IF NOT EXISTS idx_hotspots_build ON hotspots(build_id);
        )";
    }

    SQLiteBackend::SQLiteBackend(std::string db_path)
        : db_path_(std::move(db_path)) {}

    SQLiteBackend::~SQLiteBackend() {
        if (db_) {
            sqlite3_close(db_);
        }
    }

    core::Result<void> SQLiteBackend::initialize() {
        std::scoped_lock lock(mutex_);

        if (const int rc = sqlite3_open(db_path_.c_str(), &db_); rc != SQLITE_OK) {
            const std::string error = sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to open database: " + error
            });
        }

        sqlite3_exec(db_, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "PRAGMA journal_mode = WAL", nullptr, nullptr, nullptr);

        return execute_schema();
    }

    core::Result<void> SQLiteBackend::close() {
        std::scoped_lock lock(mutex_);

        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }

        return core::Result<void>::success();
    }

    core::Result<void> SQLiteBackend::execute_schema() const
    {
        char* error_msg = nullptr;
        int rc = sqlite3_exec(db_, Schema::SCHEMA_SQL, nullptr, nullptr, &error_msg);

        if (rc != SQLITE_OK) {
            const std::string error = error_msg;
            sqlite3_free(error_msg);
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to execute schema: " + error
            });
        }

        const auto version_sql =
            "INSERT OR IGNORE INTO schema_version (version, migrated_at) "
            "VALUES ('1.0', strftime('%s', 'now'))";

        rc = sqlite3_exec(db_, version_sql, nullptr, nullptr, &error_msg);
        if (rc != SQLITE_OK) {
            const std::string error = error_msg;
            sqlite3_free(error_msg);
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to set schema version: " + error
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> SQLiteBackend::store_build(const BuildRecord& build) {
        std::scoped_lock lock(mutex_);

        const auto sql =
            "INSERT OR REPLACE INTO builds "
            "(id, timestamp, commit_sha, branch, configuration, platform, "
            " build_system, total_time_ms, is_clean_build, file_count) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement: " +
                          std::string(sqlite3_errmsg(db_))
            });
        }

        sqlite3_bind_text(stmt, 1, build.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, build.timestamp);
        sqlite3_bind_text(stmt, 3, build.commit_sha.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, build.branch.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, build.configuration.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, build.platform.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, build.build_system.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 8, build.total_time_ms);
        sqlite3_bind_int(stmt, 9, build.is_clean_build ? 1 : 0);
        sqlite3_bind_int(stmt, 10, build.file_count);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to insert build: " +
                          std::string(sqlite3_errmsg(db_))
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> SQLiteBackend::store_compilation_units(
        const std::vector<CompilationRecord>& units) {

        std::scoped_lock lock(mutex_);

        const auto sql =
            "INSERT INTO compilation_units "
            "(build_id, file_path, total_time_ms, preprocessing_time_ms, "
            " parsing_time_ms, codegen_time_ms, file_size_bytes) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        for (const auto& [build_id, file_path, total_time_ms, preprocessing_time_ms, parsing_time_ms, codegen_time_ms, file_size_bytes] : units) {
            sqlite3_bind_text(stmt, 1, build_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, file_path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(stmt, 3, total_time_ms);
            sqlite3_bind_double(stmt, 4, preprocessing_time_ms);
            sqlite3_bind_double(stmt, 5, parsing_time_ms);
            sqlite3_bind_double(stmt, 6, codegen_time_ms);
            sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(file_size_bytes));

            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                return core::Result<void>::failure(core::Error{
                    core::ErrorCode::DATABASE_ERROR,
                    "Failed to insert compilation unit"
                });
            }

            sqlite3_reset(stmt);
        }

        sqlite3_finalize(stmt);
        return core::Result<void>::success();
    }

    core::Result<void> SQLiteBackend::store_dependencies(
        const std::vector<DependencyRecord>& deps) {

        std::scoped_lock lock(mutex_);

        const auto sql =
            "INSERT INTO dependencies "
            "(build_id, source_file, target_file, is_direct, line_number) "
            "VALUES (?, ?, ?, ?, ?)";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        for (const auto& [build_id, source_file, target_file, is_direct, line_number] : deps) {
            sqlite3_bind_text(stmt, 1, build_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, source_file.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, target_file.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, is_direct ? 1 : 0);
            sqlite3_bind_int(stmt, 5, line_number);

            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                return core::Result<void>::failure(core::Error{
                    core::ErrorCode::DATABASE_ERROR,
                    "Failed to insert dependency"
                });
            }

            sqlite3_reset(stmt);
        }

        sqlite3_finalize(stmt);
        return core::Result<void>::success();
    }

    core::Result<void> SQLiteBackend::store_hotspots(
        const std::vector<HotspotRecord>& hotspots) {

        std::scoped_lock lock(mutex_);

        const auto sql =
            "INSERT INTO hotspots "
            "(build_id, file_path, time_ms, impact_score, num_dependents, category) "
            "VALUES (?, ?, ?, ?, ?, ?)";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        for (const auto& [build_id, file_path, time_ms, impact_score, num_dependents, category] : hotspots) {
            sqlite3_bind_text(stmt, 1, build_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, file_path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(stmt, 3, time_ms);
            sqlite3_bind_double(stmt, 4, impact_score);
            sqlite3_bind_int(stmt, 5, num_dependents);
            sqlite3_bind_text(stmt, 6, category.c_str(), -1, SQLITE_TRANSIENT);

            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                return core::Result<void>::failure(core::Error{
                    core::ErrorCode::DATABASE_ERROR,
                    "Failed to insert hotspot"
                });
            }

            sqlite3_reset(stmt);
        }

        sqlite3_finalize(stmt);
        return core::Result<void>::success();
    }

    core::Result<std::optional<BuildRecord>> SQLiteBackend::get_latest_build(
        const std::string& branch) {

        std::scoped_lock lock(mutex_);

        std::string sql =
            "SELECT id, timestamp, commit_sha, branch, configuration, platform, "
            "build_system, total_time_ms, is_clean_build, file_count "
            "FROM builds ";

        if (!branch.empty()) {
            sql += "WHERE branch = ? ";
        }

        sql += "ORDER BY timestamp DESC LIMIT 1";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<std::optional<BuildRecord>>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        if (!branch.empty()) {
            sqlite3_bind_text(stmt, 1, branch.c_str(), -1, SQLITE_TRANSIENT);
        }

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            BuildRecord record{
                .id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                .timestamp = sqlite3_column_int64(stmt, 1),
                .commit_sha = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
                .branch = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
                .configuration = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)),
                .platform = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)),
                .build_system = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)),
                .total_time_ms = sqlite3_column_double(stmt, 7),
                .is_clean_build = sqlite3_column_int(stmt, 8) != 0,
                .file_count = sqlite3_column_int(stmt, 9)
            };

            sqlite3_finalize(stmt);
            return core::Result<std::optional<BuildRecord>>::success(record);
        }

        sqlite3_finalize(stmt);
        return core::Result<std::optional<BuildRecord>>::success(std::nullopt);
    }

    core::Result<std::optional<BuildRecord>> SQLiteBackend::get_build(
        const std::string& build_id) {

        std::scoped_lock lock(mutex_);

        const auto sql =
            "SELECT id, timestamp, commit_sha, branch, configuration, platform, "
            "build_system, total_time_ms, is_clean_build, file_count "
            "FROM builds WHERE id = ?";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<std::optional<BuildRecord>>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        sqlite3_bind_text(stmt, 1, build_id.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            BuildRecord record{
                .id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                .timestamp = sqlite3_column_int64(stmt, 1),
                .commit_sha = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
                .branch = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
                .configuration = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)),
                .platform = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)),
                .build_system = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)),
                .total_time_ms = sqlite3_column_double(stmt, 7),
                .is_clean_build = sqlite3_column_int(stmt, 8) != 0,
                .file_count = sqlite3_column_int(stmt, 9)
            };

            sqlite3_finalize(stmt);
            return core::Result<std::optional<BuildRecord>>::success(record);
        }

        sqlite3_finalize(stmt);
        return core::Result<std::optional<BuildRecord>>::success(std::nullopt);
    }

    core::Result<std::optional<BuildRecord>> SQLiteBackend::get_build_by_commit(
        const std::string& commit_sha,
        const std::string& configuration) {

        std::scoped_lock lock(mutex_);

        std::string sql =
            "SELECT id, timestamp, commit_sha, branch, configuration, platform, "
            "build_system, total_time_ms, is_clean_build, file_count "
            "FROM builds WHERE commit_sha = ?";

        if (!configuration.empty()) {
            sql += " AND configuration = ?";
        }

        sql += " ORDER BY timestamp DESC LIMIT 1";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<std::optional<BuildRecord>>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        sqlite3_bind_text(stmt, 1, commit_sha.c_str(), -1, SQLITE_TRANSIENT);
        if (!configuration.empty()) {
            sqlite3_bind_text(stmt, 2, configuration.c_str(), -1, SQLITE_TRANSIENT);
        }

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            BuildRecord record{
                .id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                .timestamp = sqlite3_column_int64(stmt, 1),
                .commit_sha = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
                .branch = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
                .configuration = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)),
                .platform = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)),
                .build_system = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)),
                .total_time_ms = sqlite3_column_double(stmt, 7),
                .is_clean_build = sqlite3_column_int(stmt, 8) != 0,
                .file_count = sqlite3_column_int(stmt, 9)
            };

            sqlite3_finalize(stmt);
            return core::Result<std::optional<BuildRecord>>::success(record);
        }

        sqlite3_finalize(stmt);
        return core::Result<std::optional<BuildRecord>>::success(std::nullopt);
    }

    core::Result<std::vector<BuildRecord>> SQLiteBackend::list_builds(
        const int limit,
        const std::string& branch) {

        std::scoped_lock lock(mutex_);

        std::string sql =
            "SELECT id, timestamp, commit_sha, branch, configuration, platform, "
            "build_system, total_time_ms, is_clean_build, file_count "
            "FROM builds ";

        if (!branch.empty()) {
            sql += "WHERE branch = ? ";
        }

        sql += "ORDER BY timestamp DESC LIMIT ?";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<std::vector<BuildRecord>>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        int bind_idx = 1;
        if (!branch.empty()) {
            sqlite3_bind_text(stmt, bind_idx++, branch.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_int(stmt, bind_idx, limit);

        std::vector<BuildRecord> records;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            BuildRecord record{
                .id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                .timestamp = sqlite3_column_int64(stmt, 1),
                .commit_sha = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
                .branch = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
                .configuration = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)),
                .platform = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)),
                .build_system = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)),
                .total_time_ms = sqlite3_column_double(stmt, 7),
                .is_clean_build = sqlite3_column_int(stmt, 8) != 0,
                .file_count = sqlite3_column_int(stmt, 9)
            };
            records.push_back(record);
        }

        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            return core::Result<std::vector<BuildRecord>>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Error reading builds"
            });
        }

        return core::Result<std::vector<BuildRecord>>::success(records);
    }

    core::Result<std::vector<CompilationRecord>> SQLiteBackend::get_compilation_units(
        const std::string& build_id) {

        std::scoped_lock lock(mutex_);

        const auto sql =
            "SELECT build_id, file_path, total_time_ms, preprocessing_time_ms, "
            "parsing_time_ms, codegen_time_ms, file_size_bytes "
            "FROM compilation_units WHERE build_id = ? "
            "ORDER BY total_time_ms DESC";

        sqlite3_stmt* stmt;
        if (const int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr); rc != SQLITE_OK) {
            return core::Result<std::vector<CompilationRecord>>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        sqlite3_bind_text(stmt, 1, build_id.c_str(), -1, SQLITE_TRANSIENT);

        std::vector<CompilationRecord> records;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            CompilationRecord record{
                .build_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                .file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                .total_time_ms = sqlite3_column_double(stmt, 2),
                .preprocessing_time_ms = sqlite3_column_double(stmt, 3),
                .parsing_time_ms = sqlite3_column_double(stmt, 4),
                .codegen_time_ms = sqlite3_column_double(stmt, 5),
                .file_size_bytes = static_cast<size_t>(sqlite3_column_int64(stmt, 6))
            };
            records.push_back(record);
        }

        sqlite3_finalize(stmt);
        return core::Result<std::vector<CompilationRecord>>::success(records);
    }

    core::Result<std::vector<DependencyRecord>> SQLiteBackend::get_dependencies(
        const std::string& build_id) {

        std::scoped_lock lock(mutex_);

        const auto sql =
            "SELECT build_id, source_file, target_file, is_direct, line_number "
            "FROM dependencies WHERE build_id = ?";

        sqlite3_stmt* stmt;
        if (const int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr); rc != SQLITE_OK) {
            return core::Result<std::vector<DependencyRecord>>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        sqlite3_bind_text(stmt, 1, build_id.c_str(), -1, SQLITE_TRANSIENT);

        std::vector<DependencyRecord> records;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            DependencyRecord record{
                .build_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                .source_file = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                .target_file = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
                .is_direct = sqlite3_column_int(stmt, 3) != 0,
                .line_number = sqlite3_column_int(stmt, 4)
            };
            records.push_back(record);
        }

        sqlite3_finalize(stmt);
        return core::Result<std::vector<DependencyRecord>>::success(records);
    }

    core::Result<std::vector<HotspotRecord>> SQLiteBackend::get_hotspots(
        const std::string& build_id,
        const int limit) {

        std::scoped_lock lock(mutex_);

        const auto sql =
            "SELECT build_id, file_path, time_ms, impact_score, num_dependents, category "
            "FROM hotspots WHERE build_id = ? "
            "ORDER BY impact_score DESC LIMIT ?";

        sqlite3_stmt* stmt;
        if (const int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr); rc != SQLITE_OK) {
            return core::Result<std::vector<HotspotRecord>>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        sqlite3_bind_text(stmt, 1, build_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);

        std::vector<HotspotRecord> records;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            HotspotRecord record{
                .build_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                .file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                .time_ms = sqlite3_column_double(stmt, 2),
                .impact_score = sqlite3_column_double(stmt, 3),
                .num_dependents = sqlite3_column_int(stmt, 4),
                .category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))
            };
            records.push_back(record);
        }

        sqlite3_finalize(stmt);
        return core::Result<std::vector<HotspotRecord>>::success(records);
    }

    core::Result<ComparisonResult> SQLiteBackend::compare_builds(
        const std::string& baseline_id,
        const std::string& current_id) {

        auto baseline_result = get_build(baseline_id);
        if (!baseline_result.is_success() || !baseline_result.value().has_value()) {
            return core::Result<ComparisonResult>::failure(core::Error{
                core::ErrorCode::NOT_FOUND,
                "Baseline build not found"
            });
        }

        auto current_result = get_build(current_id);
        if (!current_result.is_success() || !current_result.value().has_value()) {
            return core::Result<ComparisonResult>::failure(core::Error{
                core::ErrorCode::NOT_FOUND,
                "Current build not found"
            });
        }

        auto baseline = baseline_result.value().value();
        auto current = current_result.value().value();

        double time_delta = current.total_time_ms - baseline.total_time_ms;
        double time_delta_percent = (time_delta / baseline.total_time_ms) * 100.0;

        auto baseline_units_result = get_compilation_units(baseline_id);
        auto current_units_result = get_compilation_units(current_id);

        if (!baseline_units_result.is_success() || !current_units_result.is_success()) {
            return core::Result<ComparisonResult>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to load compilation units"
            });
        }

        auto baseline_units = baseline_units_result.value();
        auto current_units = current_units_result.value();

        std::unordered_map<std::string, double> baseline_times;
        for (const auto& unit : baseline_units) {
            baseline_times[unit.file_path] = unit.total_time_ms;
        }

        std::vector<std::string> regression_files;
        std::vector<std::string> improved_files;
        std::vector<std::string> new_hotspots;

        for (const auto& unit : current_units) {
            if (auto it = baseline_times.find(unit.file_path); it == baseline_times.end()) {
                if (unit.total_time_ms > 1000.0) {
                    new_hotspots.push_back(unit.file_path);
                }
            } else {
                double delta = unit.total_time_ms - it->second;

                if (double delta_percent = (delta / it->second) * 100.0; delta_percent > 10.0) {
                    regression_files.push_back(unit.file_path);
                } else if (delta_percent < -10.0) {
                    improved_files.push_back(unit.file_path);
                }
            }
        }

        return core::Result<ComparisonResult>::success(ComparisonResult{
            .baseline = baseline,
            .current = current,
            .time_delta_ms = current.total_time_ms - baseline.total_time_ms,
            .time_delta_percent = time_delta_percent,
            .new_hotspots = new_hotspots,
            .regression_files = regression_files,
            .improved_files = improved_files
        });
    }

    core::Result<void> SQLiteBackend::cleanup_old_builds(const int retention_days) {
        std::scoped_lock lock(mutex_);

        const auto sql =
            "DELETE FROM builds WHERE timestamp < strftime('%s', 'now', ?)";

        const std::string interval = "-" + std::to_string(retention_days) + " days";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to prepare statement"
            });
        }

        sqlite3_bind_text(stmt, 1, interval.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to cleanup old builds"
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> SQLiteBackend::begin_transaction() {
        std::scoped_lock lock(mutex_);

        char* error_msg = nullptr;

        if (const int rc = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &error_msg); rc != SQLITE_OK) {
            const std::string error = error_msg;
            sqlite3_free(error_msg);
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to begin transaction: " + error
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> SQLiteBackend::commit_transaction() {
        std::scoped_lock lock(mutex_);

        char* error_msg = nullptr;

        if (const int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &error_msg); rc != SQLITE_OK) {
            const std::string error = error_msg;
            sqlite3_free(error_msg);
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to commit transaction: " + error
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> SQLiteBackend::rollback_transaction() {
        std::scoped_lock lock(mutex_);

        char* error_msg = nullptr;

        if (const int rc = sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &error_msg); rc != SQLITE_OK) {
            const std::string error = error_msg;
            sqlite3_free(error_msg);
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to rollback transaction: " + error
            });
        }

        return core::Result<void>::success();
    }

    std::unique_ptr<DatabaseBackend> create_sqlite_backend(
        const std::string& db_path) {
        return std::make_unique<SQLiteBackend>(db_path);
    }

    core::Result<void> SQLiteBackend::execute_sql(const std::string& sql) {
        std::scoped_lock lock(mutex_);

        char* error_msg = nullptr;

        if (const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg); rc != SQLITE_OK) {
            const std::string error = error_msg ? error_msg : "Unknown error";
            sqlite3_free(error_msg);
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "SQL execution failed: " + error
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> SQLiteBackend::check_schema_version() {
        std::scoped_lock lock(mutex_);

        const auto sql = "SELECT version FROM schema_version LIMIT 1";

        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::DATABASE_ERROR,
                "Failed to check schema version: " +
                          std::string(sqlite3_errmsg(db_))
            });
        }

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            const auto version = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 0));

            sqlite3_finalize(stmt);

            if (std::string(version) != "1.0") {
                return core::Result<void>::failure(core::Error{
                    core::ErrorCode::DATABASE_ERROR,
                    "Incompatible schema version: " +
                              std::string(version) + " (expected 1.0)"
                });
            }

            return core::Result<void>::success();
        }

        sqlite3_finalize(stmt);

        return core::Result<void>::failure(core::Error{
            core::ErrorCode::DATABASE_ERROR,
            "Schema version not found"
        });
    }

} // namespace bha::storage