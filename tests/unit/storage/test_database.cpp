//
// Created by gregorian on 10/12/2025.
//

#include <gtest/gtest.h>
#include "bha/storage/database.h"
#include <memory>
#include <ranges>
#include <unordered_map>

using namespace bha::storage;
using namespace bha::core;

class MockDatabaseBackend final : public DatabaseBackend {
public:
    std::unordered_map<std::string, BuildRecord> builds_;
    std::unordered_map<std::string, std::vector<CompilationRecord>> compilation_units_;
    std::unordered_map<std::string, std::vector<DependencyRecord>> dependencies_;
    std::unordered_map<std::string, std::vector<HotspotRecord>> hotspots_;
    bool initialized_ = false;
    bool in_transaction_ = false;

    Result<void> initialize() override {
        initialized_ = true;
        return Result<void>::success();
    }

    Result<void> close() override {
        initialized_ = false;
        return Result<void>::success();
    }

    Result<void> store_build(const BuildRecord& build) override {
        builds_[build.id] = build;
        return Result<void>::success();
    }

    Result<void> store_compilation_units(const std::vector<CompilationRecord>& units) override {
        if (!units.empty()) {
            compilation_units_[units[0].build_id] = units;
        }
        return Result<void>::success();
    }

    Result<void> store_dependencies(const std::vector<DependencyRecord>& deps) override {
        if (!deps.empty()) {
            dependencies_[deps[0].build_id] = deps;
        }
        return Result<void>::success();
    }

    Result<void> store_hotspots(const std::vector<HotspotRecord>& hotspots) override {
        if (!hotspots.empty()) {
            hotspots_[hotspots[0].build_id] = hotspots;
        }
        return Result<void>::success();
    }

    Result<std::optional<BuildRecord>> get_build(const std::string& build_id) override {
        auto it = builds_.find(build_id);
        if (it != builds_.end()) {
            return Result<std::optional<BuildRecord>>::success(it->second);
        }
        return Result<std::optional<BuildRecord>>::success(std::nullopt);
    }

    Result<std::optional<BuildRecord>> get_latest_build(const std::string& branch) override {
        BuildRecord* latest = nullptr;
        for (auto& build : builds_ | std::views::values) {
            if (branch.empty() || build.branch == branch) {
                if (!latest || build.timestamp > latest->timestamp) {
                    latest = &build;
                }
            }
        }
        if (latest) {
            return Result<std::optional<BuildRecord>>::success(*latest);
        }
        return Result<std::optional<BuildRecord>>::success(std::nullopt);
    }

    Result<std::optional<BuildRecord>> get_build_by_commit(
        const std::string& commit_sha,
        const std::string& configuration) override {
        for (const auto& build : builds_ | std::views::values) {
            if (build.commit_sha == commit_sha &&
                (configuration.empty() || build.configuration == configuration)) {
                return Result<std::optional<BuildRecord>>::success(build);
            }
        }
        return Result<std::optional<BuildRecord>>::success(std::nullopt);
    }

    Result<std::vector<BuildRecord>> list_builds(const int limit, const std::string& branch) override {
        std::vector<BuildRecord> result;
        for (const auto& build : builds_ | std::views::values) {
            if (branch.empty() || build.branch == branch) {
                result.push_back(build);
            }
        }
        if (result.size() > static_cast<size_t>(limit)) {
            result.resize(limit);
        }
        return Result<std::vector<BuildRecord>>::success(result);
    }

    Result<std::vector<CompilationRecord>> get_compilation_units(
        const std::string& build_id) override {
        if (const auto it = compilation_units_.find(build_id); it != compilation_units_.end()) {
            return Result<std::vector<CompilationRecord>>::success(it->second);
        }
        return Result<std::vector<CompilationRecord>>::success(std::vector<CompilationRecord>{});
    }

    Result<std::vector<DependencyRecord>> get_dependencies(
        const std::string& build_id) override {
        if (const auto it = dependencies_.find(build_id); it != dependencies_.end()) {
            return Result<std::vector<DependencyRecord>>::success(it->second);
        }
        return Result<std::vector<DependencyRecord>>::success(std::vector<DependencyRecord>{});
    }

    Result<std::vector<HotspotRecord>> get_hotspots(
        const std::string& build_id, const int limit) override {
        if (const auto it = hotspots_.find(build_id); it != hotspots_.end()) {
            auto result = it->second;
            if (result.size() > static_cast<size_t>(limit)) {
                result.resize(limit);
            }
            return Result<std::vector<HotspotRecord>>::success(result);
        }
        return Result<std::vector<HotspotRecord>>::success(std::vector<HotspotRecord>{});
    }

    Result<ComparisonResult> compare_builds(
        const std::string& baseline_id,
        const std::string& current_id) override {
        const auto baseline_it = builds_.find(baseline_id);
        const auto current_it = builds_.find(current_id);

        if (baseline_it == builds_.end() || current_it == builds_.end()) {
            return Result<ComparisonResult>::failure(Error{
                ErrorCode::NOT_FOUND,
                "Build not found"
            });
        }

        ComparisonResult result;
        result.baseline = baseline_it->second;
        result.current = current_it->second;
        result.time_delta_ms = current_it->second.total_time_ms - baseline_it->second.total_time_ms;
        result.time_delta_percent = (result.time_delta_ms / baseline_it->second.total_time_ms) * 100.0;

        return Result<ComparisonResult>::success(result);
    }

