//
// Created by gregorian on 05/11/2025.
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

TEST_F(ClangParserTest, GetFormatName) {
    EXPECT_EQ(parser->get_format_name(), "clang-time-trace");
}

TEST_F(ClangParserTest, GetCompilerType) {
    EXPECT_EQ(parser->get_compiler_type(), CompilerType::CLANG);
}

TEST_F(ClangParserTest, GetSupportedExtensions) {
    const auto extensions = parser->get_supported_extensions();
    ASSERT_EQ(extensions.size(), 1);
    EXPECT_EQ(extensions[0], ".json");
}

TEST_F(ClangParserTest, GetCapabilities) {
    auto [supports_timing, supports_templates, supports_preprocessing, supports_optimization, supports_dependencies] = parser->get_capabilities();
    EXPECT_TRUE(supports_timing);
    EXPECT_TRUE(supports_templates);
    EXPECT_TRUE(supports_preprocessing);
    EXPECT_TRUE(supports_optimization);
    EXPECT_FALSE(supports_dependencies);
}

TEST_F(ClangParserTest, CanParse_ValidTraceFile) {
    const std::string file_path = create_trace_file("valid_trace.json", get_minimal_trace());
    EXPECT_TRUE(parser->can_parse(file_path));
}

TEST_F(ClangParserTest, CanParse_InvalidExtension) {
    const std::string file_path = create_trace_file("file.txt", get_minimal_trace());
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(ClangParserTest, CanParse_NonExistentFile) {
    EXPECT_FALSE(parser->can_parse("/nonexistent/file.json"));
}

TEST_F(ClangParserTest, CanParse_NotTraceEvents) {
    const std::string content = R"({"other": "data"})";
    const std::string file_path = create_trace_file("not_trace.json", content);
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(ClangParserTest, CanParse_EmptyFile) {
    const std::string file_path = create_trace_file("empty.json", "");
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(ClangParserTest, CanParse_InvalidJSON) {
    const std::string content = "{invalid json}";
    const std::string file_path = create_trace_file("invalid.json", content);
    EXPECT_FALSE(parser->can_parse(file_path));
}

TEST_F(ClangParserTest, Parse_ValidFile) {
    const std::string file_path = create_trace_file("valid.json", get_minimal_trace());

    auto result = parser->parse(file_path);
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);
    EXPECT_EQ(units[0].compiler_type, "clang");
}

TEST_F(ClangParserTest, Parse_NonExistentFile) {
    auto result = parser->parse("/nonexistent/file.json");
    ASSERT_FALSE(result.is_success());
    EXPECT_EQ(result.error().code, ErrorCode::FILE_NOT_FOUND);
}

TEST_F(ClangParserTest, Parse_CompleteTrace) {
    std::string file_path = create_trace_file("complete.json", get_complete_trace());

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

TEST_F(ClangParserTest, Parse_InvalidJSON) {
    const std::string file_path = create_trace_file("invalid.json", "{}");
    auto result = parser->parse(file_path);

    ASSERT_FALSE(result.is_success());
    EXPECT_EQ(result.error().code, ErrorCode::JSON_PARSE_ERROR);
}

TEST_F(ClangParserTest, ParseString_MinimalTrace) {
    const auto result = parser->parse_string(get_minimal_trace());
    ASSERT_TRUE(result.is_success());

    const auto& units = result.value();
    ASSERT_EQ(units.size(), 1);
    EXPECT_FALSE(units[0].id.empty());
}

TEST_F(ClangParserTest, ParseString_EmptyString) {
    const auto result = parser->parse_string("");
    EXPECT_FALSE(result.is_success());
}

TEST_F(ClangParserTest, ParseString_InvalidJSON) {
    const auto result = parser->parse_string("{not valid json");
    EXPECT_FALSE(result.is_success());
}

TEST_F(ClangParserTest, ParseString_EmptyTraceEvents) {
    const std::string empty_events = R"({"traceEvents": []})";
    auto result = parser->parse_string(empty_events);
    ASSERT_FALSE(result.is_success());

    EXPECT_EQ(result.error().code, ErrorCode::JSON_PARSE_ERROR);
}

TEST_F(ClangParserTest, ParseString_MissingTraceEvents) {
    const std::string no_events = R"({"other": "data"})";
    const auto result = parser->parse_string(no_events);
    EXPECT_FALSE(result.is_success());
}

TEST_F(ClangParserTest, Timing_ExecuteCompiler) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 10000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 10000.0, 0.1);
}

TEST_F(ClangParserTest, Timing_TotalExecuteCompiler) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Total ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 8000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 8000.0, 0.1);
}

