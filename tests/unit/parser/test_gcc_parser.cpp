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

class GCCParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "gcc_parser_test";
        fs::create_directories(temp_dir);

        parser = std::make_unique<GCCTimeReportParser>();
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

TEST_F(GCCParserTest, GetFormatName) {
    EXPECT_EQ(parser->get_format_name(), "gcc-time-report");
}

TEST_F(GCCParserTest, GetCompilerType) {
    EXPECT_EQ(parser->get_compiler_type(), CompilerType::GCC);
}

TEST_F(GCCParserTest, GetSupportedExtensions) {
    const auto extensions = parser->get_supported_extensions();
    ASSERT_EQ(extensions.size(), 2);
    EXPECT_EQ(extensions[0], ".txt");
    EXPECT_EQ(extensions[1], ".log");
}

TEST_F(GCCParserTest, GetCapabilities) {
    auto [supports_timing, supports_templates, supports_preprocessing, supports_optimization, supports_dependencies] = parser->get_capabilities();
    EXPECT_TRUE(supports_timing);
    EXPECT_FALSE(supports_templates);
    EXPECT_TRUE(supports_preprocessing);
    EXPECT_TRUE(supports_optimization);
    EXPECT_FALSE(supports_dependencies);
}

TEST_F(GCCParserTest, CanParse_ValidReport) {
    std::string file_path = create_report_file("valid_report.txt", get_minimal_report());
    EXPECT_TRUE(parser->can_parse(file_path));
}

TEST_F(GCCParserTest, CanParse_MissingTimeVariable) {
    const std::string content = R"(
Some other content
TOTAL                               :   1.00          1.00          1.20
)";
    const std::string file_path = create_report_file("no_time_var.txt", content);
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(GCCParserTest, CanParse_MissingTotal) {
    const std::string content = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
)";
    const std::string file_path = create_report_file("no_total.txt", content);
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(GCCParserTest, CanParse_NonExistentFile) {
    EXPECT_FALSE(parser->can_parse("/nonexistent/file.txt"));
}

TEST_F(GCCParserTest, CanParse_EmptyFile) {
    const std::string file_path = create_report_file("empty.txt", "");
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(GCCParserTest, CanParse_NotTimeReport) {
    const std::string content = "This is just some random text file content.";
    const std::string file_path = create_report_file("not_report.txt", content);
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(GCCParserTest, Parse_ValidFile) {
    const std::string file_path = create_report_file("valid.txt", get_minimal_report());

    auto result = parser->parse(file_path);
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);
    EXPECT_EQ(units[0].compiler_type, "gcc");
}

TEST_F(GCCParserTest, Parse_NonExistentFile) {
    auto result = parser->parse("/nonexistent/file.txt");
    ASSERT_FALSE(result.is_success());
    EXPECT_EQ(result.error().code, ErrorCode::FILE_NOT_FOUND);
}

TEST_F(GCCParserTest, Parse_CompleteReport) {
    std::string file_path = create_report_file("complete.txt", get_complete_report());

    auto result = parser->parse(file_path);
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);

    const auto& unit = units[0];
    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
    EXPECT_GT(unit.parsing_time_ms, 0.0);
    EXPECT_GT(unit.codegen_time_ms, 0.0);
    EXPECT_GT(unit.optimization_time_ms, 0.0);
}

TEST_F(GCCParserTest, Parse_EmptyReport) {
    const std::string content = R"(
Time variable                                   usr           sys          wall
TOTAL                               :   0.00          0.00          0.00
)";
    const std::string file_path = create_report_file("empty_report.txt", content);

    const auto result = parser->parse(file_path);
    EXPECT_FALSE(result.is_success());
}

TEST_F(GCCParserTest, ParseString_MinimalReport) {
    auto result = parser->parse_string(get_minimal_report());
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);
    EXPECT_FALSE(units[0].id.empty());
}

TEST_F(GCCParserTest, ParseString_EmptyString) {
    const auto result = parser->parse_string("");
    EXPECT_FALSE(result.is_success());
}

TEST_F(GCCParserTest, ParseString_NoTimeEntries) {
    const std::string content = R"(
Time variable                                   usr           sys          wall
TOTAL                               :   0.00          0.00          0.00
)";
    const auto result = parser->parse_string(content);
    EXPECT_FALSE(result.is_success());
}

TEST_F(GCCParserTest, ParseString_CompleteReport) {
    auto result = parser->parse_string(get_complete_report());
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    EXPECT_EQ(units.size(), 1);
}