    Result<void> cleanup_old_builds(int retention_days) override {
        const auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24 * retention_days);
        const auto cutoff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            cutoff.time_since_epoch()).count();

        std::vector<std::string> to_delete;
        for (const auto& [id, build] : builds_) {
            if (build.timestamp < cutoff_ms) {
                to_delete.push_back(id);
            }
        }

        for (const auto& id : to_delete) {
            builds_.erase(id);
            compilation_units_.erase(id);
            dependencies_.erase(id);
            hotspots_.erase(id);
        }

        return Result<void>::success();
    }

    Result<void> begin_transaction() override {
        in_transaction_ = true;
        return Result<void>::success();
    }

    Result<void> commit_transaction() override {
        in_transaction_ = false;
        return Result<void>::success();
    }

    Result<void> rollback_transaction() override {
        in_transaction_ = false;
        return Result<void>::success();
    }
};

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto backend = std::make_unique<MockDatabaseBackend>();
        mock_backend_ = backend.get();
        database_ = std::make_unique<Database>(std::move(backend));
    }

    MockDatabaseBackend* mock_backend_ = nullptr;
    std::unique_ptr<Database> database_;

    static BuildTrace create_test_trace() {
        BuildTrace trace;
        trace.commit_sha = "abc123";
        trace.branch = "main";
        trace.total_build_time_ms = 5000.0;

        CompilationUnit unit;
        unit.file_path = "test.cpp";
        unit.total_time_ms = 1000.0;
        trace.compilation_units.push_back(unit);

        trace.dependency_graph.add_edge("test.cpp", "test.h");

        Hotspot hotspot;
        hotspot.file_path = "test.h";
        hotspot.time_ms = 500.0;
        trace.metrics.top_slow_files.push_back(hotspot);

        return trace;
    }
};

TEST_F(DatabaseTest, InitializeAndClose) {
    auto init_result = database_->initialize();
    ASSERT_TRUE(init_result.is_success());
    EXPECT_TRUE(mock_backend_->initialized_);

    auto close_result = database_->close();
    ASSERT_TRUE(close_result.is_success());
    EXPECT_FALSE(mock_backend_->initialized_);
}

TEST_F(DatabaseTest, StoreBuildTrace) {
    auto build_result = database_->initialize();
    ASSERT_TRUE(build_result.is_success());

    auto trace = create_test_trace();
    auto result = database_->store_build_trace(trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_FALSE(result.value().empty());

    EXPECT_EQ(mock_backend_->builds_.size(), 1);
    const auto& stored_build = mock_backend_->builds_.begin()->second;
    EXPECT_EQ(stored_build.commit_sha, "abc123");
    EXPECT_EQ(stored_build.branch, "main");
    EXPECT_EQ(mock_backend_->compilation_units_.size(), 1);
    EXPECT_EQ(mock_backend_->dependencies_.size(), 1);
    EXPECT_EQ(mock_backend_->hotspots_.size(), 1);
}

TEST_F(DatabaseTest, GetBaseline) {
    auto build_result = database_->initialize();
    ASSERT_TRUE(build_result.is_success());

    auto trace = create_test_trace();
    auto store_result = database_->store_build_trace(trace);
    ASSERT_TRUE(store_result.is_success());

    auto baseline_result = database_->get_baseline("main");
    ASSERT_TRUE(baseline_result.is_success());
    ASSERT_TRUE(baseline_result.value().has_value());

    const auto& baseline = baseline_result.value().value();
    EXPECT_EQ(baseline.commit_sha, "abc123");
    EXPECT_EQ(baseline.branch, "main");
}

TEST_F(DatabaseTest, GetBaselineNonExistent) {
    auto build_result = database_->initialize();
    ASSERT_TRUE(build_result.is_success());
    auto baseline_result = database_->get_baseline("nonexistent");
    ASSERT_TRUE(baseline_result.is_success());
    EXPECT_FALSE(baseline_result.value().has_value());
}

TEST_F(DatabaseTest, CompareWithBaseline) {
    auto build_result = database_->initialize();
    ASSERT_TRUE(build_result.is_success());

    auto baseline_trace = create_test_trace();
    baseline_trace.total_build_time_ms = 5000.0;
    auto baseline_result = database_->store_build_trace(baseline_trace);
    ASSERT_TRUE(baseline_result.is_success());
    
    auto current_trace = create_test_trace();
    current_trace.total_build_time_ms = 6000.0;
    current_trace.commit_sha = "def456";

    auto comparison_result = database_->compare_with_baseline(current_trace, "main");
    ASSERT_TRUE(comparison_result.is_success());

    const auto& comparison = comparison_result.value();
    EXPECT_EQ(comparison.baseline.commit_sha, "abc123");
    EXPECT_EQ(comparison.current.commit_sha, "def456");
    EXPECT_GT(comparison.time_delta_ms, 0);
}

TEST_F(DatabaseTest, GetRecentBuilds) {
    auto initialize_result = database_->initialize();
    ASSERT_TRUE(initialize_result.is_success());

    for (int i = 0; i < 5; i++) {
        auto trace = create_test_trace();
        trace.commit_sha = "commit" + std::to_string(i);
        auto build_result = database_->store_build_trace(trace);
        ASSERT_TRUE(build_result.is_success());
    }

    auto result = database_->get_recent_builds(3);
    ASSERT_TRUE(result.is_success());
    EXPECT_LE(result.value().size(), 3);
}

TEST_F(DatabaseTest, Cleanup) {
    auto initialize_result = database_->initialize();
    ASSERT_TRUE(initialize_result.is_success());

    const auto trace = create_test_trace();
    auto build_result = database_->store_build_trace(trace);
    ASSERT_TRUE(build_result.is_success());

    EXPECT_EQ(mock_backend_->builds_.size(), 1);

    const auto cleanup_result = database_->cleanup(0);  // Delete everything
    ASSERT_TRUE(cleanup_result.is_success());

    // Verify old builds were removed
    EXPECT_EQ(mock_backend_->builds_.size(), 0);
}