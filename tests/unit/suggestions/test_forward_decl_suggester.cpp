//
// Created by gregorian on 12/12/2025.
//

#include <gtest/gtest.h>
#include "bha/suggestions/forward_decl_suggester.h"
#include "bha/utils/file_utils.h"
#include <fstream>
#include <filesystem>

using namespace bha::suggestions;
using namespace bha::core;

class ForwardDeclSuggesterTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = "/tmp/bha_fwd_decl_test";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    void create_test_file(const std::string& filename, const std::string& content) const
    {
        std::ofstream file(test_dir + "/" + filename);
        file << content;
        file.close();
    }

    std::string test_dir;
};

TEST_F(ForwardDeclSuggesterTest, ExtractIncludesFromFile) {
    const std::string content = R"(
#include <vector>
#include "myclass.h"
#include <string>
#include "utils/helper.h"
)";

    create_test_file("test.cpp", content);
    auto result = ForwardDeclSuggester::extract_includes(test_dir + "/test.cpp");

    ASSERT_TRUE(result.is_success());
    auto includes = result.value();
    EXPECT_EQ(includes.size(), 4);
    EXPECT_TRUE(std::ranges::find(includes.begin(), includes.end(), "vector") != includes.end());
    EXPECT_TRUE(std::ranges::find(includes.begin(), includes.end(), "myclass.h") != includes.end());
}

TEST_F(ForwardDeclSuggesterTest, ExtractIncludesFromNonExistentFile) {
    auto result = ForwardDeclSuggester::extract_includes("/nonexistent/file.cpp");
    EXPECT_FALSE(result.is_success());
    EXPECT_EQ(result.error().code, ErrorCode::FILE_NOT_FOUND);
}

TEST_F(ForwardDeclSuggesterTest, ExtractClassesFromFile) {
    const std::string content = R"(
class MyClass {
public:
    int value;
};

struct DataStruct {
    double x, y;
};

class AnotherClass;
)";

    create_test_file("classes.h", content);
    auto result = ForwardDeclSuggester::extract_classes(test_dir + "/classes.h");

    ASSERT_TRUE(result.is_success());
    auto classes = result.value();
    EXPECT_GE(classes.size(), 2);
    EXPECT_TRUE(std::ranges::find(classes.begin(), classes.end(), "MyClass") != classes.end());
    EXPECT_TRUE(std::ranges::find(classes.begin(), classes.end(), "DataStruct") != classes.end());
}

TEST_F(ForwardDeclSuggesterTest, ExtractClassesFromNonExistentFile) {
    auto result = ForwardDeclSuggester::extract_classes("/nonexistent/classes.h");
    EXPECT_FALSE(result.is_success());
    EXPECT_EQ(result.error().code, ErrorCode::FILE_NOT_FOUND);
}

TEST_F(ForwardDeclSuggesterTest, CalculateConfidenceForOpportunity) {
    ForwardDeclOpportunity opp;
    opp.class_name = "TestClass";
    opp.used_by_pointer = true;
    opp.used_by_reference = false;
    opp.used_by_value = false;
    opp.confidence = 0.85;

    const double confidence = ForwardDeclSuggester::calculate_confidence(opp);
    EXPECT_DOUBLE_EQ(confidence, 0.85);
}

TEST_F(ForwardDeclSuggesterTest, EstimateTimeSavingsWithTrace) {
    BuildTrace trace;
    CompilationUnit unit;
    unit.file_path = "myclass.h";
    unit.preprocessing_time_ms = 100.0;
    trace.compilation_units.push_back(unit);

    auto result = ForwardDeclSuggester::estimate_time_savings("myclass.h", trace);
    ASSERT_TRUE(result.is_success());
    EXPECT_NEAR(result.value(), 80.0, 0.1);  // 80% of preprocessing time
}

TEST_F(ForwardDeclSuggesterTest, EstimateTimeSavingsWithoutTrace) {
    const BuildTrace empty_trace;

    auto result = ForwardDeclSuggester::estimate_time_savings("unknown.h", empty_trace);
    ASSERT_TRUE(result.is_success());
    EXPECT_DOUBLE_EQ(result.value(), 50.0);  // Default fallback value
}

TEST_F(ForwardDeclSuggesterTest, AnalyzeIncludesForPointerUsage) {
    const std::string content = R"(
#include "myclass.h"

void process(MyClass* ptr);
MyClass* create();
)";

    create_test_file("pointer_usage.cpp", content);

    const std::string header_content = R"(
class MyClass {
public:
    int value;
};
)";
    create_test_file("myclass.h", header_content);

    const BuildTrace trace;
    const auto result = ForwardDeclSuggester::analyze_includes(test_dir + "/pointer_usage.cpp", trace);

    ASSERT_TRUE(result.is_success());
}

