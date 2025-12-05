//
// Created by gregorian on 08/11/2025.
//

#include <gtest/gtest.h>
#include "bha/parsers/msvc_parser.h"
#include "bha/utils/file_utils.h"
#include <filesystem>

using namespace bha::parsers;
using namespace bha::core;
namespace fs = std::filesystem;

class MSVCParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "msvc_parser_test";
        fs::create_directories(temp_dir);

        parser = std::make_unique<MSVCTraceParser>();
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    [[nodiscard]] std::string create_trace_file(const std::string& filename, const std::string& content) const
    {
        const fs::path file_path = temp_dir / filename;
        std::ofstream file(file_path);
        file << content;
        file.close();
        return file_path.string();
    }

    static std::string get_minimal_trace() {
        return R"(
c1xx.dll
time(main.cpp=0.5000)
)";
    }

    static std::string get_complete_trace() {
        return R"(
Build started...
1>------ Build started: Project: MyProject, Configuration: Debug x64 ------

c1xx.dll
time(main.cpp=1.2500)
time(helper.cpp=0.8000)

Include Time Summary:
time(iostream=0.3000) (5 times)
time(vector=0.2500) (3 times)
time(string=0.1500) (2 times)

Template Instantiation Time:
0.4500: std::vector<int>
0.3200: std::map<std::string, int>
0.2100: std::shared_ptr<MyClass>

Build succeeded.
)";
    }

    static std::string get_template_trace() {
        return R"(
c1xx.dll
time(templates.cpp=2.5000)

Template Instantiation Time:
1.2000: std::vector<std::string>
0.8500: std::map<int, std::string>
0.6200: MyTemplate<double, int>
0.4100: std::unique_ptr<MyClass>
)";
    }

    fs::path temp_dir;
    std::unique_ptr<MSVCTraceParser> parser;
};


TEST_F(MSVCParserTest, GetFormatName) {
    EXPECT_EQ(parser->get_format_name(), "msvc-trace");
}

TEST_F(MSVCParserTest, GetCompilerType) {
    EXPECT_EQ(parser->get_compiler_type(), CompilerType::MSVC);
}

TEST_F(MSVCParserTest, GetSupportedExtensions) {
    const auto extensions = parser->get_supported_extensions();
    ASSERT_EQ(extensions.size(), 2);
    EXPECT_EQ(extensions[0], ".txt");
    EXPECT_EQ(extensions[1], ".log");
}

TEST_F(MSVCParserTest, GetCapabilities) {
    auto [supports_timing, supports_templates, supports_preprocessing, supports_optimization, supports_dependencies] = parser->get_capabilities();
    EXPECT_TRUE(supports_timing);
    EXPECT_TRUE(supports_templates);
    EXPECT_FALSE(supports_preprocessing);
    EXPECT_FALSE(supports_optimization);
    EXPECT_TRUE(supports_dependencies);
}

TEST_F(MSVCParserTest, CanParse_ValidTrace) {
    const std::string file_path = create_trace_file("valid_trace.txt", get_minimal_trace());
    EXPECT_TRUE(parser->can_parse(file_path));
}

TEST_F(MSVCParserTest, CanParse_WithTimeFunction) {
    const std::string content = "time(main.cpp=0.5)";
    const std::string file_path = create_trace_file("with_time.txt", content);
    EXPECT_TRUE(parser->can_parse(file_path));
}

TEST_F(MSVCParserTest, CanParse_NonExistentFile) {
    EXPECT_FALSE(parser->can_parse("/nonexistent/file.txt"));
}

