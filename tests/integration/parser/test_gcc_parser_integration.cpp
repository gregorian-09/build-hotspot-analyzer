//
// Created by gregorian on 08/11/2025.
//

#include <gtest/gtest.h>
#include "bha/parsers/gcc_parser.h"
#include "bha/utils/file_utils.h"
#include <filesystem>

using namespace bha::parsers;
using namespace bha::core;
namespace fs = std::filesystem;

class GCCParserTest : public ::testing::Test
{
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "gcc_parser_test";
        fs::create_directories(temp_dir);

        auto parser = std::make_unique<GCCTimeReportParser>();
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    [[nodiscard]] std::string create_report_file(const std::string& filename, const std::string& content) const
    {
        const fs::path file_path = temp_dir / filename;
        std::ofstream file(file_path);
        file << content;
        file.close();
        return file_path.string();
    }

    static std::string get_minimal_report() {
        return R"(
Execution times (seconds)
Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";
    }

    static std::string get_complete_report() {
        return R"(
Compiling: /path/to/source.cpp

Execution times (seconds)
Time variable                                   usr           sys          wall
 phase setup                        :   0.10 ( 10%)   0.02 (  2%)   0.12 ( 10%)
 phase parsing                      :   0.30 ( 30%)   0.05 (  5%)   0.35 ( 29%)
 phase opt and generate             :   0.40 ( 40%)   0.08 (  8%)   0.48 ( 40%)
 phase finalize                     :   0.05 (  5%)   0.01 (  1%)   0.06 (  5%)
 preprocessing                      :   0.08 (  8%)   0.02 (  2%)   0.10 (  8%)
 name lookup                        :   0.05 (  5%)   0.01 (  1%)   0.06 (  5%)
 template instantiation             :   0.10 ( 10%)   0.02 (  2%)   0.12 ( 10%)
 RTL generation                     :   0.15 ( 15%)   0.03 (  3%)   0.18 ( 15%)
TOTAL                               :   1.00          1.00          1.20
)";
    }

    static std::string get_optimization_report() {
        return R"(
Execution times (seconds)
Time variable                                   usr           sys          wall
 phase parsing                      :   0.20 ( 20%)   0.04 (  4%)   0.24 ( 20%)
 phase opt and generate             :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
 expand                             :   0.15 ( 15%)   0.03 (  3%)   0.18 ( 15%)
 integrated RA                      :   0.10 ( 10%)   0.02 (  2%)   0.12 ( 10%)
TOTAL                               :   1.00          1.00          1.20
)";
    }

    fs::path temp_dir;
    std::unique_ptr<GCCTimeReportParser> parser;
};


TEST_F(GCCParserTest, Integration_SimpleCompilation) {
    const std::string report = R"(
gcc -c simple.cpp -ftime-report

Execution times (seconds)
Time variable                                   usr           sys          wall
 phase setup                        :   0.01 (  5%)   0.00 (  0%)   0.01 (  4%)
 phase parsing                      :   0.10 ( 50%)   0.01 ( 50%)   0.11 ( 50%)
 phase opt and generate             :   0.08 ( 40%)   0.01 ( 50%)   0.09 ( 41%)
 phase finalize                     :   0.01 (  5%)   0.00 (  0%)   0.01 (  5%)
TOTAL                               :   0.20          0.02          0.22
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "simple.cpp");
    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_GT(unit.parsing_time_ms, 0.0);
}

TEST_F(GCCParserTest, Integration_ComplexCompilation) {
    auto result = parser->parse_string(get_complete_report());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    EXPECT_EQ(unit.file_path, "/path/to/source.cpp");

    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
    EXPECT_GT(unit.parsing_time_ms, 0.0);
    EXPECT_GT(unit.codegen_time_ms, 0.0);
    EXPECT_GT(unit.optimization_time_ms, 0.0);

    EXPECT_EQ(unit.compiler_type, "gcc");
    EXPECT_FALSE(unit.id.empty());
}

TEST_F(GCCParserTest, Integration_TemplateHeavyCode) {
    const std::string report = R"(
Compiling: templates.cpp

Execution times (seconds)
Time variable                                   usr           sys          wall
 phase parsing                      :   5.00 ( 50%)   0.50 (  5%)   5.50 ( 50%)
 template instantiation             :   2.00 ( 20%)   0.20 (  2%)   2.20 ( 20%)
 name lookup                        :   1.00 ( 10%)   0.10 (  1%)   1.10 ( 10%)
 phase opt and generate             :   2.00 ( 20%)   0.20 (  2%)   2.20 ( 20%)
TOTAL                               :  10.00          1.00         11.00
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    EXPECT_GT(unit.parsing_time_ms, 8000.0);
}

TEST_F(GCCParserTest, Integration_OptimizationHeavy) {
    auto result = parser->parse_string(get_optimization_report());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    EXPECT_GT(unit.optimization_time_ms, 0.0);
    EXPECT_GT(unit.codegen_time_ms, 0.0);
}

TEST_F(GCCParserTest, Integration_QuickCompilation) {
    const std::string report = R"(
Compiling: hello.c

Execution times (seconds)
Time variable                                   usr           sys          wall
 phase parsing                      :   0.01 ( 50%)   0.00 (  0%)   0.01 ( 50%)
 phase opt and generate             :   0.01 ( 50%)   0.00 (  0%)   0.01 ( 50%)
TOTAL                               :   0.02          0.00          0.02
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "hello.c");
    EXPECT_LT(unit.total_time_ms, 100.0);
}

