//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/pch_analyzer.hpp"

#include <gtest/gtest.h>

namespace bha::analyzers
{
    class PCHAnalyzerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            analyzer_ = std::make_unique<PCHAnalyzer>();
        }

        std::unique_ptr<PCHAnalyzer> analyzer_;
    };

    TEST_F(PCHAnalyzerTest, Name) {
        EXPECT_EQ(analyzer_->name(), "PCHAnalyzer");
    }

    TEST_F(PCHAnalyzerTest, Description) {
        EXPECT_FALSE(analyzer_->description().empty());
    }

    TEST_F(PCHAnalyzerTest, AnalyzeEmptyTrace) {
        const BuildTrace trace;
        constexpr AnalysisOptions options;

        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
    }

    TEST_F(PCHAnalyzerTest, AnalyzePCHEmptyTrace) {
        const BuildTrace trace;
        constexpr AnalysisOptions options;

        auto result = PCHAnalyzer::analyze_pch(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().candidates.size(), 0u);
        EXPECT_EQ(result.value().total_headers_analyzed, 0u);
    }

    TEST_F(PCHAnalyzerTest, IdentifiesPCHCandidates) {
        BuildTrace trace;

        for (int i = 0; i < 5; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";

            IncludeInfo common_header;
            common_header.header = "common.h";
            common_header.parse_time = std::chrono::milliseconds(100);
            unit.includes.push_back(common_header);

            trace.units.push_back(unit);
        }

        AnalysisOptions options;
        options.min_duration_threshold = std::chrono::milliseconds(10);

        auto result = PCHAnalyzer::analyze_pch(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().candidates.size(), 1u);

        bool found = false;
        for (const auto& candidate : result.value().candidates) {
            if (candidate.header.filename() == "common.h") {
                found = true;
                EXPECT_EQ(candidate.inclusion_count, 5u);
                EXPECT_EQ(candidate.including_files, 5u);
                EXPECT_GT(candidate.pch_score, 0.0);
                EXPECT_GT(candidate.estimated_savings.count(), 0);
            }
        }
        EXPECT_TRUE(found);
    }

    TEST_F(PCHAnalyzerTest, SkipsRarelyIncludedHeaders) {
        BuildTrace trace;

        for (int i = 0; i < 2; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";

            IncludeInfo rare_header;
            rare_header.header = "rare.h";
            rare_header.parse_time = std::chrono::milliseconds(100);
            unit.includes.push_back(rare_header);

            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = PCHAnalyzer::analyze_pch(trace, options);

        ASSERT_TRUE(result.is_ok());
        // rare.h should not be in candidates (only 2 including files)
        for (const auto& candidate : result.value().candidates) {
            EXPECT_NE(candidate.header.filename().string(), "rare.h");
        }
    }

    TEST_F(PCHAnalyzerTest, SkipsLowParseTimeHeaders) {
        BuildTrace trace;

        for (int i = 0; i < 5; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";

            IncludeInfo fast_header;
            fast_header.header = "fast.h";
            fast_header.parse_time = std::chrono::milliseconds(1);  // Very fast
            unit.includes.push_back(fast_header);

            trace.units.push_back(unit);
        }

        AnalysisOptions options;
        options.min_duration_threshold = std::chrono::milliseconds(10);

        auto result = PCHAnalyzer::analyze_pch(trace, options);

        ASSERT_TRUE(result.is_ok());
        // fast.h should not be in candidates (below duration threshold)
        for (const auto& candidate : result.value().candidates) {
            EXPECT_NE(candidate.header.filename().string(), "fast.h");
        }
    }

    TEST_F(PCHAnalyzerTest, SortsByPCHScore) {
        BuildTrace trace;

        // High-score candidate: included by many files, high parse time
        for (int i = 0; i < 10; ++i) {
            CompilationUnit unit;
            unit.source_file = "user" + std::to_string(i) + ".cpp";

            IncludeInfo good_candidate;
            good_candidate.header = "good_candidate.h";
            good_candidate.parse_time = std::chrono::milliseconds(200);
            unit.includes.push_back(good_candidate);

            trace.units.push_back(unit);
        }

        // Lower-score candidate: fewer files, lower parse time
        for (int i = 0; i < 4; ++i) {
            CompilationUnit unit;
            unit.source_file = "other" + std::to_string(i) + ".cpp";

            IncludeInfo lesser_candidate;
            lesser_candidate.header = "lesser_candidate.h";
            lesser_candidate.parse_time = std::chrono::milliseconds(50);
            unit.includes.push_back(lesser_candidate);

            trace.units.push_back(unit);
        }

        AnalysisOptions options;
        options.min_duration_threshold = std::chrono::milliseconds(10);

        auto result = PCHAnalyzer::analyze_pch(trace, options);

        ASSERT_TRUE(result.is_ok());
        ASSERT_GE(result.value().candidates.size(), 2u);

        // Higher score should be first
        EXPECT_GE(result.value().candidates[0].pch_score,
                  result.value().candidates[1].pch_score);
    }

    TEST_F(PCHAnalyzerTest, CalculatesTotalParseTime) {
        BuildTrace trace;

        for (int i = 0; i < 5; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";

            IncludeInfo header;
            header.header = "measured.h";
            header.parse_time = std::chrono::milliseconds(100);
            unit.includes.push_back(header);

            trace.units.push_back(unit);
        }

        AnalysisOptions options;
        options.min_duration_threshold = std::chrono::milliseconds(10);

        auto result = PCHAnalyzer::analyze_pch(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().current_total_parse_time >=
                  std::chrono::milliseconds(500));
    }

    TEST_F(PCHAnalyzerTest, CalculatesEstimatedSavings) {
        BuildTrace trace;

        for (int i = 0; i < 5; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";

            IncludeInfo header;
            header.header = "savings.h";
            header.parse_time = std::chrono::milliseconds(100);
            unit.includes.push_back(header);

            trace.units.push_back(unit);
        }

        AnalysisOptions options;
        options.min_duration_threshold = std::chrono::milliseconds(10);

        auto result = PCHAnalyzer::analyze_pch(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GT(result.value().potential_savings.count(), 0);

        // Savings should be approximately (N-1) * per_parse_time
        // 5 inclusions @ 100ms each, savings = 4 * 100ms = 400ms expected
        EXPECT_TRUE(result.value().potential_savings >= std::chrono::milliseconds(300));
    }

    TEST_F(PCHAnalyzerTest, CountsMultipleInclusionsPerFile) {
        BuildTrace trace;

        // Single file includes the same header multiple times (through different paths)
        for (int i = 0; i < 3; ++i) {
            CompilationUnit unit;
            unit.source_file = "file" + std::to_string(i) + ".cpp";

            // Each file includes the header twice
            for (int j = 0; j < 2; ++j) {
                IncludeInfo header;
                header.header = "multi.h";
                header.parse_time = std::chrono::milliseconds(50);
                unit.includes.push_back(header);
            }

            trace.units.push_back(unit);
        }

        AnalysisOptions options;
        options.min_duration_threshold = std::chrono::milliseconds(10);

        auto result = PCHAnalyzer::analyze_pch(trace, options);

        ASSERT_TRUE(result.is_ok());
        ASSERT_GE(result.value().candidates.size(), 1u);

        // Find multi.h
        for (const auto& candidate : result.value().candidates) {
            if (candidate.header.filename() == "multi.h") {
                EXPECT_EQ(candidate.inclusion_count, 6u);  // 3 files * 2 times
                EXPECT_EQ(candidate.including_files, 3u);  // Only 3 unique files
            }
        }
    }
}