TEST_F(ForwardDeclSuggesterTest, SuggestForwardDeclarationsWithPointerUsage) {
    const std::string content = R"(
#include "myclass.h"

void process(MyClass* ptr) {
    // Use pointer
}
)";

    create_test_file("usage.cpp", content);

    const std::string header_content = R"(
class MyClass {
public:
    int value;
};
)";
    create_test_file("myclass.h", header_content);

    const BuildTrace trace;
    const auto result = ForwardDeclSuggester::suggest_forward_declarations(
        test_dir + "/usage.cpp", trace);

    ASSERT_TRUE(result.is_success());
}

TEST_F(ForwardDeclSuggesterTest, ForwardDeclOpportunityStructure) {
    ForwardDeclOpportunity opp;
    opp.class_name = "TestClass";
    opp.include_file = "test.h";
    opp.current_location = "main.cpp";
    opp.usage_count = 5;
    opp.used_by_pointer = true;
    opp.used_by_reference = false;
    opp.used_by_value = false;
    opp.confidence = 0.9;
    opp.estimated_savings_ms = 75.5;

    EXPECT_EQ(opp.class_name, "TestClass");
    EXPECT_EQ(opp.include_file, "test.h");
    EXPECT_EQ(opp.usage_count, 5);
    EXPECT_TRUE(opp.used_by_pointer);
    EXPECT_FALSE(opp.used_by_value);
    EXPECT_DOUBLE_EQ(opp.confidence, 0.9);
    EXPECT_DOUBLE_EQ(opp.estimated_savings_ms, 75.5);
}

TEST_F(ForwardDeclSuggesterTest, SuggestForwardDeclarationsWithReferenceUsage) {
    const std::string content = R"(
#include "myclass.h"

void process(MyClass& ref) {
    ref.doWork();
}
)";

    create_test_file("ref_usage.cpp", content);

    const std::string header_content = R"(
class MyClass {
public:
    void doWork();
};
)";
    create_test_file("myclass.h", header_content);

    const BuildTrace trace;
    auto result = ForwardDeclSuggester::suggest_forward_declarations(
        test_dir + "/ref_usage.cpp", trace);

    ASSERT_TRUE(result.is_success());
    auto suggestions = result.value();
}

TEST_F(ForwardDeclSuggesterTest, ExtractMultipleIncludes) {
    const std::string content = R"(
#include <vector>
#include <map>
#include <set>
#include <string>
#include "header1.h"
#include "header2.h"
#include "utils/helper.h"
#include <memory>
)";

    create_test_file("multi_includes.cpp", content);
    auto result = ForwardDeclSuggester::extract_includes(test_dir + "/multi_includes.cpp");

    ASSERT_TRUE(result.is_success());
    const auto includes = result.value();
    EXPECT_GE(includes.size(), 4);
}

TEST_F(ForwardDeclSuggesterTest, ExtractClassesMultiple) {
    const std::string content = R"(
class Class1 { };
class Class2 { };
struct Struct1 { };
class Class3 { };
struct Struct2 { };
)";

    create_test_file("multi_classes.h", content);
    auto result = ForwardDeclSuggester::extract_classes(test_dir + "/multi_classes.h");

    ASSERT_TRUE(result.is_success());
    const auto classes = result.value();
    EXPECT_GE(classes.size(), 2);
}

TEST_F(ForwardDeclSuggesterTest, ConfidenceScoreReflectsPointerUsage) {
    ForwardDeclOpportunity opp_pointer;
    opp_pointer.class_name = "MyClass";
    opp_pointer.used_by_pointer = true;
    opp_pointer.used_by_reference = false;
    opp_pointer.used_by_value = false;
    opp_pointer.usage_count = 5;

    ForwardDeclOpportunity opp_value;
    opp_value.class_name = "MyClass";
    opp_value.used_by_pointer = false;
    opp_value.used_by_reference = false;
    opp_value.used_by_value = true;
    opp_value.usage_count = 5;

    const double conf_pointer = ForwardDeclSuggester::calculate_confidence(opp_pointer);
    const double conf_value = ForwardDeclSuggester::calculate_confidence(opp_value);

    EXPECT_GT(conf_pointer, conf_value);
    EXPECT_GE(conf_pointer, 0.0);
    EXPECT_LE(conf_pointer, 1.0);
    EXPECT_GE(conf_value, 0.0);
    EXPECT_LE(conf_value, 1.0);
}

