//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/pimpl_suggester.hpp"

#include <gtest/gtest.h>

namespace bha::suggestions
{
    class PIMPLSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<PIMPLSuggester>();
        }

        std::unique_ptr<PIMPLSuggester> suggester_;
    };

    TEST_F(PIMPLSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "PIMPLSuggester");
    }

    TEST_F(PIMPLSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(PIMPLSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::PIMPLPattern);
    }

    TEST_F(PIMPLSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(PIMPLSuggesterTest, SuggestsForSlowSourceWithManyIncludes) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(120);

        analyzers::AnalysisResult analysis;

        analyzers::FileAnalysisResult file;
        file.file = "widget.cpp";
        file.compile_time = std::chrono::milliseconds(2000);
        analysis.files.push_back(file);

        for (int i = 0; i < 8; ++i) {
            analyzers::DependencyAnalysisResult::HeaderInfo header;
            header.path = "dep" + std::to_string(i) + ".h";
            header.total_parse_time = std::chrono::milliseconds(100);
            header.included_by = {"widget.cpp"};
            analysis.dependencies.headers.push_back(header);
        }

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().suggestions.size(), 1u);

        if (!result.value().suggestions.empty()) {
            const auto& suggestion = result.value().suggestions[0];
            EXPECT_EQ(suggestion.type, SuggestionType::PIMPLPattern);
            EXPECT_FALSE(suggestion.is_safe);
            EXPECT_GT(suggestion.estimated_savings.count(), 0);
            EXPECT_FALSE(suggestion.implementation_steps.empty());
            EXPECT_FALSE(suggestion.caveats.empty());
        }
    }

    TEST_F(PIMPLSuggesterTest, SkipsHeaderFiles) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;

        analyzers::FileAnalysisResult file;
        file.file = "widget.h";
        file.compile_time = std::chrono::milliseconds(5000);
        analysis.files.push_back(file);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(PIMPLSuggesterTest, SkipsFastCompiles) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;

        analyzers::FileAnalysisResult file;
        file.file = "fast.cpp";
        file.compile_time = std::chrono::milliseconds(100);
        analysis.files.push_back(file);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(PIMPLSuggesterTest, SkipsFilesWithFewIncludes) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;

        // Add a slow source file with few includes
        analyzers::FileAnalysisResult file;
        file.file = "isolated.cpp";
        file.compile_time = std::chrono::milliseconds(5000);
        analysis.files.push_back(file);

        for (int i = 0; i < 2; ++i) {
            analyzers::DependencyAnalysisResult::HeaderInfo header;
            header.path = "dep" + std::to_string(i) + ".h";
            header.included_by = {"isolated.cpp"};
            analysis.dependencies.headers.push_back(header);
        }

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(PIMPLSuggesterTest, SkipsExistingImplFiles) {
        BuildTrace trace;

        analyzers::AnalysisResult analysis;

        analyzers::FileAnalysisResult file;
        file.file = "widget_impl.cpp";
        file.compile_time = std::chrono::milliseconds(5000);
        analysis.files.push_back(file);

        for (int i = 0; i < 10; ++i) {
            analyzers::DependencyAnalysisResult::HeaderInfo header;
            header.path = "dep" + std::to_string(i) + ".h";
            header.included_by = {"widget_impl.cpp"};
            analysis.dependencies.headers.push_back(header);
        }

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(PIMPLSuggesterTest, CalculatesCorrectPriority) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(300);

        analyzers::AnalysisResult analysis;

        // Critical: > 5000ms, >= 20 includes
        analyzers::FileAnalysisResult critical_file;
        critical_file.file = "critical.cpp";
        critical_file.compile_time = std::chrono::milliseconds(6000);
        analysis.files.push_back(critical_file);

        // High: > 2000ms, >= 10 includes
        analyzers::FileAnalysisResult high_file;
        high_file.file = "high.cpp";
        high_file.compile_time = std::chrono::milliseconds(3000);
        analysis.files.push_back(high_file);

        for (int i = 0; i < 25; ++i) {
            analyzers::DependencyAnalysisResult::HeaderInfo header;
            header.path = "critical_dep" + std::to_string(i) + ".h";
            header.included_by = {"critical.cpp"};
            analysis.dependencies.headers.push_back(header);
        }

        for (int i = 0; i < 12; ++i) {
            analyzers::DependencyAnalysisResult::HeaderInfo header;
            header.path = "high_dep" + std::to_string(i) + ".h";
            header.included_by = {"high.cpp"};
            analysis.dependencies.headers.push_back(header);
        }

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_GE(result.value().suggestions.size(), 2u);

        std::unordered_map<std::string, Priority> priorities;
        for (const auto& s : result.value().suggestions) {
            if (s.id.find("critical.cpp") != std::string::npos) {
                priorities["critical"] = s.priority;
            } else if (s.id.find("high.cpp") != std::string::npos) {
                priorities["high"] = s.priority;
            }
        }

        EXPECT_EQ(priorities["critical"], Priority::Critical);
        EXPECT_EQ(priorities["high"], Priority::High);
    }

    TEST_F(PIMPLSuggesterTest, SortsByEstimatedSavings) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(120);

        analyzers::AnalysisResult analysis;

        analyzers::FileAnalysisResult small_file;
        small_file.file = "small.cpp";
        small_file.compile_time = std::chrono::milliseconds(1500);
        analysis.files.push_back(small_file);

        analyzers::FileAnalysisResult big_file;
        big_file.file = "big.cpp";
        big_file.compile_time = std::chrono::milliseconds(5000);
        analysis.files.push_back(big_file);

        for (int i = 0; i < 10; ++i) {
            analyzers::DependencyAnalysisResult::HeaderInfo header1;
            header1.path = "small_dep" + std::to_string(i) + ".h";
            header1.included_by = {"small.cpp"};
            analysis.dependencies.headers.push_back(header1);

            analyzers::DependencyAnalysisResult::HeaderInfo header2;
            header2.path = "big_dep" + std::to_string(i) + ".h";
            header2.included_by = {"big.cpp"};
            analysis.dependencies.headers.push_back(header2);
        }

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_GE(result.value().suggestions.size(), 2u);

        EXPECT_TRUE(result.value().suggestions[0].estimated_savings >=
                  result.value().suggestions[1].estimated_savings);
    }
}