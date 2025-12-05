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

TEST_F(MSVCParserTest, Integration_SimpleCompilation) {
    const std::string trace = R"(
Microsoft (R) C/C++ Optimizing Compiler Version 19.29.30133
Copyright (C) Microsoft Corporation.  All rights reserved.

c1xx.dll
time(simple.cpp=0.2500)

Build succeeded.
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "simple.cpp");
    EXPECT_NEAR(unit.total_time_ms, 250.0, 0.1);
}

TEST_F(MSVCParserTest, Integration_ComplexCompilation) {
    auto result = parser->parse_string(get_complete_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    EXPECT_EQ(unit.file_path, "main.cpp");

    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);

    EXPECT_FALSE(unit.direct_includes.empty());
    EXPECT_EQ(unit.direct_includes.size(), 3);

    EXPECT_FALSE(unit.template_instantiations.empty());
    EXPECT_EQ(unit.template_instantiations.size(), 3);

    EXPECT_EQ(unit.compiler_type, "msvc");
    EXPECT_FALSE(unit.id.empty());
}

TEST_F(MSVCParserTest, Integration_TemplateHeavyCode) {
    auto result = parser->parse_string(get_template_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    EXPECT_EQ(unit.template_instantiations.size(), 4);

    // Should be sorted by time
    for (size_t i = 1; i < unit.template_instantiations.size(); ++i) {
        EXPECT_GE(unit.template_instantiations[i-1].time_ms,
                  unit.template_instantiations[i].time_ms);
    }
}

TEST_F(MSVCParserTest, Integration_HeaderHeavyCode) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=0.5000)

Include Time Summary:
time(iostream=0.5000) (10 times)
time(vector=0.4500) (8 times)
time(map=0.4000) (7 times)
time(algorithm=0.3500) (6 times)
time(memory=0.3000) (5 times)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    EXPECT_GE(unit.preprocessing_time_ms, 2000.0);

    EXPECT_EQ(unit.direct_includes.size(), 5);
}

TEST_F(MSVCParserTest, Integration_QuickCompilation) {
    const std::string trace = R"(
c1xx.dll
time(hello.cpp=0.0500)

Build succeeded.
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "hello.cpp");
    EXPECT_LT(unit.total_time_ms, 100.0); // Quick compilation
}

TEST_F(MSVCParserTest, Integration_ParseFromActualFile) {
    std::string trace_content = get_complete_trace();
    std::string file_path = create_trace_file("real_trace.txt", trace_content);

    auto result = parser->parse(file_path);
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);

    const auto& unit = units[0];
    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_FALSE(unit.id.empty());
    EXPECT_EQ(unit.compiler_type, "msvc");
}

TEST_F(MSVCParserTest, Integration_MultipleFileParsing) {
    std::string file1 = create_trace_file("trace1.txt", get_minimal_trace());
    std::string file2 = create_trace_file("trace2.txt", get_complete_trace());
    std::string file3 = create_trace_file("trace3.log", get_template_trace());

    auto result1 = parser->parse(file1);
    auto result2 = parser->parse(file2);
    auto result3 = parser->parse(file3);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());
    ASSERT_TRUE(result3.is_success());

    EXPECT_TRUE(result1.value()[0].template_instantiations.empty());
    EXPECT_FALSE(result2.value()[0].direct_includes.empty());
    EXPECT_GT(result3.value()[0].template_instantiations.size(), 0);
}

TEST_F(MSVCParserTest, Integration_RealWorldVS2019Format) {
    const std::string trace = R"(
1>------ Build started: Project: MyProject, Configuration: Debug x64 ------
1>Compiling...
1>main.cpp
1>
1>c1xx.dll
1>time(C:\Users\Dev\Project\main.cpp=2.5500)
1>time(C:\Users\Dev\Project\helper.cpp=1.2000)
1>time(C:\Users\Dev\Project\utils.cpp=0.8500)
1>
1>Include Time Summary:
1>time(C:\Program Files\Microsoft Visual Studio\...\iostream=0.6500) (12 times)
1>time(C:\Program Files\Microsoft Visual Studio\...\vector=0.4500) (8 times)
1>
1>Template Instantiation Time:
1>1.2500: std::vector<std::string>
1>0.8500: std::map<int, std::string>
1>
1>Build succeeded.
1>
1>Time Elapsed 00:00:05.62
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    EXPECT_GT(unit.total_time_ms, 4000.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
    EXPECT_FALSE(unit.template_instantiations.empty());
}

TEST_F(MSVCParserTest, Integration_WithVerboseOutput) {
    const std::string trace = R"(
Microsoft (R) C/C++ Optimizing Compiler Version 19.29.30133 for x64
Copyright (C) Microsoft Corporation.  All rights reserved.

/nologo /EHsc /W4 /std:c++20 main.cpp

c1xx.dll
time(main.cpp=3.2500)

Include Time Summary:
time(iostream=0.8000) (15 times)
time(vector=0.6000) (10 times)
time(algorithm=0.4500) (8 times)

Template Instantiation Time:
1.5000: std::vector<int>
1.2000: std::map<std::string, double>
0.8500: std::shared_ptr<MyClass>

main.obj
Generating code
Finished generating code
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_GT(unit.total_time_ms, 3000.0);
    EXPECT_EQ(unit.direct_includes.size(), 3);
    EXPECT_EQ(unit.template_instantiations.size(), 3);
}

TEST_F(MSVCParserTest, Conversion_ExactSecond) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.0000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 1000.0, 0.1);
}

TEST_F(MSVCParserTest, Conversion_FractionalSecond) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=1.5000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 1500.0, 0.1);
}

TEST_F(MSVCParserTest, Conversion_SubSecond) {
    const std::string trace = R"(
c1xx.dll
time(main.cpp=0.2500)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 250.0, 0.1);
}

TEST_F(MSVCParserTest, Conversion_LargeValue) {
    const std::string trace = R"(
c1xx.dll
time(slow.cpp=120.5000)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 120500.0, 0.1);
}

TEST_F(MSVCParserTest, Conversion_Millisecond) {
    const std::string trace = R"(
c1xx.dll
time(quick.cpp=0.0010)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 1.0, 0.1);
}

TEST_F(MSVCParserTest, Conversion_SubMillisecond) {
    const std::string trace = R"(
c1xx.dll
time(tiny.cpp=0.0001)
)";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 0.1, 0.01);
}