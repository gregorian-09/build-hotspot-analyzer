#include <gtest/gtest.h>

#include "bha/parsers/lld_trace_parser.hpp"

#include <chrono>

namespace bha::parsers::test {

    TEST(LLDTimeTraceParserTest, ParsesExactLinkerAndLtoSummaries) {
        constexpr std::string_view content = R"json({
  "beginningOfTime": 1737053448177000000,
  "traceEvents": [
    {"ph": "M", "name": "process_name", "args": {"name": "ld.lld"}},
    {"ph": "X", "name": "Parse input files", "ts": 0, "dur": 1000},
    {"ph": "X", "name": "LTO", "ts": 1000, "dur": 500, "args": {"detail": "module"}},
    {"ph": "X", "name": "Total LTO", "ts": 0, "dur": 500},
    {"ph": "X", "name": "ExecuteLinker", "ts": 0, "dur": 2000},
    {"ph": "X", "name": "Total ExecuteLinker", "ts": 0, "dur": 2000}
  ]
})json";

        LLDTimeTraceParser parser;
        const auto result = parser.parse_content(content, "app.time-trace");

        ASSERT_TRUE(result.is_ok());
        const auto& trace = result.value();
        ASSERT_EQ(trace.events.size(), 5u);
        ASSERT_TRUE(trace.execute_linker_time.has_value());
        ASSERT_TRUE(trace.lto_time.has_value());
        EXPECT_EQ(*trace.execute_linker_time, std::chrono::microseconds(2000));
        EXPECT_EQ(*trace.lto_time, std::chrono::microseconds(500));
        ASSERT_EQ(trace.metric_capabilities.size(), 2u);
        EXPECT_EQ(trace.metric_capabilities[0].metric, "linker.trace.wall_time");
        EXPECT_EQ(trace.metric_capabilities[1].metric, "lto.wall_time");
        EXPECT_EQ(trace.metric_capabilities[1].provenance.scope, "Total LTO");
    }

    TEST(LLDTimeTraceParserTest, RejectsTraceWithoutSupportedSummary) {
        constexpr std::string_view content = R"json({
  "traceEvents": [
    {"ph": "X", "name": "Parse input files", "ts": 0, "dur": 1000}
  ]
})json";

        LLDTimeTraceParser parser;
        const auto result = parser.parse_content(content, "app.time-trace");

        EXPECT_TRUE(result.is_err());
    }

    TEST(LLDTimeTraceParserTest, RejectsMalformedTimedEvent) {
        constexpr std::string_view content = R"json({
  "traceEvents": [
    {"ph": "X", "name": "ExecuteLinker", "ts": 0}
  ]
})json";

        LLDTimeTraceParser parser;
        const auto result = parser.parse_content(content, "app.time-trace");

        EXPECT_TRUE(result.is_err());
    }

    TEST(LLDTimeTraceParserTest, RejectsDurationOutsideNormalizedRange) {
        constexpr std::string_view content = R"json({
  "traceEvents": [
    {"ph": "X", "name": "ExecuteLinker", "ts": 0, "dur": 1e30}
  ]
})json";

        LLDTimeTraceParser parser;
        const auto result = parser.parse_content(content, "app.time-trace");

        EXPECT_TRUE(result.is_err());
    }

}  // namespace bha::parsers::test