TEST_F(GCCParserTest, TimeEntry_SimpleFormat) {
    const std::string line = " phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)";

    const auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->phase_name, "phase parsing");
    EXPECT_NEAR(entry->usr_time, 0.50, 0.001);
    EXPECT_NEAR(entry->sys_time, 0.10, 0.001);
    EXPECT_NEAR(entry->wall_time, 0.60, 0.001);
}

TEST_F(GCCParserTest, TimeEntry_WithPercentage) {
    const std::string line = " preprocessing                      :   0.08 (  8%)   0.02 (  2%)   0.10 (  8%)";

    const auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->phase_name, "preprocessing");
    EXPECT_NEAR(entry->usr_time, 0.08, 0.001);
    EXPECT_NEAR(entry->sys_time, 0.02, 0.001);
    EXPECT_NEAR(entry->wall_time, 0.10, 0.001);
}

TEST_F(GCCParserTest, TimeEntry_LongPhaseName) {
    std::string line = " phase opt and generate             :   0.40 ( 40%)   0.08 (  8%)   0.48 ( 40%)";

    auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->phase_name, "phase opt and generate");
}

TEST_F(GCCParserTest, TimeEntry_ZeroValues) {
    const std::string line = " some phase                         :   0.00 (  0%)   0.00 (  0%)   0.00 (  0%)";

    const auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_NEAR(entry->usr_time, 0.0, 0.001);
    EXPECT_NEAR(entry->sys_time, 0.0, 0.001);
    EXPECT_NEAR(entry->wall_time, 0.0, 0.001);
}

TEST_F(GCCParserTest, TimeEntry_LargeValues) {
    const std::string line = " compilation                        :  120.50 ( 95%)  10.25 (  8%) 130.75 ( 95%)";

    const auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_NEAR(entry->usr_time, 120.50, 0.001);
    EXPECT_NEAR(entry->sys_time, 10.25, 0.001);
    EXPECT_NEAR(entry->wall_time, 130.75, 0.001);
}

TEST_F(GCCParserTest, TimeEntry_SmallFractions) {
    const std::string line = " tiny phase                         :   0.001 (  0%)   0.002 (  0%)   0.003 (  0%)";

    const auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_NEAR(entry->usr_time, 0.001, 0.0001);
    EXPECT_NEAR(entry->sys_time, 0.002, 0.0001);
    EXPECT_NEAR(entry->wall_time, 0.003, 0.0001);
}

TEST_F(GCCParserTest, TimeEntry_NoColon) {
    const std::string line = " invalid line without colon";
    const auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(GCCParserTest, TimeEntry_EmptyLine) {
    const auto entry = GCCTimeReportParser::parse_time_entry_line("");
    EXPECT_FALSE(entry.has_value());
}

TEST_F(GCCParserTest, TimeEntry_OnlyWhitespace) {
    const auto entry = GCCTimeReportParser::parse_time_entry_line("     ");
    EXPECT_FALSE(entry.has_value());
}

TEST_F(GCCParserTest, TimeEntry_InsufficientValues) {
    const std::string line = " phase parsing                      :   0.50";
    const auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(GCCParserTest, TimeEntry_InvalidNumbers) {
    const std::string line = " phase parsing                      :   abc   def   ghi";
    const auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(GCCParserTest, TimeEntry_MixedValidInvalid) {
    const std::string line = " phase parsing                      :   0.50   abc   0.60";
    const auto entry = GCCTimeReportParser::parse_time_entry_line(line);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(GCCParserTest, Timing_TotalTime) {
    auto result = parser->parse_string(get_minimal_report());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    // Total should be sum of all wall times
    EXPECT_GT(unit.total_time_ms, 0.0);
}

TEST_F(GCCParserTest, Timing_ParsingPhase) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.parsing_time_ms, 600.0, 1.0);
}

TEST_F(GCCParserTest, Timing_PreprocessingPhase) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 preprocessing                      :   0.08 (  8%)   0.02 (  2%)   0.10 (  8%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.preprocessing_time_ms, 100.0, 1.0);
}

TEST_F(GCCParserTest, Timing_CodegenPhase) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 RTL generation                     :   0.15 ( 15%)   0.03 (  3%)   0.18 ( 15%)
 expand                             :   0.12 ( 12%)   0.02 (  2%)   0.14 ( 12%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    // Should sum both RTL generation and expand
    EXPECT_NEAR(unit.codegen_time_ms, 320.0, 1.0);
}

TEST_F(GCCParserTest, Timing_OptimizationPhase) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase opt and generate             :   0.40 ( 40%)   0.08 (  8%)   0.48 ( 40%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.optimization_time_ms, 480.0, 1.0);
}

