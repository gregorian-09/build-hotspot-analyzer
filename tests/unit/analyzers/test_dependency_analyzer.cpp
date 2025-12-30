//
// Created by gregorian-rayne on 12/30/25.
//


#include "bha/analyzers/dependency_analyzer.hpp"

#include <gtest/gtest.h>

namespace bha::analyzers
{
    class DependencyAnalyzerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            analyzer_ = std::make_unique<DependencyAnalyzer>();
        }

        static BuildTrace create_test_trace() {
            BuildTrace trace;
            trace.id = "test-trace";

            CompilationUnit unit1;
            unit1.source_file = "/src/main.cpp";
            unit1.includes = {
                {"/include/header.h", std::chrono::milliseconds(100), 1, {}, {}},
                {"/include/utils.h", std::chrono::milliseconds(50), 1, {}, {}},
            };

            CompilationUnit unit2;
            unit2.source_file = "/src/other.cpp";
            unit2.includes = {
                {"/include/header.h", std::chrono::milliseconds(100), 1, {}, {}},
                {"/include/common.h", std::chrono::milliseconds(80), 2, {}, {}},
            };

            trace.units = {unit1, unit2};
            return trace;
        }

        std::unique_ptr<DependencyAnalyzer> analyzer_;
    };

    TEST_F(DependencyAnalyzerTest, Name) {
        EXPECT_EQ(analyzer_->name(), "DependencyAnalyzer");
    }

    TEST_F(DependencyAnalyzerTest, AnalyzeEmptyTrace) {
        const BuildTrace empty_trace;
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(empty_trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().dependencies.headers.empty());
    }

    TEST_F(DependencyAnalyzerTest, AnalyzeBasicTrace) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& deps = result.value().dependencies;

        EXPECT_EQ(deps.unique_headers, 3u);
        EXPECT_EQ(deps.total_includes, 4u);
    }

    TEST_F(DependencyAnalyzerTest, HeaderIncludedMultipleTimes) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        auto& headers = result.value().dependencies.headers;

        const auto it = std::ranges::find_if(headers,
                                             [](const auto& h) {
                                                 return h.path.filename() == "header.h";
                                             });

        ASSERT_NE(it, headers.end());
        EXPECT_EQ(it->inclusion_count, 2u);
        EXPECT_EQ(it->including_files, 2u);
    }

    TEST_F(DependencyAnalyzerTest, HeadersSortedByImpact) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& headers = result.value().dependencies.headers;

        for (std::size_t i = 1; i < headers.size(); ++i) {
            EXPECT_GE(headers[i - 1].impact_score, headers[i].impact_score);
        }
    }
}