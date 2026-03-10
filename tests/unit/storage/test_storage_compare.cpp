#include <gtest/gtest.h>

#include "bha/storage.hpp"

#include <filesystem>

namespace bha::storage::test {

namespace {
analyzers::AnalysisResult make_analysis(
    const std::vector<std::pair<std::string, Duration>>& files,
    const Duration build_time,
    const Duration header_time,
    const Duration template_time
) {
    analyzers::AnalysisResult analysis;
    analysis.performance.total_build_time = build_time;
    analysis.dependencies.total_include_time = header_time;
    analysis.templates.total_template_time = template_time;

    for (const auto& [name, time] : files) {
        analyzers::FileAnalysisResult file;
        file.file = name;
        file.compile_time = time;
        analysis.files.push_back(file);
    }
    return analysis;
}
}  // namespace

TEST(StorageCompareTest, ComputesCategoryPercentChanges) {
    const auto old_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(600)}, {"b.cpp", std::chrono::milliseconds(400)}},
        std::chrono::milliseconds(1000),
        std::chrono::milliseconds(200),
        std::chrono::milliseconds(100)
    );
    const auto new_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(750)}, {"b.cpp", std::chrono::milliseconds(450)}},
        std::chrono::milliseconds(1200),
        std::chrono::milliseconds(230),
        std::chrono::milliseconds(80)
    );

    const auto comparison = compare_analyses(old_analysis, new_analysis, 0.05);

    EXPECT_NEAR(comparison.build_time_percent_change, 20.0, 1e-9);
    EXPECT_NEAR(comparison.translation_unit.percent_change, 20.0, 1e-9);
    EXPECT_NEAR(comparison.headers.percent_change, 15.0, 1e-9);
    EXPECT_NEAR(comparison.templates.percent_change, -20.0, 1e-9);
    EXPECT_NEAR(comparison.significance_threshold_percent, 5.0, 1e-9);
    EXPECT_TRUE(comparison.is_significant());
}

TEST(StorageCompareTest, SignificanceThresholdIsConfigurable) {
    const auto old_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(1000)}},
        std::chrono::milliseconds(1000),
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(100)
    );
    const auto new_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(1030)}},
        std::chrono::milliseconds(1030),
        std::chrono::milliseconds(103),
        std::chrono::milliseconds(100)
    );

    const auto comparison_5 = compare_analyses(old_analysis, new_analysis, 0.05);
    const auto comparison_2 = compare_analyses(old_analysis, new_analysis, 0.02);

    EXPECT_FALSE(comparison_5.is_significant());
    EXPECT_TRUE(comparison_2.is_significant());
}

TEST(StorageCompareTest, FileRegressionThresholdFollowsConfiguredSignificance) {
    const auto old_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(100)}},
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(10),
        std::chrono::milliseconds(5)
    );
    const auto new_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(104)}},
        std::chrono::milliseconds(104),
        std::chrono::milliseconds(10),
        std::chrono::milliseconds(5)
    );

    const auto comparison_5 = compare_analyses(old_analysis, new_analysis, 0.05);
    const auto comparison_2 = compare_analyses(old_analysis, new_analysis, 0.02);

    EXPECT_TRUE(comparison_5.regressions.empty());
    ASSERT_EQ(comparison_2.regressions.size(), 1u);
    EXPECT_NEAR(comparison_2.regressions.front().percent_change, 4.0, 1e-9);
}

