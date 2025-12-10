//
// Created by gregorian on 10/12/2025.
//

#include <gtest/gtest.h>
#include "bha/storage/sqlite_backend.h"
#include <filesystem>
#include <chrono>

using namespace bha::storage;
using namespace bha::core;

class SQLiteBackendTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto temp_dir = std::filesystem::temp_directory_path();
        test_db_path_ = (temp_dir / ("test_bha_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".db")).string();
        backend_ = std::make_unique<SQLiteBackend>(test_db_path_);
    }

    void TearDown() override {
        backend_->close();
        backend_.reset();
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove(test_db_path_);
        }
    }

    std::unique_ptr<SQLiteBackend> backend_;
    std::string test_db_path_;

    static BuildRecord create_test_build(const std::string& id = "build123") {
        BuildRecord build;
        build.id = id;
        build.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        build.commit_sha = "abc123def456";
        build.branch = "main";
        build.configuration = "Release";
        build.platform = "Linux";
        build.build_system = "CMake";
        build.total_time_ms = 15000.0;
        build.is_clean_build = true;
        build.file_count = 100;
        return build;
    }

    static std::vector<CompilationRecord> create_test_units(const std::string& build_id) {
        std::vector<CompilationRecord> units;
        for (int i = 0; i < 3; i++) {
            CompilationRecord unit;
            unit.build_id = build_id;
            unit.file_path = "/src/file" + std::to_string(i) + ".cpp";
            unit.total_time_ms = 1000.0 * (i + 1);
            unit.preprocessing_time_ms = 100.0 * (i + 1);
            unit.parsing_time_ms = 200.0 * (i + 1);
            unit.codegen_time_ms = 700.0 * (i + 1);
            unit.file_size_bytes = 10000 + i * 1000;
            units.push_back(unit);
        }
        return units;
    }

    static std::vector<DependencyRecord> create_test_dependencies(const std::string& build_id) {
        std::vector<DependencyRecord> deps;
        for (int i = 0; i < 2; i++) {
            DependencyRecord dep;
            dep.build_id = build_id;
            dep.source_file = "/src/file" + std::to_string(i) + ".cpp";
            dep.target_file = "/include/header" + std::to_string(i) + ".h";
            dep.is_direct = true;
            dep.line_number = 10 + i;
            deps.push_back(dep);
        }
        return deps;
    }

    static std::vector<HotspotRecord> create_test_hotspots(const std::string& build_id) {
        std::vector<HotspotRecord> hotspots;
        for (int i = 0; i < 3; i++) {
            HotspotRecord hotspot;
            hotspot.build_id = build_id;
            hotspot.file_path = "/include/hotspot" + std::to_string(i) + ".h";
            hotspot.time_ms = 500.0 * (3 - i);
            hotspot.impact_score = 0.9 - (i * 0.1);
            hotspot.num_dependents = 50 - (i * 10);
            hotspot.category = (i == 0) ? "header" : "template";
            hotspots.push_back(hotspot);
        }
        return hotspots;
    }
};

TEST_F(SQLiteBackendTest, InitializeCreatesDatabase) {
    const auto result = backend_->initialize();
    if (!result.is_success()) {
        std::cerr << "Initialize failed: " << result.error().message << std::endl;
    }
    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(test_db_path_));
}

TEST_F(SQLiteBackendTest, CloseDatabase) {
    backend_->initialize();
    const auto result = backend_->close();
    ASSERT_TRUE(result.is_success());
}

TEST_F(SQLiteBackendTest, StoreBuild) {
    backend_->initialize();

    auto build = create_test_build();
    auto result = backend_->store_build(build);
    ASSERT_TRUE(result.is_success());

    auto get_result = backend_->get_build("build123");
    ASSERT_TRUE(get_result.is_success());
    ASSERT_TRUE(get_result.value().has_value());

    const auto& retrieved = get_result.value().value();
    EXPECT_EQ(retrieved.id, "build123");
    EXPECT_EQ(retrieved.commit_sha, "abc123def456");
    EXPECT_EQ(retrieved.branch, "main");
    EXPECT_EQ(retrieved.configuration, "Release");
    EXPECT_NEAR(retrieved.timestamp, build.timestamp, 1000);
}

TEST_F(SQLiteBackendTest, StoreCompilationUnits) {
    backend_->initialize();

    auto build = create_test_build();
    backend_->store_build(build);

    auto units = create_test_units("build123");
    auto result = backend_->store_compilation_units(units);
    ASSERT_TRUE(result.is_success());

    auto get_result = backend_->get_compilation_units("build123");
    ASSERT_TRUE(get_result.is_success());
    EXPECT_EQ(get_result.value().size(), 3);
}

TEST_F(SQLiteBackendTest, StoreDependencies) {
    backend_->initialize();

    auto build = create_test_build();
    backend_->store_build(build);

    auto deps = create_test_dependencies("build123");
    auto result = backend_->store_dependencies(deps);
    ASSERT_TRUE(result.is_success());

    auto get_result = backend_->get_dependencies("build123");
    ASSERT_TRUE(get_result.is_success());
    EXPECT_EQ(get_result.value().size(), 2);
}