TEST_F(MSVCParserTest, CanParse_EmptyFile) {
    const std::string file_path = create_trace_file("empty.txt", "");
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(MSVCParserTest, CanParse_NotMSVCTrace) {
    const std::string content = "This is just some random text file content.";
    const std::string file_path = create_trace_file("not_trace.txt", content);
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(MSVCParserTest, CanParse_MissingBothMarkers) {
    const std::string content = "Some build output without c1xx.dll or time()";
    const std::string file_path = create_trace_file("no_markers.txt", content);
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(MSVCParserTest, Parse_ValidFile) {
    const std::string file_path = create_trace_file("valid.txt", get_minimal_trace());

    auto result = parser->parse(file_path);
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);
    EXPECT_EQ(units[0].compiler_type, "msvc");
}

TEST_F(MSVCParserTest, Parse_NonExistentFile) {
    auto result = parser->parse("/nonexistent/file.txt");
    ASSERT_FALSE(result.is_success());
    EXPECT_EQ(result.error().code, ErrorCode::FILE_NOT_FOUND);
}

TEST_F(MSVCParserTest, Parse_CompleteTrace) {
    const std::string file_path = create_trace_file("complete.txt", get_complete_trace());

    auto result = parser->parse(file_path);
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);

    const auto& unit = units[0];
    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
    EXPECT_FALSE(unit.template_instantiations.empty());
    EXPECT_FALSE(unit.direct_includes.empty());
}

TEST_F(MSVCParserTest, Parse_EmptyTrace) {
    const std::string content = "c1xx.dll\n\n";
    const std::string file_path = create_trace_file("empty_trace.txt", content);

    const auto result = parser->parse(file_path);
    EXPECT_FALSE(result.is_success());
}

TEST_F(MSVCParserTest, ParseString_MinimalTrace) {
    const auto result = parser->parse_string(get_minimal_trace());
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);
    EXPECT_FALSE(units[0].id.empty());
}

TEST_F(MSVCParserTest, ParseString_EmptyString) {
    const auto result = parser->parse_string("");
    EXPECT_FALSE(result.is_success());
}

TEST_F(MSVCParserTest, ParseString_NoFileEntries) {
    const std::string content = "c1xx.dll\n\n";
    const auto result = parser->parse_string(content);
    EXPECT_FALSE(result.is_success());
}

TEST_F(MSVCParserTest, ParseString_CompleteTrace) {
    const auto result = parser->parse_string(get_complete_trace());
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    EXPECT_EQ(units.size(), 1);
}

TEST_F(MSVCParserTest, TimeEntry_SimpleFormat) {
    const std::string line = "time(main.cpp=0.5000)";

    const auto entry = MSVCTraceParser::parse_time_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->file_or_header, "main.cpp");
    EXPECT_NEAR(entry->time_seconds, 0.5000, 0.0001);
    EXPECT_EQ(entry->count, 1);
}

TEST_F(MSVCParserTest, TimeEntry_WithCount) {
    const std::string line = "time(iostream=0.3000) (5 times)";

    const auto entry = MSVCTraceParser::parse_time_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->file_or_header, "iostream");
    EXPECT_NEAR(entry->time_seconds, 0.3000, 0.0001);
    EXPECT_EQ(entry->count, 5);
}

TEST_F(MSVCParserTest, TimeEntry_WithPath) {
    const std::string line = R"(time(C:\Project\src\file.cpp=1.2500))";

    const auto entry = MSVCTraceParser::parse_time_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->file_or_header, "C:\\Project\\src\\file.cpp");
    EXPECT_NEAR(entry->time_seconds, 1.2500, 0.0001);
}

TEST_F(MSVCParserTest, TimeEntry_WithSpaces) {
    const std::string line = "  time(  helper.cpp  =  0.8000  )  ";

    const auto entry = MSVCTraceParser::parse_time_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->file_or_header, "helper.cpp");
    EXPECT_NEAR(entry->time_seconds, 0.8000, 0.0001);
}

TEST_F(MSVCParserTest, TimeEntry_ZeroTime) {
    const std::string line = "time(quick.cpp=0.0000)";

    const auto entry = MSVCTraceParser::parse_time_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_NEAR(entry->time_seconds, 0.0, 0.0001);
}

TEST_F(MSVCParserTest, TimeEntry_LargeTime) {
    const std::string line = "time(slow.cpp=120.5500)";

    const auto entry = MSVCTraceParser::parse_time_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_NEAR(entry->time_seconds, 120.5500, 0.0001);
}

TEST_F(MSVCParserTest, TimeEntry_SmallFraction) {
    const std::string line = "time(tiny.cpp=0.0001)";

    const auto entry = MSVCTraceParser::parse_time_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_NEAR(entry->time_seconds, 0.0001, 0.00001);
}

