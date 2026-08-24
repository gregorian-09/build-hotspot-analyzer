//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/file_analyzer.hpp"

#include <gtest/gtest.h>

namespace bha::analyzers
{
    class FileAnalyzerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            analyzer_ = std::make_unique<FileAnalyzer>();
        }

        static BuildTrace create_test_trace() {
            BuildTrace trace;
            trace.id = "test-trace";
            trace.total_time = std::chrono::seconds(10);

            CompilationUnit unit1;
            unit1.source_file = "/src/main.cpp";
            unit1.metrics.total_time = std::chrono::seconds(5);
            unit1.metrics.frontend_time = std::chrono::seconds(3);
            unit1.metrics.backend_time = std::chrono::seconds(2);

            CompilationUnit unit2;
            unit2.source_file = "/src/utils.cpp";
            unit2.metrics.total_time = std::chrono::seconds(3);

            CompilationUnit unit3;
            unit3.source_file = "/src/helper.cpp";
            unit3.metrics.total_time = std::chrono::seconds(2);

            trace.units = {unit1, unit2, unit3};
            return trace;
        }

        std::unique_ptr<FileAnalyzer> analyzer_;
    };

    TEST_F(FileAnalyzerTest, Name) {
        EXPECT_EQ(analyzer_->name(), "FileAnalyzer");
    }

    TEST_F(FileAnalyzerTest, Description) {
        EXPECT_FALSE(analyzer_->description().empty());
    }

    TEST_F(FileAnalyzerTest, AnalyzeEmptyTrace) {
        const BuildTrace empty_trace;
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(empty_trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().files.empty());
    }

    TEST_F(FileAnalyzerTest, AnalyzeBasicTrace) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value();

        EXPECT_EQ(analysis.files.size(), 3u);
        EXPECT_EQ(analysis.files[0].file, fs::path("/src/main.cpp"));
        EXPECT_EQ(analysis.files[0].rank, 1u);
    }

    TEST_F(FileAnalyzerTest, FilesSortedByTime) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& files = result.value().files;

        for (std::size_t i = 1; i < files.size(); ++i) {
            EXPECT_TRUE(files[i - 1].compile_time >= files[i].compile_time);
        }
    }

    TEST_F(FileAnalyzerTest, LeavesAggregatePerformanceToDedicatedAnalyzer) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& perf = result.value().performance;

        EXPECT_EQ(perf.total_files, 0u);
        EXPECT_EQ(perf.avg_file_time, Duration::zero());
        EXPECT_TRUE(perf.slowest_files.empty());
    }

    TEST_F(FileAnalyzerTest, RespectsMinDurationThreshold) {
        const auto trace = create_test_trace();
        AnalysisOptions options;
        options.min_duration_threshold = std::chrono::seconds(4);

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().files.size(), 1u);
        EXPECT_EQ(result.value().files.front().file, fs::path("/src/main.cpp"));
    }

    TEST_F(FileAnalyzerTest, DoesNotInferPercentagesWithoutBuildDuration) {
        auto trace = create_test_trace();
        trace.total_time = Duration::zero();
        constexpr AnalysisOptions options;

        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().files.size(), 3u);
        for (const auto& file : result.value().files) {
            EXPECT_DOUBLE_EQ(file.time_percent, 0.0);
        }
    }

    TEST_F(FileAnalyzerTest, RejectsNegativeCompilationTiming) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(1);
        CompilationUnit unit;
        unit.metrics.frontend_time = Duration(-1);
        trace.units.push_back(std::move(unit));

        constexpr AnalysisOptions options;
        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code(), ErrorCode::AnalysisError);
    }
}