TEST_F(SQLiteBackendTest, StoreHotspots) {
    backend_->initialize();

    auto build = create_test_build();
    backend_->store_build(build);

    auto hotspots = create_test_hotspots("build123");
    auto result = backend_->store_hotspots(hotspots);
    ASSERT_TRUE(result.is_success());

    auto get_result = backend_->get_hotspots("build123", 2);
    ASSERT_TRUE(get_result.is_success());
    EXPECT_LE(get_result.value().size(), 2);
}

TEST_F(SQLiteBackendTest, GetLatestBuild) {
    backend_->initialize();

    for (int i = 0; i < 3; i++) {
        auto build = create_test_build("build" + std::to_string(i));
        build.timestamp += i * 1000;
        backend_->store_build(build);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto result = backend_->get_latest_build("main");
    ASSERT_TRUE(result.is_success());
    ASSERT_TRUE(result.value().has_value());

    EXPECT_EQ(result.value().value().id, "build2");
}

TEST_F(SQLiteBackendTest, GetBuildByCommit) {
    backend_->initialize();

    auto build = create_test_build();
    backend_->store_build(build);

    auto result = backend_->get_build_by_commit("abc123def456", "Release");
    ASSERT_TRUE(result.is_success());
    ASSERT_TRUE(result.value().has_value());
    EXPECT_EQ(result.value().value().id, "build123");
}

TEST_F(SQLiteBackendTest, ListBuildsWithLimit) {
    backend_->initialize();

    for (int i = 0; i < 5; i++) {
        auto build = create_test_build("build" + std::to_string(i));
        backend_->store_build(build);
    }

    auto result = backend_->list_builds(3, "main");
    ASSERT_TRUE(result.is_success());
    EXPECT_LE(result.value().size(), 3);
}

TEST_F(SQLiteBackendTest, CompareBuilds) {
    backend_->initialize();

    auto baseline = create_test_build("baseline");
    baseline.total_time_ms = 10000.0;
    backend_->store_build(baseline);

    auto current = create_test_build("current");
    current.total_time_ms = 12000.0;
    backend_->store_build(current);

    auto result = backend_->compare_builds("baseline", "current");
    ASSERT_TRUE(result.is_success());

    const auto& comparison = result.value();
    EXPECT_EQ(comparison.baseline.id, "baseline");
    EXPECT_EQ(comparison.current.id, "current");
    EXPECT_GT(comparison.time_delta_ms, 0);
    EXPECT_GT(comparison.time_delta_percent, 0);
}

TEST_F(SQLiteBackendTest, CleanupOldBuilds) {
    backend_->initialize();

    auto old_build = create_test_build("old_build");
    auto old_time = std::chrono::system_clock::now() - std::chrono::hours(24 * 100);
    old_build.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        old_time.time_since_epoch()).count();
    backend_->store_build(old_build);

    auto recent_build = create_test_build("recent_build");
    backend_->store_build(recent_build);

    auto result = backend_->cleanup_old_builds(90);
    ASSERT_TRUE(result.is_success());

    auto old_result = backend_->get_build("old_build");
    ASSERT_TRUE(old_result.is_success());
    EXPECT_FALSE(old_result.value().has_value());

    auto recent_result = backend_->get_build("recent_build");
    ASSERT_TRUE(recent_result.is_success());
    EXPECT_TRUE(recent_result.value().has_value());
}

TEST_F(SQLiteBackendTest, TransactionCommit) {
    backend_->initialize();

    auto begin_result = backend_->begin_transaction();
    ASSERT_TRUE(begin_result.is_success());

    auto build = create_test_build();
    auto store_result = backend_->store_build(build);
    ASSERT_TRUE(store_result.is_success());

    auto commit_result = backend_->commit_transaction();
    ASSERT_TRUE(commit_result.is_success());

    auto get_result = backend_->get_build("build123");
    ASSERT_TRUE(get_result.is_success());
    ASSERT_TRUE(get_result.value().has_value());
}

TEST_F(SQLiteBackendTest, TransactionRollback) {
    backend_->initialize();

    auto build1 = create_test_build("build1");
    backend_->store_build(build1);

    backend_->begin_transaction();
    auto build2 = create_test_build("build2");
    backend_->store_build(build2);

    auto rollback_result = backend_->rollback_transaction();
    ASSERT_TRUE(rollback_result.is_success());

    auto get1 = backend_->get_build("build1");
    ASSERT_TRUE(get1.is_success());
    EXPECT_TRUE(get1.value().has_value());

    auto get2 = backend_->get_build("build2");
    ASSERT_TRUE(get2.is_success());
    EXPECT_FALSE(get2.value().has_value());
}

TEST_F(SQLiteBackendTest, GetNonExistentBuild) {
    backend_->initialize();

    auto result = backend_->get_build("nonexistent");
    ASSERT_TRUE(result.is_success());
    EXPECT_FALSE(result.value().has_value());
}

TEST_F(SQLiteBackendTest, EmptyDatabase) {
    backend_->initialize();

    auto result = backend_->list_builds(10, "");
    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(result.value().empty());
}