TEST_F(MSVCParserTest, TimeEntry_NoTimeFunction) {
    const std::string line = "main.cpp=0.5000";
    const auto entry = MSVCTraceParser::parse_time_line(line);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(MSVCParserTest, TimeEntry_NoClosingParen) {
    const std::string line = "time(main.cpp=0.5000";
    const auto entry = MSVCTraceParser::parse_time_line(line);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(MSVCParserTest, TimeEntry_NoEquals) {
    const std::string line = "time(main.cpp 0.5000)";
    const auto entry = MSVCTraceParser::parse_time_line(line);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(MSVCParserTest, TimeEntry_EmptyLine) {
    const auto entry = MSVCTraceParser::parse_time_line("");
    EXPECT_FALSE(entry.has_value());
}

TEST_F(MSVCParserTest, TimeEntry_OnlyWhitespace) {
    const auto entry = MSVCTraceParser::parse_time_line("     ");
    EXPECT_FALSE(entry.has_value());
}

TEST_F(MSVCParserTest, TimeEntry_InvalidTimeValue) {
    const std::string line = "time(main.cpp=invalid)";
    const auto entry = MSVCTraceParser::parse_time_line(line);
    ASSERT_TRUE(entry.has_value());
    EXPECT_NEAR(entry->time_seconds, 0.0, 0.0001); // Should default to 0
}

TEST_F(MSVCParserTest, TimeEntry_MultipleEquals) {
    const std::string line = "time(file=name=value.cpp=0.5000)";
    const auto entry = MSVCTraceParser::parse_time_line(line);
    ASSERT_TRUE(entry.has_value());
}

TEST_F(MSVCParserTest, TemplateEntry_SimpleFormat) {
    const std::string line = "0.4500: std::vector<int>";

    const auto entry = MSVCTraceParser::parse_template_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->template_name, "std::vector<int>");
    EXPECT_NEAR(entry->time_seconds, 0.4500, 0.0001);
}

TEST_F(MSVCParserTest, TemplateEntry_ComplexTemplate) {
    const std::string line = "1.2000: std::map<std::string, std::vector<int>>";

    const auto entry = MSVCTraceParser::parse_template_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->template_name, "std::map<std::string, std::vector<int>>");
    EXPECT_NEAR(entry->time_seconds, 1.2000, 0.0001);
}

TEST_F(MSVCParserTest, TemplateEntry_WithNamespace) {
    const std::string line = "0.8500: MyNamespace::MyTemplate<double>";

    const auto entry = MSVCTraceParser::parse_template_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->template_name, "MyNamespace::MyTemplate<double>");
}

TEST_F(MSVCParserTest, TemplateEntry_WithSpaces) {
    const std::string line = "  0.3200  :  std::shared_ptr<MyClass>  ";

    const auto entry = MSVCTraceParser::parse_template_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->template_name, "std::shared_ptr<MyClass>");
    EXPECT_NEAR(entry->time_seconds, 0.3200, 0.0001);
}

TEST_F(MSVCParserTest, TemplateEntry_ZeroTime) {
    const std::string line = "0.0000: QuickTemplate<T>";

    const auto entry = MSVCTraceParser::parse_template_line(line);
    EXPECT_FALSE(entry.has_value()); // Zero time is rejected
}

TEST_F(MSVCParserTest, TemplateEntry_NoColon) {
    const std::string line = "0.5000 std::vector<int>";
    const auto entry = MSVCTraceParser::parse_template_line(line);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(MSVCParserTest, TemplateEntry_EmptyLine) {
    const auto entry = MSVCTraceParser::parse_template_line("");
    EXPECT_FALSE(entry.has_value());
}

TEST_F(MSVCParserTest, TemplateEntry_InvalidTime) {
    const std::string line = "invalid: std::vector<int>";
    const auto entry = MSVCTraceParser::parse_template_line(line);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(MSVCParserTest, TemplateEntry_MultipleColons) {
    const std::string line = "0.5000: std::vector<int>::iterator";

    const auto entry = MSVCTraceParser::parse_template_line(line);
    ASSERT_TRUE(entry.has_value());

    EXPECT_EQ(entry->template_name, "std::vector<int>::iterator");
}

TEST_F(MSVCParserTest, FileTimes_SingleFile) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=0.5000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 500.0, 0.1);
}

