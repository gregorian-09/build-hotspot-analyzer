//
// Created by gregorian on 08/11/2025.
//

#include <gtest/gtest.h>
#include "bha/parsers/clang_parser.h"
#include "bha/utils/file_utils.h"
#include <filesystem>

using namespace bha::parsers;
using namespace bha::core;
namespace fs = std::filesystem;

class ClangParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "clang_parser_test";
        fs::create_directories(temp_dir);

        parser = std::make_unique<ClangTimeTraceParser>();
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    [[nodiscard]] std::string create_trace_file(const std::string& filename, const std::string& content) const {
        const fs::path file_path = temp_dir / filename;
        std::ofstream file(file_path);
        file << content;
        file.close();
        return file_path.string();
    }

    static std::string get_minimal_trace()
    {
        return R"({
            "traceEvents": [
                {
                    "pid": 1,
                    "tid": 1,
                    "ph": "X",
                    "ts": 1000,
                    "dur": 5000000,
                    "name": "Total ExecuteCompiler"
                },
                {
                    "pid": 1,
                    "tid": 1,
                    "ph": "X",
                    "ts": 1000,
                    "dur": 4000000,
                    "name": "Source",
                    "args": {
                        "detail": "test.cpp"
                    }
                }
            ],
            "beginningOfTime": 1000
        })";
    }

    static std::string get_complete_trace() {
        return R"({
            "traceEvents": [
                {
                    "name": "Source",
                    "ph": "X",
                    "ts": 1000,
                    "dur": 1000000,
                    "pid": 1,
                    "tid": 1,
                    "args": {
                        "detail": "/path/to/file.cpp"
                    }
                },
                {
                    "name": "Frontend",
                    "ph": "X",
                    "ts": 1001000,
                    "dur": 2000000,
                    "pid": 1,
                    "tid": 1
                },
                {
                    "name": "Backend",
                    "ph": "X",
                    "ts": 3001000,
                    "dur": 1500000,
                    "pid": 1,
                    "tid": 1
                },
                {
                    "name": "OptModule",
                    "ph": "X",
                    "ts": 4501000,
                    "dur": 500000,
                    "pid": 1,
                    "tid": 1
                },
                {
                    "name": "ExecuteCompiler",
                    "ph": "X",
                    "ts": 1000,
                    "dur": 5000000,
                    "pid": 1,
                    "tid": 1
                }
            ]
        })";
    }

    static std::string get_template_trace() {
        return R"({
            "traceEvents": [
                {
                    "name": "InstantiateClass",
                    "ph": "X",
                    "ts": 1000,
                    "dur": 100000,
                    "pid": 1,
                    "tid": 1,
                    "args": {
                        "detail": "std::vector<int>"
                    }
                },
                {
                    "name": "InstantiateFunction",
                    "ph": "X",
                    "ts": 101000,
                    "dur": 50000,
                    "pid": 1,
                    "tid": 1,
                    "args": {
                        "detail": "std::sort<int*>"
                    }
                },
                {
                    "name": "ParseTemplate",
                    "ph": "X",
                    "ts": 151000,
                    "dur": 75000,
                    "pid": 1,
                    "tid": 1,
                    "args": {
                        "detail": "MyTemplate<T>"
                    }
                },
                {
                    "name": "ExecuteCompiler",
                    "ph": "X",
                    "ts": 1000,
                    "dur": 300000,
                    "pid": 1,
                    "tid": 1
                }
            ]
        })";
    }

    fs::path temp_dir;
    std::unique_ptr<ClangTimeTraceParser> parser;
};