TEST(StorageSnapshotTest, PersistsCacheDistributionMetrics) {
    namespace fs = std::filesystem;

    const auto unique = std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    const fs::path root = fs::temp_directory_path() / ("bha-cache-snapshot-" + unique);

    SnapshotStore store(root);
    analyzers::AnalysisResult analysis;
    analysis.performance.total_build_time = std::chrono::milliseconds(4200);
    analysis.cache_distribution.total_compilations = 12;
    analysis.cache_distribution.cache_friendly_compilations = 9;
    analysis.cache_distribution.cache_risk_compilations = 3;
    analysis.cache_distribution.cache_hit_opportunity_percent = 75.0;
    analysis.cache_distribution.sccache_detected = true;
    analysis.cache_distribution.fastbuild_detected = false;
    analysis.cache_distribution.cache_wrapper_detected = true;
    analysis.cache_distribution.dynamic_macro_risk_count = 1;
    analysis.cache_distribution.profile_or_coverage_risk_count = 1;
    analysis.cache_distribution.pch_generation_risk_count = 0;
    analysis.cache_distribution.volatile_path_risk_count = 2;
    analysis.cache_distribution.distributed_suitability_score = 61.5;
    analysis.cache_distribution.heavy_translation_units = 4;
    analysis.cache_distribution.homogeneous_command_units = 7;

    const auto save_result = store.save("cache-metrics", analysis);
    ASSERT_TRUE(save_result.is_ok());

    const auto load_result = store.load("cache-metrics");
    ASSERT_TRUE(load_result.is_ok());
    const auto& cache = load_result.value().analysis.cache_distribution;

    EXPECT_EQ(cache.total_compilations, 12u);
    EXPECT_EQ(cache.cache_friendly_compilations, 9u);
    EXPECT_EQ(cache.cache_risk_compilations, 3u);
    EXPECT_DOUBLE_EQ(cache.cache_hit_opportunity_percent, 75.0);
    EXPECT_TRUE(cache.sccache_detected);
    EXPECT_FALSE(cache.fastbuild_detected);
    EXPECT_TRUE(cache.cache_wrapper_detected);
    EXPECT_EQ(cache.dynamic_macro_risk_count, 1u);
    EXPECT_EQ(cache.profile_or_coverage_risk_count, 1u);
    EXPECT_EQ(cache.pch_generation_risk_count, 0u);
    EXPECT_EQ(cache.volatile_path_risk_count, 2u);
    EXPECT_DOUBLE_EQ(cache.distributed_suitability_score, 61.5);
    EXPECT_EQ(cache.heavy_translation_units, 4u);
    EXPECT_EQ(cache.homogeneous_command_units, 7u);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(StorageSnapshotTest, PersistsSuggestionHotspotOrigins) {
    namespace fs = std::filesystem;

    const auto unique = std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    const fs::path root = fs::temp_directory_path() / ("bha-hotspot-origin-" + unique);

    SnapshotStore store(root);
    analyzers::AnalysisResult analysis;
    analysis.performance.total_build_time = std::chrono::milliseconds(100);

    Suggestion suggestion;
    suggestion.id = "hotspot-origin";
    suggestion.type = SuggestionType::ForwardDeclaration;
    suggestion.title = "Use forward declaration";
    suggestion.confidence = 0.9;
    suggestion.is_safe = true;

    HotspotOrigin origin;
    origin.kind = "include_chain";
    origin.source = "src/main.cpp";
    origin.target = "include/widget.hpp";
    origin.estimated_cost = std::chrono::milliseconds(12);
    origin.chain = {"src/main.cpp", "include/a.hpp", "include/widget.hpp"};
    origin.note = "Exact include chain reconstructed from source/header directives.";
    suggestion.hotspot_origins.push_back(origin);

    const auto save_result = store.save("origin-metrics", analysis, {suggestion});
    ASSERT_TRUE(save_result.is_ok());

    const auto load_result = store.load("origin-metrics");
    ASSERT_TRUE(load_result.is_ok());
    ASSERT_EQ(load_result.value().suggestions.size(), 1u);
    ASSERT_EQ(load_result.value().suggestions.front().hotspot_origins.size(), 1u);
    const auto& loaded_origin = load_result.value().suggestions.front().hotspot_origins.front();
    EXPECT_EQ(loaded_origin.kind, "include_chain");
    EXPECT_EQ(loaded_origin.chain.size(), 3u);
    EXPECT_EQ(loaded_origin.note, "Exact include chain reconstructed from source/header directives.");

    std::error_code ec;
    fs::remove_all(root, ec);
}

}  // namespace bha::storage::test