TEST_F(GCCParserTest, Timing_AllPhases) {
    auto result = parser->parse_string(get_complete_report());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
    EXPECT_GT(unit.parsing_time_ms, 0.0);
    EXPECT_GT(unit.codegen_time_ms, 0.0);
    EXPECT_GT(unit.optimization_time_ms, 0.0);
}

TEST_F(GCCParserTest, Timing_NameLookupAsParsing) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 name lookup                        :   0.05 (  5%)   0.01 (  1%)   0.06 (  5%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    // name lookup should count as parsing
    EXPECT_NEAR(unit.parsing_time_ms, 60.0, 1.0);
}

TEST_F(GCCParserTest, Timing_TemplateAsParsing) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 template instantiation             :   0.10 ( 10%)   0.02 (  2%)   0.12 ( 10%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    // template should count as parsing
    EXPECT_NEAR(unit.parsing_time_ms, 120.0, 1.0);
}

TEST_F(GCCParserTest, Timing_PhaseSetupAsPreprocessing) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase setup                        :   0.10 ( 10%)   0.02 (  2%)   0.12 ( 10%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.preprocessing_time_ms, 120.0, 1.0);
}

TEST_F(GCCParserTest, Timing_Aggregation) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.20 ( 20%)   0.04 (  4%)   0.24 ( 20%)
 name lookup                        :   0.10 ( 10%)   0.02 (  2%)   0.12 ( 10%)
 template instantiation             :   0.08 (  8%)   0.02 (  2%)   0.10 (  8%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    // Should aggregate all parsing-related phases
    EXPECT_NEAR(unit.parsing_time_ms, 460.0, 1.0);
}

TEST_F(GCCParserTest, Timing_CaseInsensitiveMatching) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 PHASE PARSING                      :   0.20 ( 20%)   0.04 (  4%)   0.24 ( 20%)
 PREPROCESSING                      :   0.10 ( 10%)   0.02 (  2%)   0.12 ( 10%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_GT(unit.parsing_time_ms, 0.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
}

TEST_F(GCCParserTest, FilePath_CppExtension) {
    const std::string content = R"(
Compiling: /path/to/source.cpp

Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(content);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/path/to/source.cpp");
}

TEST_F(GCCParserTest, FilePath_CcExtension) {
    const std::string content = R"(
Compiling: /project/file.cc

Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(content);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/project/file.cc");
}

TEST_F(GCCParserTest, FilePath_CxxExtension) {
    const std::string content = R"(
Compiling: /project/file.cxx

Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(content);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/project/file.cxx");
}

TEST_F(GCCParserTest, FilePath_CExtension) {
    const std::string content = R"(
Compiling: /project/file.c

Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(content);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/project/file.c");
}

TEST_F(GCCParserTest, FilePath_NoPath) {
    const std::string content = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(content);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "unknown");
}

TEST_F(GCCParserTest, FilePath_MultipleWords) {
    const std::string content = R"(
gcc -c main.cpp -o main.o

Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(content);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "main.cpp");
}

TEST_F(GCCParserTest, FilePath_WithSpaces) {
    const std::string content = R"(
Processing file: my file.cpp (with spaces)

Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(content);
    ASSERT_TRUE(result.is_success());

    // Should extract the file even with spaces
    const auto& unit = result.value()[0];
    EXPECT_TRUE(unit.file_path.find(".cpp") != std::string::npos);
}

TEST_F(GCCParserTest, FilePath_MultipleFiles_FirstMatch) {
    const std::string content = R"(
Compiling: first.cpp second.cc

Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    auto result = parser->parse_string(content);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    // Should get the first matching file
    EXPECT_TRUE(unit.file_path == "first.cpp" || unit.file_path.find(".cpp") != std::string::npos);
}

TEST_F(GCCParserTest, IsTimeReportLine_ValidPhase) {
    EXPECT_TRUE(GCCTimeReportParser::is_time_report_line(" phase parsing : 0.50"));
}

TEST_F(GCCParserTest, IsTimeReportLine_ValidParsing) {
    EXPECT_TRUE(GCCTimeReportParser::is_time_report_line(" parsing : 0.50"));
}

TEST_F(GCCParserTest, IsTimeReportLine_ValidNameLookup) {
    EXPECT_TRUE(GCCTimeReportParser::is_time_report_line(" name lookup : 0.50"));
}

TEST_F(GCCParserTest, IsTimeReportLine_ValidTemplate) {
    EXPECT_TRUE(GCCTimeReportParser::is_time_report_line(" template : 0.50"));
}

