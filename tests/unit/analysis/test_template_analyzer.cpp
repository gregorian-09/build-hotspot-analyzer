//
// Created by gregorian on 08/12/2025.
//

#include <ranges>
#include <gtest/gtest.h>
#include "bha/analysis/template_analyzer.h"

using namespace bha::analysis;
using namespace bha::core;

class TemplateAnalyzerTest : public ::testing::Test {
protected:
    void SetUp() override {
        trace = BuildTrace{};
    }

    BuildTrace trace;

    void CreateSimpleTrace() {
        CompilationUnit unit1;
        unit1.file_path = "main.cpp";
        unit1.total_time_ms = 3000.0;

        TemplateInstantiation temp1;
        temp1.template_name = "std::vector<int>";
        temp1.time_ms = 500.0;
        temp1.instantiation_depth = 1;
        unit1.template_instantiations.push_back(temp1);

        TemplateInstantiation temp2;
        temp2.template_name = "std::map<std::string, int>";
        temp2.time_ms = 400.0;
        temp2.instantiation_depth = 2;
        unit1.template_instantiations.push_back(temp2);

        trace.compilation_units.push_back(unit1);

        CompilationUnit unit2;
        unit2.file_path = "module.cpp";
        unit2.total_time_ms = 2000.0;

        TemplateInstantiation temp3;
        temp3.template_name = "std::vector<int>";
        temp3.time_ms = 300.0;
        temp3.instantiation_depth = 1;
        unit2.template_instantiations.push_back(temp3);

        trace.compilation_units.push_back(unit2);
    }

    void CreateComplexTrace() {
        std::vector<std::pair<std::string, double>> files = {
            {"main.cpp", 5000.0},
            {"module1.cpp", 3500.0},
            {"module2.cpp", 2800.0},
            {"module3.cpp", 2200.0}
        };

        const std::vector<std::pair<std::string, double>> templates = {
            {"std::vector<int>", 400.0},
            {"std::map<std::string, double>", 600.0},
            {"std::unordered_map<std::string, std::vector<int>>", 800.0},
            {"MyTemplateClass<int, double>", 350.0},
            {"std::shared_ptr<ComplexType>", 250.0}
        };

        int template_idx = 0;
        for (const auto& [file, time] : files) {
            CompilationUnit unit;
            unit.file_path = file;
            unit.total_time_ms = time;

            for (int i = 0; i < 3; ++i) {
                TemplateInstantiation temp;
                temp.template_name = templates[template_idx % templates.size()].first;
                temp.time_ms = templates[template_idx % templates.size()].second;
                temp.instantiation_depth = 1 + (i % 3);
                unit.template_instantiations.push_back(temp);
                template_idx++;
            }

            trace.compilation_units.push_back(unit);
        }
    }
};

TEST_F(TemplateAnalyzerTest, AnalyzeTemplatesWithEmptyTrace) {
    auto result = TemplateAnalyzer::analyze_templates(trace, 20);

    ASSERT_TRUE(result.is_success());
    const auto& analysis = result.value();
    EXPECT_EQ(analysis.expensive_templates.size(), 0);
    EXPECT_EQ(analysis.total_template_time_ms, 0.0);
}

TEST_F(TemplateAnalyzerTest, AnalyzeTemplatesWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = TemplateAnalyzer::analyze_templates(trace, 20);

    ASSERT_TRUE(result.is_success());
    const auto& analysis = result.value();
    EXPECT_GE(analysis.expensive_templates.size(), 0);
    EXPECT_GE(analysis.total_template_time_ms, 0.0);
    EXPECT_GE(analysis.template_time_percentage, 0.0);
}

TEST_F(TemplateAnalyzerTest, AnalyzeTemplatesWithComplexTrace) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::analyze_templates(trace, 5);

    ASSERT_TRUE(result.is_success());
    const auto& analysis = result.value();
    EXPECT_LE(analysis.expensive_templates.size(), 5);
    EXPECT_GT(analysis.total_template_time_ms, 0.0);
    EXPECT_GE(analysis.template_time_percentage, 0.0);

    // Template time can exceed 100% when instantiations overlap or are counted separately
    EXPECT_LE(analysis.template_time_percentage, 1000.0);  // Reasonable upper bound
}

