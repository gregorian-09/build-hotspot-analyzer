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
        EXPECT_EQ(it->files_using.size(), 2u);
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

    TEST_F(TemplateAnalyzerTest, DoesNotInferBuildPercentageWithoutBuildDuration) {
        auto trace = create_test_trace();
        trace.total_time = Duration::zero();
        constexpr AnalysisOptions options;

        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_DOUBLE_EQ(result.value().templates.template_time_percent, 0.0);
        for (const auto& tmpl : result.value().templates.templates) {
            EXPECT_GT(tmpl.time_percent, 0.0);
        }
    }

    TEST_F(TemplateAnalyzerTest, RejectsUnidentifiedTemplateRows) {
        BuildTrace trace;
        CompilationUnit unit;

        TemplateInstantiation unidentified_function;
        unidentified_function.name = "InstantiateFunction";
        unidentified_function.time = std::chrono::milliseconds(900);
        unidentified_function.count = 9;

        TemplateInstantiation unidentified_class;
        unidentified_class.name = "InstantiateClass";
        unidentified_class.time = std::chrono::milliseconds(700);
        unidentified_class.count = 7;

        TemplateInstantiation identified;
        identified.name = "InstantiateClass";
        identified.full_signature = "Box<int>";
        identified.time = std::chrono::milliseconds(100);
        identified.count = 1;

        unit.templates = {unidentified_function, unidentified_class, identified};
        trace.units.push_back(std::move(unit));

        constexpr AnalysisOptions options;
        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& templates = result.value().templates;
        ASSERT_EQ(templates.templates.size(), 1u);
        EXPECT_EQ(templates.templates.front().full_signature, "Box<int>");
        EXPECT_EQ(templates.templates.front().total_time, std::chrono::milliseconds(100));
        EXPECT_EQ(templates.templates.front().instantiation_count, 1u);
        EXPECT_EQ(templates.total_template_time, std::chrono::milliseconds(100));
        EXPECT_EQ(templates.total_instantiations, 1u);
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