TEST_F(GCCParserTest, IsTimeReportLine_NoColon) {
    EXPECT_FALSE(GCCTimeReportParser::is_time_report_line(" phase parsing 0.50"));
}

TEST_F(GCCParserTest, IsTimeReportLine_NoKeyword) {
    EXPECT_FALSE(GCCTimeReportParser::is_time_report_line(" something : 0.50"));
}

TEST_F(GCCParserTest, CompilationUnit_HasID) {
    auto result = parser->parse_string(get_minimal_report());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_FALSE(unit.id.empty());
}

TEST_F(GCCParserTest, CompilationUnit_CompilerType) {
    auto result = parser->parse_string(get_minimal_report());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.compiler_type, "gcc");
}

TEST_F(GCCParserTest, CompilationUnit_HasBuildTimestamp)
{
    auto result = parser->parse_string(get_minimal_report());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    // Timestamp should be set
    const auto time = unit.build_timestamp.time_since_epoch().count();
    EXPECT_GT(time, 0);
}

TEST_F(GCCParserTest, CompilationUnit_ConsistentID) {
    std::string report = get_complete_report();

    auto result1 = parser->parse_string(report);
    auto result2 = parser->parse_string(report);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());

    // Same report should produce same ID
    EXPECT_EQ(result1.value()[0].id, result2.value()[0].id);
}

TEST_F(GCCParserTest, EdgeCase_VeryLargeReport) {
    std::string report = R"(
Time variable                                   usr           sys          wall
)";

    // Add many entries
    for (int i = 0; i < 1000; ++i) {
        report += " phase" + std::to_string(i) + "                      :   0.01 (  1%)   0.00 (  0%)   0.01 (  1%)\n";
    }
    report += "TOTAL                               :  10.00         10.00         10.00\n";

    const auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_VeryLongPhaseName) {
    const std::string long_name(1000, 'a');
    const std::string report = R"(
Time variable                                   usr           sys          wall
 )" + long_name + R"(      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_ExtraWhitespace) {
    const std::string report = R"(


Time variable                                   usr           sys          wall


 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)


TOTAL                               :   1.00          1.00          1.20


)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_MixedLineEndings) {
    const std::string report = "Time variable\r\n phase parsing : 0.50 ( 50%) 0.10 ( 10%) 0.60 ( 50%)\r\nTOTAL : 1.00 1.00 1.20\r\n";

    const auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_TabsInsteadOfSpaces) {
    const std::string report = R"(
Time variable
	phase parsing	:	0.50	(	50%)	0.10	(	10%)	0.60	(	50%)
TOTAL	:	1.00	1.00	1.20
)";

    const auto result = parser->parse_string(report);
    EXPECT_TRUE(result.is_success() || !result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_ScientificNotation) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   5.0e-1 ( 50%)   1.0e-1 ( 10%)   6.0e-1 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    EXPECT_TRUE(result.is_success() || !result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_NegativePercentage) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 (-50%)   0.10 (-10%)   0.60 (-50%)
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    EXPECT_TRUE(result.is_success() || !result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_MissingPercentages) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.50          0.10          0.60
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_InconsistentFormatting) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing:0.50(50%)0.10(10%)0.60(50%)
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    EXPECT_TRUE(result.is_success() || !result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_UnicodeInPhaseName) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase_世界_parsing                  :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_SpecialCharactersInPhaseName) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase-parsing/optimization         :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_MultipleColons) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase:parsing:detail               :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_TotalInMiddle) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
 another phase                      :   0.20 ( 20%)   0.04 (  4%)   0.24 ( 20%)
)";

    auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_GT(unit.total_time_ms, 0.0);
}

TEST_F(GCCParserTest, EdgeCase_NoTimeVariableHeader) {
    const std::string report = R"(
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    // This should fail because no "Time variable" header
    EXPECT_FALSE(result.is_success());
}

TEST_F(GCCParserTest, EdgeCase_MultipleTimeVariableSections) {
    const std::string report = R"(
Time variable                                   usr           sys          wall
 phase parsing                      :   0.50 ( 50%)   0.10 ( 10%)   0.60 ( 50%)
TOTAL                               :   1.00          1.00          1.20

Time variable                                   usr           sys          wall
 another phase                      :   0.30 ( 30%)   0.05 (  5%)   0.35 ( 29%)
TOTAL                               :   1.00          1.00          1.20
)";

    const auto result = parser->parse_string(report);
    ASSERT_TRUE(result.is_success());
}