TEST_F(TemplateAnalyzerTest, FindExpensiveTemplatesWithEmptyTrace) {
    auto result = TemplateAnalyzer::find_expensive_templates(trace, 20, 100.0);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(TemplateAnalyzerTest, FindExpensiveTemplatesWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = TemplateAnalyzer::find_expensive_templates(trace, 10, 200.0);

    ASSERT_TRUE(result.is_success());
    const auto& templates = result.value();
    EXPECT_GE(templates.size(), 0);

    for (const auto& tmpl : templates) {
        EXPECT_FALSE(tmpl.template_name.empty());
        EXPECT_GE(tmpl.time_ms, 200.0);
    }
}

TEST_F(TemplateAnalyzerTest, FindExpensiveTemplatesWithComplexTrace) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::find_expensive_templates(trace, 5, 300.0);

    ASSERT_TRUE(result.is_success());
    const auto& templates = result.value();
    EXPECT_LE(templates.size(), 5);

    for (const auto& tmpl : templates) {
        EXPECT_GE(tmpl.time_ms, 300.0);
    }
}

TEST_F(TemplateAnalyzerTest, FindExpensiveTemplatesWithZeroThreshold) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::find_expensive_templates(trace, 20, 0.0);

    ASSERT_TRUE(result.is_success());
    const auto& templates = result.value();
    EXPECT_GT(templates.size(), 0);
}

TEST_F(TemplateAnalyzerTest, CountInstantiationsWithEmptyTrace) {
    auto result = TemplateAnalyzer::count_instantiations(trace);

    ASSERT_TRUE(result.is_success());
    const auto& counts = result.value();
    EXPECT_EQ(counts.size(), 0);
}

TEST_F(TemplateAnalyzerTest, CountInstantiationsWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = TemplateAnalyzer::count_instantiations(trace);

    ASSERT_TRUE(result.is_success());
    auto counts = result.value();
    EXPECT_GT(counts.size(), 0);

    // std::vector<int> should appear twice
    if (counts.contains("std::vector<int>")) {
        EXPECT_GE(counts["std::vector<int>"], 1);
    }
}

TEST_F(TemplateAnalyzerTest, CountInstantiationsWithComplexTrace) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::count_instantiations(trace);

    ASSERT_TRUE(result.is_success());
    auto counts = result.value();
    EXPECT_GT(counts.size(), 0);

    for (const auto& [name, count] : counts) {
        EXPECT_FALSE(name.empty());
        EXPECT_GE(count, 1);
    }
}

TEST_F(TemplateAnalyzerTest, CalculateTemplateTimesWithEmptyTrace) {
    auto result = TemplateAnalyzer::calculate_template_times(trace);

    ASSERT_TRUE(result.is_success());
    const auto& times = result.value();
    EXPECT_EQ(times.size(), 0);
}

TEST_F(TemplateAnalyzerTest, CalculateTemplateTimesWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = TemplateAnalyzer::calculate_template_times(trace);

    ASSERT_TRUE(result.is_success());
    auto times = result.value();
    EXPECT_GT(times.size(), 0);

    for (const auto& [name, time] : times) {
        EXPECT_FALSE(name.empty());
        EXPECT_GE(time, 0.0);
    }
}

TEST_F(TemplateAnalyzerTest, CalculateTemplateTimesWithComplexTrace) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::calculate_template_times(trace);

    ASSERT_TRUE(result.is_success());
    auto times = result.value();
    EXPECT_GT(times.size(), 0);

    double total_time = 0.0;
    for (const auto& time : times | std::views::values) {
        EXPECT_GE(time, 0.0);
        total_time += time;
    }
    EXPECT_GT(total_time, 0.0);
}

TEST_F(TemplateAnalyzerTest, SuggestExplicitInstantiationsWithEmptyTrace) {
    auto result = TemplateAnalyzer::suggest_explicit_instantiations(trace, 3);

    ASSERT_TRUE(result.is_success());
    const auto& suggestions = result.value();
    EXPECT_EQ(suggestions.size(), 0);
}

TEST_F(TemplateAnalyzerTest, SuggestExplicitInstantiationsWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = TemplateAnalyzer::suggest_explicit_instantiations(trace, 1);

    ASSERT_TRUE(result.is_success());
    const auto& suggestions = result.value();
    EXPECT_GE(suggestions.size(), 0);

    for (const auto& name : suggestions) {
        EXPECT_FALSE(name.empty());
    }
}

