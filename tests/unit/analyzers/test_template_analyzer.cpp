//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/template_analyzer.hpp"

#include <gtest/gtest.h>

namespace bha::analyzers {

    class TemplateAnalyzerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            analyzer_ = std::make_unique<TemplateAnalyzer>();
        }

        static BuildTrace create_test_trace() {
            BuildTrace trace;
            trace.id = "test-trace";
            trace.total_time = std::chrono::seconds(10);

            CompilationUnit unit1;
            unit1.source_file = "/src/main.cpp";
            unit1.metrics.total_time = std::chrono::seconds(5);
            unit1.templates = {
                {"InstantiateClass", "std::vector<int>", {"int"}, std::chrono::milliseconds(500), {}, 2},
                {"InstantiateClass", "std::map<std::string, int>", {"std::string", "int"}, std::chrono::milliseconds(800), {}, 1},
            };

            CompilationUnit unit2;
            unit2.source_file = "/src/other.cpp";
            unit2.metrics.total_time = std::chrono::seconds(3);
            unit2.templates = {
                {"InstantiateClass", "std::vector<int>", {"int"}, std::chrono::milliseconds(400), {}, 3},
            };

            trace.units = {unit1, unit2};
            return trace;
        }

        std::unique_ptr<TemplateAnalyzer> analyzer_;
    };

    TEST_F(TemplateAnalyzerTest, Name) {
        EXPECT_EQ(analyzer_->name(), "TemplateAnalyzer");
    }

    TEST_F(TemplateAnalyzerTest, AnalyzeEmptyTrace) {
        const BuildTrace empty_trace;
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(empty_trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().templates.templates.empty());
    }

    TEST_F(TemplateAnalyzerTest, AnalyzeBasicTrace) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& tmpl = result.value().templates;

        EXPECT_EQ(tmpl.templates.size(), 2u);
        EXPECT_GT(tmpl.total_template_time.count(), 0);
        EXPECT_GT(tmpl.total_instantiations, 0u);
    }

    TEST_F(TemplateAnalyzerTest, TemplatesAggregated) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        auto& templates = result.value().templates.templates;

        const auto it = std::ranges::find_if(templates,
                                             [](const auto& t) {
                                                 return t.full_signature == "std::vector<int>";
                                             });

        ASSERT_NE(it, templates.end());
        EXPECT_EQ(it->instantiation_count, 5u);
        EXPECT_TRUE(it->total_time == std::chrono::milliseconds(900));
    }

    TEST_F(TemplateAnalyzerTest, TemplatesSortedByTime) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& templates = result.value().templates.templates;

        for (std::size_t i = 1; i < templates.size(); ++i) {
            EXPECT_TRUE(templates[i - 1].total_time >= templates[i].total_time);
        }
    }

    TEST_F(TemplateAnalyzerTest, SkipsWhenDisabled) {
        const auto trace = create_test_trace();
        AnalysisOptions options;
        options.analyze_templates = false;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().templates.templates.empty());
    }
}