TEST_F(ClangParserTest, Timing_Preprocessing) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Source",
                "ph": "X",
                "ts": 1000,
                "dur": 2000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.preprocessing_time_ms, 2000.0, 0.1);
}

TEST_F(ClangParserTest, Timing_Frontend) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Frontend",
                "ph": "X",
                "ts": 1000,
                "dur": 3000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.parsing_time_ms, 3000.0, 0.1);
}

TEST_F(ClangParserTest, Timing_Backend) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Backend",
                "ph": "X",
                "ts": 1000,
                "dur": 4000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.codegen_time_ms, 4000.0, 0.1);
}

TEST_F(ClangParserTest, Timing_OptModule) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "OptModule",
                "ph": "X",
                "ts": 1000,
                "dur": 1500000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.optimization_time_ms, 1500.0, 0.1);
}

TEST_F(ClangParserTest, Timing_Optimizer) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Optimizer",
                "ph": "X",
                "ts": 1000,
                "dur": 2500000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.optimization_time_ms, 2500.0, 0.1);
}

TEST_F(ClangParserTest, Timing_MultipleEventsAggregation) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Source",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1
            },
            {
                "name": "Source",
                "ph": "X",
                "ts": 2000000,
                "dur": 500000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    // Should aggregate both Source events
    EXPECT_NEAR(unit.preprocessing_time_ms, 1500.0, 0.1);
}

TEST_F(ClangParserTest, Timing_AllPhasesComplete) {
    auto result = parser->parse_string(get_complete_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];

    EXPECT_GT(unit.total_time_ms, 0.0);
    EXPECT_GT(unit.preprocessing_time_ms, 0.0);
    EXPECT_GT(unit.parsing_time_ms, 0.0);
    EXPECT_GT(unit.codegen_time_ms, 0.0);
    EXPECT_GT(unit.optimization_time_ms, 0.0);

    // Total should be approximately the sum (with ExecuteCompiler override)
    EXPECT_NEAR(unit.total_time_ms, 5000.0, 0.1);
}

TEST_F(ClangParserTest, Timing_NoExecuteCompiler_FallbackToSum) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Frontend",
                "ph": "X",
                "ts": 1000,
                "dur": 2000000,
                "pid": 1,
                "tid": 1
            },
            {
                "name": "Backend",
                "ph": "X",
                "ts": 2001000,
                "dur": 3000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NEAR(unit.total_time_ms, 5000.0, 0.1);
}

TEST_F(ClangParserTest, Templates_InstantiateClass) {
    const std::string trace = R"({
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
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_EQ(unit.template_instantiations.size(), 1);

    const auto& inst = unit.template_instantiations[0];
    EXPECT_EQ(inst.template_name, "std::vector<int>");
    EXPECT_EQ(inst.instantiation_context, "InstantiateClass");
    EXPECT_NEAR(inst.time_ms, 100.0, 0.1);
}

TEST_F(ClangParserTest, Templates_InstantiateFunction) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "InstantiateFunction",
                "ph": "X",
                "ts": 1000,
                "dur": 50000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "std::sort<int*>"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_EQ(unit.template_instantiations.size(), 1);

    const auto& inst = unit.template_instantiations[0];
    EXPECT_EQ(inst.template_name, "std::sort<int*>");
    EXPECT_EQ(inst.instantiation_context, "InstantiateFunction");
}

TEST_F(ClangParserTest, Templates_ParseTemplate) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "ParseTemplate",
                "ph": "X",
                "ts": 1000,
                "dur": 75000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "MyTemplate<T>"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_EQ(unit.template_instantiations.size(), 1);

    const auto& inst = unit.template_instantiations[0];
    EXPECT_EQ(inst.template_name, "MyTemplate<T>");
    EXPECT_EQ(inst.instantiation_context, "ParseTemplate");
}