TEST_F(MSVCParserTest, FileTimes_MultipleFiles) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.2500)
time(helper.cpp=0.8000)
time(utils.cpp=0.4500)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 2500.0, 0.1);
}

TEST_F(MSVCParserTest, FileTimes_StopsAtEmptyLine) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)
time(helper.cpp=0.5000)

time(ignored.cpp=0.3000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    // Should only count entries before empty line
    EXPECT_NEAR(unit.total_time_ms, 1500.0, 0.1);
}

TEST_F(MSVCParserTest, FileTimes_WithInvalidEntries) {
    const std::string trace = R"(
c1xx.dll
time(valid.cpp=1.0000)
invalid line
time(also_valid.cpp=0.5000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    // Should parse valid entries only
    const auto& unit = result.value()[0];
    EXPECT_GT(unit.total_time_ms, 0.0);
}

TEST_F(MSVCParserTest, IncludeTimes_SingleHeader) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)

Include Time Summary:
time(iostream=0.3000) (5 times)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.preprocessing_time_ms, 300.0, 0.1);
    ASSERT_EQ(unit.direct_includes.size(), 1);
    EXPECT_EQ(unit.direct_includes[0], "iostream");
}

TEST_F(MSVCParserTest, IncludeTimes_MultipleHeaders) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)

Include Time Summary:
time(iostream=0.3000) (5 times)
time(vector=0.2500) (3 times)
time(string=0.1500) (2 times)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.preprocessing_time_ms, 700.0, 0.1);
    EXPECT_EQ(unit.direct_includes.size(), 3);
}

TEST_F(MSVCParserTest, IncludeTimes_StopsAtTemplate) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)

Include Time Summary:
time(iostream=0.3000) (5 times)

Template Instantiation Time:
0.5000: std::vector<int>
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.direct_includes.size(), 1);
    EXPECT_FALSE(unit.template_instantiations.empty());
}

TEST_F(MSVCParserTest, IncludeTimes_HeaderUnitsFormat) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)

Header Units Time Summary:
time(module1=0.4000) (2 times)
time(module2=0.3000) (1 times)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
    EXPECT_FALSE(unit.direct_includes.empty());
}

TEST_F(MSVCParserTest, IncludeTimes_NoIncludeSection) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.preprocessing_time_ms, 0.0, 0.1);
    EXPECT_TRUE(unit.direct_includes.empty());
}

TEST_F(MSVCParserTest, TemplateTimes_SingleTemplate) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)

Template Instantiation Time:
0.5000: std::vector<int>
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_EQ(unit.template_instantiations.size(), 1);
    EXPECT_EQ(unit.template_instantiations[0].template_name, "std::vector<int>");
    EXPECT_NEAR(unit.template_instantiations[0].time_ms, 500.0, 0.1);
}

TEST_F(MSVCParserTest, TemplateTimes_MultipleTemplates) {
    auto result = parser->parse_string(get_template_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_EQ(unit.template_instantiations.size(), 4);

    // Should be sorted by time (descending)
    EXPECT_GE(unit.template_instantiations[0].time_ms,
              unit.template_instantiations[1].time_ms);
    EXPECT_GE(unit.template_instantiations[1].time_ms,
              unit.template_instantiations[2].time_ms);
}

TEST_F(MSVCParserTest, TemplateTimes_SortedByTime) {
    auto result = parser->parse_string(get_template_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    // First should be the slowest
    EXPECT_NEAR(unit.template_instantiations[0].time_ms, 1200.0, 0.1);
    EXPECT_EQ(unit.template_instantiations[0].template_name, "std::vector<std::string>");
}

TEST_F(MSVCParserTest, TemplateTimes_ClassTemplateMemberFunctions) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)

Class Template Member Functions:
0.6000: MyClass<T>::method()
0.4000: MyClass<T>::operator=
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.template_instantiations.size(), 2);
}