TEST_F(ClangParserTest, Integration_STLHeavyCode) {
    std::string trace = R"({
        "traceEvents": [
            {
                "name": "ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 10000000,
                "pid": 1,
                "tid": 1
            },
            {
                "name": "InstantiateClass",
                "ph": "X",
                "ts": 1000,
                "dur": 500000,
                "pid": 1,
                "tid": 1,
                "args": {"detail": "std::vector<int>"}
            },
            {
                "name": "InstantiateClass",
                "ph": "X",
                "ts": 501000,
                "dur": 300000,
                "pid": 1,
                "tid": 1,
                "args": {"detail": "std::map<std::string, int>"}
            },
            {
                "name": "InstantiateFunction",
                "ph": "X",
                "ts": 801000,
                "dur": 200000,
                "pid": 1,
                "tid": 1,
                "args": {"detail": "std::sort<std::vector<int>::iterator>"}
            },
            {
                "name": "Source",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1,
                "args": {"detail": "/project/stl_heavy.cpp"}
            },
            {
                "name": "Frontend",
                "ph": "X",
                "ts": 1001000,
                "dur": 5000000,
                "pid": 1,
                "tid": 1
            },
            {
                "name": "Backend",
                "ph": "X",
                "ts": 6001000,
                "dur": 3000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    // Check file path
    EXPECT_EQ(unit.file_path, "/project/stl_heavy.cpp");

    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
    EXPECT_GT(unit.parsing_time_ms, 0.0);
    EXPECT_GT(unit.codegen_time_ms, 0.0);

    ASSERT_EQ(unit.template_instantiations.size(), 3);
    // Should be sorted by time (descending)
    EXPECT_EQ(unit.template_instantiations[0].template_name, "std::vector<int>");
    EXPECT_NEAR(unit.template_instantiations[0].time_ms, 500.0, 0.1);
}

TEST_F(ClangParserTest, Integration_QuickCompilation) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 100000,
                "pid": 1,
                "tid": 1
            },
            {
                "name": "Source",
                "ph": "X",
                "ts": 1000,
                "dur": 50000,
                "pid": 1,
                "tid": 1,
                "args": {"detail": "/project/simple.cpp"}
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 100.0, 0.1);
    EXPECT_TRUE(unit.template_instantiations.empty());
}

TEST_F(ClangParserTest, Integration_ComplexTemplateMetaprogramming) {
    std::string trace = R"({
        "traceEvents": [
            {
                "name": "ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 50000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    // Add many template instantiations
    std::string events;
    for (int i = 0; i < 100; ++i) {
        events += R"(,{
            "name": "InstantiateClass",
            "ph": "X",
            "ts": )" + std::to_string(1000 + i * 10000) + R"(,
            "dur": )" + std::to_string(100000 - i * 500) + R"(,
            "pid": 1,
            "tid": 1,
            "args": {"detail": "Template)" + std::to_string(i) + R"(<T>"}
        })";
    }

    const size_t insert_pos = trace.find(']');
    trace.insert(insert_pos, events);

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_EQ(unit.template_instantiations.size(), 100);

    // Should be sorted by time
    for (size_t i = 1; i < unit.template_instantiations.size(); ++i) {
        EXPECT_GE(unit.template_instantiations[i-1].time_ms,
                  unit.template_instantiations[i].time_ms);
    }
}

TEST_F(ClangParserTest, Integration_ParseFromActualFile) {
    std::string trace_content = get_complete_trace();
    std::string file_path = create_trace_file("real_trace.json", trace_content);

    auto result = parser->parse(file_path);
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);

    const auto& unit = units[0];
    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_FALSE(unit.id.empty());
    EXPECT_EQ(unit.compiler_type, "clang");
}

TEST_F(ClangParserTest, Integration_MultipleFileParsing) {
    std::string file1 = create_trace_file("trace1.json", get_minimal_trace());
    std::string file2 = create_trace_file("trace2.json", get_complete_trace());
    std::string file3 = create_trace_file("trace3.json", get_template_trace());

    auto result1 = parser->parse(file1);
    auto result2 = parser->parse(file2);
    auto result3 = parser->parse(file3);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());
    ASSERT_TRUE(result3.is_success());

    EXPECT_EQ(result1.value()[0].template_instantiations.size(), 0);
    EXPECT_GT(result2.value()[0].optimization_time_ms, 0.0);
    EXPECT_GT(result3.value()[0].template_instantiations.size(), 0);
}

TEST_F(ClangParserTest, Conversion_ExactThousand) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 1000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 1.0, 0.001);
}

TEST_F(ClangParserTest, Conversion_FractionalMilliseconds) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 1500,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 1.5, 0.001);
}

TEST_F(ClangParserTest, Conversion_SubMillisecond) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 500,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 0.5, 0.001);
}

TEST_F(ClangParserTest, Conversion_LargeValue) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 60000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 60000.0, 0.1); // 60 seconds
}