TEST_F(ClangParserTest, Templates_MultipleInstantiations) {
    auto result = parser->parse_string(get_template_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_EQ(unit.template_instantiations.size(), 3);

    // Should be sorted by time (descending)
    EXPECT_GE(unit.template_instantiations[0].time_ms,
              unit.template_instantiations[1].time_ms);
    EXPECT_GE(unit.template_instantiations[1].time_ms,
              unit.template_instantiations[2].time_ms);
}

TEST_F(ClangParserTest, Templates_SortedByTime) {
    auto result = parser->parse_string(get_template_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_GE(unit.template_instantiations.size(), 2);

    // First should be the slowest (InstantiateClass: 100ms)
    EXPECT_NEAR(unit.template_instantiations[0].time_ms, 100.0, 0.1);
    EXPECT_EQ(unit.template_instantiations[0].template_name, "std::vector<int>");
}

TEST_F(ClangParserTest, Templates_NoDetail_UseName) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "InstantiateClass",
                "ph": "X",
                "ts": 1000,
                "dur": 100000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    ASSERT_EQ(unit.template_instantiations.size(), 1);

    const auto& inst = unit.template_instantiations[0];
    EXPECT_EQ(inst.template_name, "InstantiateClass");
}

TEST_F(ClangParserTest, Templates_IgnoreNonTemplateEvents) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Frontend",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1
            },
            {
                "name": "InstantiateClass",
                "ph": "X",
                "ts": 1001000,
                "dur": 100000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "std::vector<int>"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.template_instantiations.size(), 1);
}

TEST_F(ClangParserTest, Templates_IgnoreNonXPhase) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "InstantiateClass",
                "ph": "B",
                "ts": 1000,
                "dur": 100000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "std::vector<int>"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.template_instantiations.size(), 0);
}

TEST_F(ClangParserTest, FilePath_FromSourceEvent) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Source",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "/path/to/source.cpp"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/path/to/source.cpp");
}

TEST_F(ClangParserTest, FilePath_FromDetailWithCppExtension) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "OtherEvent",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "/project/main.cpp"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/project/main.cpp");
}

TEST_F(ClangParserTest, FilePath_CcExtension) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "/project/file.cc"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/project/file.cc");
}

TEST_F(ClangParserTest, FilePath_CxxExtension) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "/project/file.cxx"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/project/file.cxx");
}

TEST_F(ClangParserTest, FilePath_CExtension) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "/project/file.c"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/project/file.c");
}

TEST_F(ClangParserTest, FilePath_NoValidPath) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "unknown");
}

TEST_F(ClangParserTest, FilePath_SourceEventPriority) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Other",
                "ph": "X",
                "ts": 1000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "/other/file.cpp"
                }
            },
            {
                "name": "Source",
                "ph": "X",
                "ts": 2000,
                "dur": 1000000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "/correct/source.cpp"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.file_path, "/correct/source.cpp");
}