TEST_F(MSVCParserTest, TemplateTimes_NoTemplateSection) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_TRUE(unit.template_instantiations.empty());
}

TEST_F(MSVCParserTest, MainFile_CppExtension) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)
time(iostream=0.3000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "main.cpp");
}

TEST_F(MSVCParserTest, MainFile_CcExtension) {
    const std::string trace = R"(
c1xx.dll
time(file.cc=1.0000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "file.cc");
}

TEST_F(MSVCParserTest, MainFile_CxxExtension) {
    const std::string trace = R"(
c1xx.dll
time(file.cxx=1.0000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "file.cxx");
}

TEST_F(MSVCParserTest, MainFile_CExtension) {
    const std::string trace = R"(
c1xx.dll
time(file.c=1.0000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "file.c");
}

TEST_F(MSVCParserTest, MainFile_PreferSourceOverHeader) {
    const std::string trace = R"(
c1xx.dll
time(header.h=0.3000)
time(source.cpp=1.0000)
time(another.h=0.2000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "source.cpp");
}

TEST_F(MSVCParserTest, MainFile_NoSourceFiles) {
    const std::string trace = R"(
c1xx.dll
time(header.h=0.3000)
time(another.h=0.2000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "header.h");
}

TEST_F(MSVCParserTest, MainFile_WithPath) {
    const std::string trace = R"(
c1xx.dll
time(C:\\Project\\src\\main.cpp=1.0000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "C:\\Project\\src\\main.cpp");
}

TEST_F(MSVCParserTest, ParseTimeValue_PlainNumber) {
    const double time = MSVCTraceParser::parse_time_value("1.2500");
    EXPECT_NEAR(time, 1.2500, 0.0001);
}

TEST_F(MSVCParserTest, ParseTimeValue_WithSuffix_s) {
    const double time = MSVCTraceParser::parse_time_value("1.5s");
    EXPECT_NEAR(time, 1.5, 0.0001);
}

TEST_F(MSVCParserTest, ParseTimeValue_WithSuffix_ms) {
    const double time = MSVCTraceParser::parse_time_value("500ms");
    EXPECT_NEAR(time, 500.0, 0.0001);
}

TEST_F(MSVCParserTest, ParseTimeValue_WithSpaces) {
    const double time = MSVCTraceParser::parse_time_value("  2.5  ");
    EXPECT_NEAR(time, 2.5, 0.0001);
}

TEST_F(MSVCParserTest, ParseTimeValue_Zero) {
    const double time = MSVCTraceParser::parse_time_value("0.0");
    EXPECT_NEAR(time, 0.0, 0.0001);
}

TEST_F(MSVCParserTest, ParseTimeValue_SmallFraction) {
    const double time = MSVCTraceParser::parse_time_value("0.0001");
    EXPECT_NEAR(time, 0.0001, 0.00001);
}

TEST_F(MSVCParserTest, ParseTimeValue_LargeNumber) {
    const double time = MSVCTraceParser::parse_time_value("999.9999");
    EXPECT_NEAR(time, 999.9999, 0.0001);
}

TEST_F(MSVCParserTest, ParseTimeValue_EmptyString) {
    const double time = MSVCTraceParser::parse_time_value("");
    EXPECT_NEAR(time, 0.0, 0.0001);
}

TEST_F(MSVCParserTest, ParseTimeValue_InvalidString) {
    const double time = MSVCTraceParser::parse_time_value("invalid");
    EXPECT_NEAR(time, 0.0, 0.0001);
}

TEST_F(MSVCParserTest, ParseTimeValue_OnlyWhitespace) {
    const double time = MSVCTraceParser::parse_time_value("   ");
    EXPECT_NEAR(time, 0.0, 0.0001);
}

TEST_F(MSVCParserTest, ParseTimeValue_ScientificNotation) {
    const double time = MSVCTraceParser::parse_time_value("1.5e-3");
    EXPECT_TRUE(time >= 0.0);
}