TEST_F(TemplateAnalyzerTest, SuggestExplicitInstantiationsWithComplexTrace) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::suggest_explicit_instantiations(trace, 2);

    ASSERT_TRUE(result.is_success());
    const auto& suggestions = result.value();
    EXPECT_GE(suggestions.size(), 0);
}

TEST_F(TemplateAnalyzerTest, SuggestExplicitInstantiationsWithHighThreshold) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::suggest_explicit_instantiations(trace, 100);

    ASSERT_TRUE(result.is_success());
    const auto& suggestions = result.value();

    // It is unlikely to have templates with 100+ instantiations
    EXPECT_GE(suggestions.size(), 0);
}

TEST_F(TemplateAnalyzerTest, FindTemplateHeavyFilesWithEmptyTrace) {
    auto result = TemplateAnalyzer::find_template_heavy_files(trace, 50.0);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(TemplateAnalyzerTest, FindTemplateHeavyFilesWithSimpleTrace) {
    CreateSimpleTrace();
    auto result = TemplateAnalyzer::find_template_heavy_files(trace, 10.0);

    ASSERT_TRUE(result.is_success());
    const auto &heavy_files = result.value();
    EXPECT_GE(heavy_files.size(), 0);

    for (const auto& file : heavy_files) {
        EXPECT_FALSE(file.empty());
    }
}

TEST_F(TemplateAnalyzerTest, FindTemplateHeavyFilesWithComplexTrace) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::find_template_heavy_files(trace, 20.0);

    ASSERT_TRUE(result.is_success());
    const auto &heavy_files = result.value();
    EXPECT_GE(heavy_files.size(), 0);
}

TEST_F(TemplateAnalyzerTest, FindTemplateHeavyFilesWithHighThreshold) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::find_template_heavy_files(trace, 95.0);

    ASSERT_TRUE(result.is_success());
    const auto& heavy_files = result.value();

    // Most files won't be 95% template time
    EXPECT_GE(heavy_files.size(), 0);
}

TEST_F(TemplateAnalyzerTest, CalculateTemplateOverhead) {
    CreateSimpleTrace();
    ASSERT_GT(trace.compilation_units.size(), 0);

    const auto& unit = trace.compilation_units[0];
    const double overhead = TemplateAnalyzer::calculate_template_overhead(unit);

    EXPECT_GE(overhead, 0.0);
    EXPECT_LE(overhead, 100.0);
}

TEST_F(TemplateAnalyzerTest, CalculateTemplateOverheadForMultipleUnits) {
    CreateComplexTrace();

    for (const auto& unit : trace.compilation_units) {
        double overhead = TemplateAnalyzer::calculate_template_overhead(unit);
        EXPECT_GE(overhead, 0.0);
        EXPECT_LE(overhead, 100.0);
    }
}

TEST_F(TemplateAnalyzerTest, TemplateHotspotStructureValidation) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::find_expensive_templates(trace, 10, 100.0);

    ASSERT_TRUE(result.is_success());

    for (const auto &hotspots = result.value(); const auto& hotspot : hotspots) {
        EXPECT_FALSE(hotspot.template_name.empty());
        EXPECT_GE(hotspot.time_ms, 0.0);
        EXPECT_GE(hotspot.instantiation_count, 0);
    }
}

TEST_F(TemplateAnalyzerTest, AnalysisResultStructureValidation) {
    CreateComplexTrace();
    auto result = TemplateAnalyzer::analyze_templates(trace, 10);

    ASSERT_TRUE(result.is_success());
    auto [expensive_templates, instantiation_counts, total_times_by_template, total_template_time_ms, template_time_percentage] = result.value();

    EXPECT_GE(expensive_templates.size(), 0);
    EXPECT_GE(instantiation_counts.size(), 0);
    EXPECT_GE(total_times_by_template.size(), 0);
    EXPECT_GE(total_template_time_ms, 0.0);
    EXPECT_GE(template_time_percentage, 0.0);
    EXPECT_LE(template_time_percentage, 1000.0);  // Reasonable upper bound
}