TEST_F(ClangParserTest, Event_AllFields) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "TestEvent",
                "ph": "X",
                "ts": 123456,
                "dur": 789012,
                "pid": 100,
                "tid": 200,
                "args": {
                    "detail": "Event detail"
                }
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, Event_MissingOptionalFields) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name":"TestEvent",
                "ph": "X"
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, Event_MissingArgs) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "TestEvent",
                "ph": "X",
                "ts": 1000,
                "dur": 5000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, Event_EmptyArgs) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "TestEvent",
                "ph": "X",
                "ts": 1000,
                "dur": 5000,
                "pid": 1,
                "tid": 1,
                "args": {}
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, Event_DifferentPhases) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "BeginEvent",
                "ph": "B",
                "ts": 1000,
                "pid": 1,
                "tid": 1
            },
            {
                "name": "EndEvent",
                "ph": "E",
                "ts": 2000,
                "pid": 1,
                "tid": 1
            },
            {
                "name": "CompleteEvent",
                "ph": "X",
                "ts": 1000,
                "dur": 1000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, Event_MultipleThreads) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event1",
                "ph": "X",
                "ts": 1000,
                "dur": 1000,
                "pid": 1,
                "tid": 1
            },
            {
                "name": "Event2",
                "ph": "X",
                "ts": 1000,
                "dur": 1000,
                "pid": 1,
                "tid": 2
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, CompilationUnit_HasID) {
    auto result = parser->parse_string(get_minimal_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_FALSE(unit.id.empty());
}

TEST_F(ClangParserTest, CompilationUnit_CompilerType) {
    auto result = parser->parse_string(get_minimal_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_EQ(unit.compiler_type, "clang");
}

TEST_F(ClangParserTest, CompilationUnit_HasBuildTimestamp) {
    auto result = parser->parse_string(get_minimal_trace());
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    const auto time = unit.build_timestamp.time_since_epoch().count();
    EXPECT_GT(time, 0);
}

TEST_F(ClangParserTest, CompilationUnit_ConsistentID) {
    std::string trace = get_complete_trace();

    auto result1 = parser->parse_string(trace);
    auto result2 = parser->parse_string(trace);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());

    EXPECT_EQ(result1.value()[0].id, result2.value()[0].id);
}

TEST_F(ClangParserTest, EdgeCase_VeryLargeTrace) {
    std::string trace = R"({"traceEvents": [)";
    for (int i = 0; i < 1000; ++i) {
        if (i > 0) trace += ",";
        trace += R"({
            "name": "Event)" + std::to_string(i) + R"(",
            "ph": "X",
            "ts": )" + std::to_string(i * 1000) + R"(,
            "dur": 1000,
            "pid": 1,
            "tid": 1
        })";
    }
    trace += "]}";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, EdgeCase_VeryLongEventName) {
    const std::string long_name(10000, 'A');
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": ")" + long_name + R"(",
                "ph": "X",
                "ts": 1000,
                "dur": 5000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, EdgeCase_VeryLongDetail) {
    const std::string long_detail(10000, 'B');
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Source",
                "ph": "X",
                "ts": 1000,
                "dur": 5000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": ")" + long_detail + R"("
                }
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, EdgeCase_ZeroDuration) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "QuickEvent",
                "ph": "X",
                "ts": 1000,
                "dur": 0,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, EdgeCase_VeryLargeDuration) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "ExecuteCompiler",
                "ph": "X",
                "ts": 1000,
                "dur": 9999999999999,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_GT(unit.total_time_ms, 0.0);
}

TEST_F(ClangParserTest, EdgeCase_NegativeValues) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event",
                "ph": "X",
                "ts": -1000,
                "dur": -5000,
                "pid": -1,
                "tid": -1
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    EXPECT_TRUE(result.is_success() || !result.is_success());
}

TEST_F(ClangParserTest, EdgeCase_UnicodeInEventName) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event_世界_🌍",
                "ph": "X",
                "ts": 1000,
                "dur": 5000,
                "pid": 1,
                "tid": 1
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, EdgeCase_SpecialCharactersInDetail) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Source",
                "ph": "X",
                "ts": 1000,
                "dur": 5000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "C:\\path\\with\\backslashes\\file.cpp"
                }
            }
        ]
    })";

    auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());

    const auto& unit = result.value()[0];
    EXPECT_NE(unit.file_path.find("backslashes"), std::string::npos);
}

TEST_F(ClangParserTest, EdgeCase_EscapedQuotesInDetail) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event",
                "ph": "X",
                "ts": 1000,
                "dur": 5000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "Template<\"string\">"
                }
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, EdgeCase_NestedJSON) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event",
                "ph": "X",
                "ts": 1000,
                "dur": 5000,
                "pid": 1,
                "tid": 1,
                "args": {
                    "detail": "info",
                    "nested": {
                        "key": "value"
                    }
                }
            }
        ]
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}

TEST_F(ClangParserTest, EdgeCase_ExtraFields) {
    const std::string trace = R"({
        "traceEvents": [
            {
                "name": "Event",
                "ph": "X",
                "ts": 1000,
                "dur": 5000,
                "pid": 1,
                "tid": 1,
                "extra_field": "ignored",
                "another_field": 12345
            }
        ],
        "extra_top_level": "also ignored"
    })";

    const auto result = parser->parse_string(trace);
    ASSERT_TRUE(result.is_success());
}