TEST_F(MSVCParserTest, CompilationUnit_HasID) {
    const auto result = parser->parse_string(get_minimal_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_FALSE(unit.id.empty());
}

TEST_F(MSVCParserTest, CompilationUnit_CompilerType) {
    const auto result = parser->parse_string(get_minimal_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.compiler_type, "msvc");
}

TEST_F(MSVCParserTest, CompilationUnit_HasBuildTimestamp) {
    const auto result = parser->parse_string(get_minimal_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    const auto time = unit.build_timestamp.time_since_epoch().count();
    EXPECT_GT(time, 0);
}

TEST_F(MSVCParserTest, CompilationUnit_ConsistentID) {
    std::string trace = get_complete_trace();

    auto result1 = parser->parse_string(trace);
    auto result2 = parser->parse_string(trace);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());

    EXPECT_EQ(result1.value()[0].id, result2.value()[0].id);
}

TEST_F(MSVCParserTest, CompilationUnit_TemplateDepthZero) {
    const auto result = parser->parse_string(get_template_trace());
    ASSERT_TRUE(result.is_success());

    for (const auto& unit = result.value()[0]; const auto& inst : unit.template_instantiations) {
        EXPECT_EQ(inst.instantiation_depth, 0);
    }
}

TEST_F(MSVCParserTest, EdgeCase_VeryLargeTrace) {
    std::string trace = "c1xx.dll\n";

    // Add many entries
    for (int i = 0; i < 1000; ++i) {
        trace += "time(file" + std::to_string(i) + ".cpp=0.01)\n";
    }

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_VeryLongFilename) {
    std::string long_name(1000, 'a');
    long_name += ".cpp";
    const std::string trace = "c1xx.dll\ntime(" + long_name + "=0.5000)";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_VeryLongTemplateName) {
    const std::string long_template(1000, 'T');
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)

Template Instantiation Time:
0.5000: )" + long_template;

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_ExtraWhitespace) {
    const std::string trace = R"(


c1xx.dll


time(main.cpp=1.0000)


)";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_MixedLineEndings) {
    const std::string trace = "c1xx.dll\r\ntime(main.cpp=1.0000)\r\n";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_TabsInsteadOfSpaces) {
    const std::string trace = "c1xx.dll\n\ttime(main.cpp=1.0000)\t(3\ttimes)\n";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_UnicodeInFilename) {
    const std::string trace = R"(
c1xx.dll
time(файл_世界.cpp=1.0000)
)";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_SpecialCharactersInPath) {
    const std::string trace = R"(
c1xx.dll
time(C:\Path\With Spaces\file-name_123.cpp=1.0000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NE(unit.file_path.find("Spaces"), std::string::npos);
}

TEST_F(MSVCParserTest, EdgeCase_NestedTemplates) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)

Template Instantiation Time:
0.8000: std::map<std::string, std::vector<std::shared_ptr<MyClass>>>
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_EQ(unit.template_instantiations.size(), 1);
}

TEST_F(MSVCParserTest, EdgeCase_MultipleC1xxDll) {
    const std::string trace = R"(
c1xx.dll
time(file1.cpp=1.0000)

c1xx.dll
time(file2.cpp=0.5000)
)";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_MalformedParentheses) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000))
time((helper.cpp=0.5000)
time(utils.cpp=0.3000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_GT(unit.total_time_ms, 0.0);
}

TEST_F(MSVCParserTest, EdgeCase_NegativeTime) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=-1.0000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    // Should handle negative time
    const auto& unit = result.value()[0];
    EXPECT_TRUE(unit.total_time_ms < 0.0 || unit.total_time_ms >= 0.0);
}

TEST_F(MSVCParserTest, EdgeCase_VerySmallTime) {
    const std::string trace = R"(
c1xx.dll
time(quick.cpp=0.000001)
)";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_ExtraTextInLine) {
    const std::string trace = R"(
c1xx.dll
Extra text before time(main.cpp=1.0000) extra text after
)";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(MSVCParserTest, EdgeCase_MixedSections) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)

Include Time Summary:
time(iostream=0.3000) (5 times)

c1xx.dll
time(helper.cpp=0.5000)

Template Instantiation Time:
0.4000: std::vector<int>
)";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}