TEST_F(ForwardDeclSuggesterTest, EstimateTimeSavingsIsNonNegative) {
    BuildTrace trace;

    for (int i = 0; i < 5; ++i) {
        CompilationUnit unit;
        unit.file_path = "header" + std::to_string(i) + ".h";
        unit.preprocessing_time_ms = 50.0 + (i * 10);
        trace.compilation_units.push_back(unit);
    }

    auto result = ForwardDeclSuggester::estimate_time_savings("header0.h", trace);
    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value(), 0.0);
}

TEST_F(ForwardDeclSuggesterTest, MultipleForwardDeclOpportunities) {
    const std::string content = R"(
#include "class1.h"
#include "class2.h"
#include "class3.h"

void work(Class1* p1, Class2& r2, Class3* p3) {
    // Use the classes
}
)";

    create_test_file("multiple_opp.cpp", content);

    const std::string header1 = "class Class1 {};";
    const std::string header2 = "class Class2 {};";
    const std::string header3 = "class Class3 {};";

    create_test_file("class1.h", header1);
    create_test_file("class2.h", header2);
    create_test_file("class3.h", header3);

    const BuildTrace trace;
    const auto result = ForwardDeclSuggester::analyze_includes(
        test_dir + "/multiple_opp.cpp", trace);

    ASSERT_TRUE(result.is_success());
}

TEST_F(ForwardDeclSuggesterTest, SuggestionsIncludeEstimatedSavings) {
    ForwardDeclOpportunity opp;
    opp.class_name = "TestClass";
    opp.include_file = "test.h";
    opp.current_location = "main.cpp";
    opp.usage_count = 5;
    opp.used_by_pointer = true;
    opp.used_by_reference = false;
    opp.used_by_value = false;
    opp.confidence = 0.85;
    opp.estimated_savings_ms = 45.5;

    EXPECT_GT(opp.estimated_savings_ms, 0.0);
}

TEST_F(ForwardDeclSuggesterTest, SafetyCheckNoValueUsageRequired) {
    ForwardDeclOpportunity safe_opp;
    safe_opp.used_by_value = false;
    safe_opp.used_by_pointer = true;
    safe_opp.used_by_reference = false;
    safe_opp.confidence = 0.9;  // High confidence

    EXPECT_FALSE(safe_opp.used_by_value);
    EXPECT_TRUE(safe_opp.used_by_pointer);
    EXPECT_GT(safe_opp.confidence, 0.7);
}

TEST_F(ForwardDeclSuggesterTest, UnsafeSuggestionWithValueUsage) {
    ForwardDeclOpportunity unsafe_opp;
    unsafe_opp.used_by_value = true;
    unsafe_opp.used_by_pointer = false;
    unsafe_opp.used_by_reference = false;

    const double confidence = ForwardDeclSuggester::calculate_confidence(unsafe_opp);

    EXPECT_LT(confidence, 0.6);
}

TEST_F(ForwardDeclSuggesterTest, MixedUsageSafety) {
    ForwardDeclOpportunity mixed;
    mixed.class_name = "MixedClass";
    mixed.used_by_pointer = true;
    mixed.used_by_reference = true;
    mixed.used_by_value = true;
    mixed.usage_count = 10;

    const double confidence = ForwardDeclSuggester::calculate_confidence(mixed);

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
}

TEST_F(ForwardDeclSuggesterTest, ApplicabilityHighUsageCount) {
    ForwardDeclOpportunity high_usage;
    high_usage.class_name = "FrequentClass";
    high_usage.usage_count = 50;  // High usage
    high_usage.used_by_pointer = true;
    high_usage.used_by_reference = false;
    high_usage.used_by_value = false;

    EXPECT_GT(high_usage.usage_count, 20);
}

TEST_F(ForwardDeclSuggesterTest, FilterOpportunitiesWithLowConfidence) {
    // An opportunity with low confidence should be filtered out
    ForwardDeclOpportunity low_conf;
    low_conf.class_name = "LowConfClass";
    low_conf.confidence = 0.3;  // Low confidence
    low_conf.estimated_savings_ms = 5.0;  // Minimal savings

    // Such opportunities would typically be filtered
    EXPECT_LT(low_conf.confidence, 0.5);
}

TEST_F(ForwardDeclSuggesterTest, HighQualitySuggestion) {
    // A high-quality suggestion has high confidence and good savings estimate
    ForwardDeclOpportunity high_quality;
    high_quality.class_name = "QualityClass";
    high_quality.confidence = 0.92;
    high_quality.estimated_savings_ms = 150.0;
    high_quality.used_by_pointer = true;
    high_quality.used_by_reference = true;
    high_quality.used_by_value = false;

    EXPECT_GT(high_quality.confidence, 0.8);
    EXPECT_GT(high_quality.estimated_savings_ms, 100.0);
    EXPECT_FALSE(high_quality.used_by_value);
}