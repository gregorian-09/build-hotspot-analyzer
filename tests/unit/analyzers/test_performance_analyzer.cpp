//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/performance_analyzer.hpp"

#include <gtest/gtest.h>

namespace bha::analyzers {

    class PerformanceAnalyzerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            analyzer_ = std::make_unique<PerformanceAnalyzer>();
        }

        std::unique_ptr<PerformanceAnalyzer> analyzer_;
    };

    TEST_F(PerformanceAnalyzerTest, Name) {
        EXPECT_EQ(analyzer_->name(), "PerformanceAnalyzer");
    }

    TEST_F(PerformanceAnalyzerTest, Description) {
        EXPECT_FALSE(analyzer_->description().empty());
    }

    TEST_F(PerformanceAnalyzerTest, AnalyzeEmptyTrace) {
        const BuildTrace trace;
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().performance.total_files, 0u);
    }

    TEST_F(PerformanceAnalyzerTest, CalculatesTotalBuildTime) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(60);

        CompilationUnit unit;
        unit.source_file = "test.cpp";
        unit.metrics.total_time = std::chrono::seconds(30);
        trace.units.push_back(unit);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().performance.total_build_time == std::chrono::seconds(60));
        EXPECT_EQ(result.value().performance.total_files, 1u);
    }

    TEST_F(PerformanceAnalyzerTest, CalculatesSequentialTime) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(30);

        for (int i = 0; i < 3; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::seconds(20);  // 20s each
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        // Sequential time = 3 * 20s = 60s
        EXPECT_TRUE(result.value().performance.sequential_time == std::chrono::seconds(60));
        EXPECT_TRUE(result.value().performance.parallel_time == std::chrono::seconds(30));
    }

    TEST_F(PerformanceAnalyzerTest, CalculatesParallelismEfficiency) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(30);

        for (int i = 0; i < 3; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::seconds(20);
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        // Efficiency = sequential (60s) / parallel (30s) = 2.0
        EXPECT_DOUBLE_EQ(result.value().performance.parallelism_efficiency, 2.0);
    }

    TEST_F(PerformanceAnalyzerTest, CalculatesAverageFileTime) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(100);

        const std::vector times_ms = {100, 200, 300, 400, 500};
        for (std::size_t i = 0; i < times_ms.size(); ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::milliseconds(times_ms[i]);
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        // Average = (100+200+300+400+500) / 5 = 300ms
        EXPECT_TRUE(result.value().performance.avg_file_time == std::chrono::milliseconds(300));
    }

    TEST_F(PerformanceAnalyzerTest, CalculatesMedianFileTime) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(100);

        const std::vector times_ms = {100, 200, 300, 400, 500};
        for (std::size_t i = 0; i < times_ms.size(); ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::milliseconds(times_ms[i]);
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        // Median of [100, 200, 300, 400, 500] = 300ms
        EXPECT_TRUE(result.value().performance.median_file_time == std::chrono::milliseconds(300));
    }

    TEST_F(PerformanceAnalyzerTest, CalculatesP90FileTime) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(100);

        for (int i = 0; i < 10; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::milliseconds((i + 1) * 100);
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().performance.p90_file_time >= std::chrono::milliseconds(800));
    }

    TEST_F(PerformanceAnalyzerTest, IdentifiesSlowestFiles) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(100);

        for (int i = 0; i < 10; ++i) {
            CompilationUnit unit;
            unit.source_file = "fast" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::milliseconds(5);
            trace.units.push_back(unit);
        }

        for (int i = 0; i < 3; ++i) {
            CompilationUnit unit;
            unit.source_file = "slow" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::milliseconds(500);
            trace.units.push_back(unit);
        }

        AnalysisOptions options;
        options.min_duration_threshold = std::chrono::milliseconds(100);

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().performance.slowest_file_count, 3u);
        EXPECT_GE(result.value().performance.slowest_files.size(), 3u);
    }

    TEST_F(PerformanceAnalyzerTest, SortsFilesByCompileTime) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(100);

        // Files in random order
        const std::vector times_ms = {300, 100, 500, 200, 400};
        for (std::size_t i = 0; i < times_ms.size(); ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::milliseconds(times_ms[i]);
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().files.size(), 5u);

        for (std::size_t i = 1; i < result.value().files.size(); ++i) {
            EXPECT_TRUE(result.value().files[i - 1].compile_time >=
                      result.value().files[i].compile_time);
        }
    }

    TEST_F(PerformanceAnalyzerTest, AssignsRanks) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(100);

        for (int i = 0; i < 5; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::milliseconds(i * 100);
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());

        // Ranks should be sequential
        for (std::size_t i = 0; i < result.value().files.size(); ++i) {
            EXPECT_EQ(result.value().files[i].rank, i + 1);
        }
    }

    TEST_F(PerformanceAnalyzerTest, CalculatesTimePercentages) {
        BuildTrace trace;
        trace.total_time = std::chrono::milliseconds(1000);

        CompilationUnit unit;
        unit.source_file = "test.cpp";
        unit.metrics.total_time = std::chrono::milliseconds(500);  // 50%
        trace.units.push_back(unit);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().files.size(), 1u);
        EXPECT_DOUBLE_EQ(result.value().files[0].time_percent, 50.0);
    }

    TEST_F(PerformanceAnalyzerTest, IdentifiesCriticalPath) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(10);

        CompilationUnit slow;
        slow.source_file = "slowest.cpp";
        slow.metrics.total_time = std::chrono::seconds(5);
        trace.units.push_back(slow);

        CompilationUnit fast;
        fast.source_file = "fast.cpp";
        fast.metrics.total_time = std::chrono::seconds(1);
        trace.units.push_back(fast);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().performance.critical_path.empty());
        // Critical path should include the slowest file
        EXPECT_EQ(result.value().performance.critical_path[0].filename(), "slowest.cpp");
    }

    TEST_F(PerformanceAnalyzerTest, ComputesCacheAndDistributionMetrics) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(3);

        CompilationUnit a;
        a.source_file = "a.cpp";
        a.metrics.total_time = std::chrono::milliseconds(1200);
        a.metrics.frontend_time = std::chrono::milliseconds(800);
        a.command_line = {"sccache", "clang++", "-c", "a.cpp", "-o", "a.o"};
        trace.units.push_back(a);

        CompilationUnit b;
        b.source_file = "b.cpp";
        b.metrics.total_time = std::chrono::milliseconds(900);
        b.metrics.frontend_time = std::chrono::milliseconds(700);
        b.command_line = {"clang++", "-DVERSION=__DATE__", "-c", "b.cpp", "-o", "b.o"};
        trace.units.push_back(b);

        CompilationUnit c;
        c.source_file = "c.cpp";
        c.metrics.total_time = std::chrono::milliseconds(600);
        c.metrics.frontend_time = std::chrono::milliseconds(500);
        c.command_line = {"fastbuild", "clang++", "--coverage", "-c", "c.cpp", "-o", "c.o"};
        trace.units.push_back(c);

        constexpr AnalysisOptions options;
        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& cache = result.value().cache_distribution;
        EXPECT_EQ(cache.total_compilations, 3u);
        EXPECT_EQ(cache.cache_friendly_compilations, 1u);
        EXPECT_EQ(cache.cache_risk_compilations, 2u);
        EXPECT_NEAR(cache.cache_hit_opportunity_percent, 33.3, 0.2);
        EXPECT_TRUE(cache.sccache_detected);
        EXPECT_TRUE(cache.fastbuild_detected);
        EXPECT_TRUE(cache.cache_wrapper_detected);
        EXPECT_EQ(cache.dynamic_macro_risk_count, 1u);
        EXPECT_EQ(cache.profile_or_coverage_risk_count, 1u);
        EXPECT_EQ(cache.pch_generation_risk_count, 0u);
        EXPECT_GE(cache.distributed_suitability_score, 0.0);
        EXPECT_LE(cache.distributed_suitability_score, 100.0);
        EXPECT_EQ(cache.heavy_translation_units, 2u);
        EXPECT_GE(cache.homogeneous_command_units, 1u);
    }

    TEST_F(PerformanceAnalyzerTest, DetectsHomogeneousCompileCommands) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(1);

        for (int i = 0; i < 4; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";
            unit.metrics.total_time = std::chrono::milliseconds(100 + i);
            unit.metrics.frontend_time = std::chrono::milliseconds(70 + i);
            unit.command_line = {
                "clang++", "-Iinclude", "-O2", "-c",
                unit.source_file.string(),
                "-o", "build/file" + std::to_string(i) + ".o"
            };
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().cache_distribution.homogeneous_command_units, 4u);
    }
}