TEST_F(GCCParserTest, Integration_ParseFromActualFile) {
    std::string report_content = get_complete_report();
    std::string file_path = create_report_file("real_report.txt", report_content);

    auto result = parser->parse(file_path);
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);

    const auto& unit = units[0];
    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_FALSE(unit.id.empty());
    EXPECT_EQ(unit.compiler_type, "gcc");
}

TEST_F(GCCParserTest, Integration_MultipleFileParsing) {
    std::string file1 = create_report_file("report1.txt", get_minimal_report());
    std::string file2 = create_report_file("report2.txt", get_complete_report());
    std::string file3 = create_report_file("report3.log", get_optimization_report());

    auto result1 = parser->parse(file1);
    auto result2 = parser->parse(file2);
    auto result3 = parser->parse(file3);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());
    ASSERT_TRUE(result3.is_success());

    EXPECT_GT(result1.value()[0].total_time_ms, 0.0);
    EXPECT_GT(result2.value()[0].total_time_ms, 0.0);
    EXPECT_GT(result3.value()[0].total_time_ms, 0.0);
}

TEST_F(GCCParserTest, Integration_RealWorldGCC10Format) {
    const std::string report = R"(
Time variable                                   usr           sys          wall               GGC
 phase setup                        :   0.01 (  3%)   0.00 (  0%)   0.01 (  3%)     0k (  0%)
 phase parsing                      :   0.24 ( 77%)   0.01 ( 50%)   0.25 ( 76%)   196k ( 81%)
 phase lang. deferred               :   0.02 (  6%)   0.00 (  0%)   0.02 (  6%)    12k (  5%)
 phase opt and generate             :   0.04 ( 13%)   0.01 ( 50%)   0.05 ( 15%)    33k ( 14%)
 phase finalize                     :   0.00 (  0%)   0.00 (  0%)   0.00 (  0%)     0k (  0%)
 garbage collection                 :   0.01 (  3%)   0.00 (  0%)   0.01 (  3%)     0k (  0%)
 preprocessing                      :   0.01 (  3%)   0.00 (  0%)   0.01 (  3%)    44k ( 18%)
 parser (global)                    :   0.06 ( 19%)   0.00 (  0%)   0.06 ( 18%)    43k ( 18%)
 parser function body               :   0.02 (  6%)   0.00 (  0%)   0.02 (  6%)     9k (  4%)
 parser inl. func. body             :   0.00 (  0%)   0.00 (  0%)   0.00 (  0%)     0k (  0%)
 parser inl. meth. body             :   0.01 (  3%)   0.00 (  0%)   0.01 (  3%)     1k (  0%)
 template instantiation             :   0.10 ( 32%)   0.00 (  0%)   0.10 ( 30%)    86k ( 36%)
 constant expression evaluation     :   0.00 (  0%)   0.00 (  0%)   0.00 (  0%)     0k (  0%)
 constraint satisfaction            :   0.00 (  0%)   0.00 (  0%)   0.00 (  0%)     1k (  0%)
 constraint normalization           :   0.00 (  0%)   0.00 (  0%)   0.00 (  0%)     0k (  0%)
TOTAL                               :   0.31          0.02          0.33           241k
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    // Should parse all entries including the GGC column
    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_GT(unit.parsing_time_ms, 0.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
}

TEST_F(GCCParserTest, Integration_VerboseOutput) {
    const std::string report = R"(
gcc version 11.2.0 (GCC)
Compiling: /home/user/project/main.cpp
Target: x86_64-linux-gnu

Execution times (seconds)
Time variable                                   usr           sys          wall
 phase parsing                      :   1.23 ( 45%)   0.12 ( 10%)   1.35 ( 44%)
 phase opt and generate             :   1.50 ( 55%)   0.23 ( 19%)   1.73 ( 56%)
TOTAL                               :   2.73          1.20          3.08

Peak memory usage: 256MB
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/home/user/project/main.cpp");
    EXPECT_GT(unit.total_time_ms, 3000.0);
}

TEST_F(GCCParserTest, Conversion_ExactSecond) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   1.00 ( 50%)   0.20 ( 10%)   1.20 ( 50%)
TOTAL                               :   2.00          2.00          2.40
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.parsing_time_ms, 1200.0, 1.0);
}

TEST_F(GCCParserTest, Conversion_Milliseconds) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.001 (  1%)   0.000 (  0%)   0.001 (  1%)
TOTAL                               :   0.10          0.10          0.10
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.parsing_time_ms, 1.0, 0.1);
}

TEST_F(GCCParserTest, Conversion_LargeValue) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      : 120.50 ( 95%)  10.25 (  8%) 130.75 ( 95%)
TOTAL                               : 127.00        125.00        138.00
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.parsing_time_ms, 130750.0, 1.0);
}

TEST_F(GCCParserTest, Conversion_SubMillisecond) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.0005 (  0%)   0.0001 (  0%)   0.0006 (  0%)
TOTAL                               :   0.10          0.10          0.10
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.parsing_time_ms, 0.6, 0.01);
}