#include "bha/parsers/process_resource_parser.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>

namespace bha::parsers::test {

    TEST(ProcessResourceParserTest, ParsesDocumentedRowsAndUnits) {
        const ProcessResourceParser parser;
        const auto result = parser.parse_content(
            "clang-18,\"/tmp/object,one.o\",92000,84000,87536\n"
            "ld,\"a.out\",900,800,53568\n",
            "resources.csv"
        );

        ASSERT_TRUE(result.is_ok());
        const auto& report = result.value();
        ASSERT_EQ(report.observations.size(), 2u);
        EXPECT_EQ(report.observations.front().tool, "clang-18");
        EXPECT_EQ(report.observations.front().output, "/tmp/object,one.o");
        EXPECT_EQ(report.observations.front().total_time, std::chrono::milliseconds(92));
        EXPECT_EQ(report.observations.front().user_time, std::chrono::milliseconds(84));
        EXPECT_EQ(report.observations.front().peak_memory_kib, 87536u);
        ASSERT_EQ(report.metric_capabilities.size(), 1u);
        EXPECT_EQ(report.metric_capabilities.front().metric, "process.resource_counters");
        EXPECT_EQ(report.metric_capabilities.front().provenance.producer, "clang");
    }

    TEST(ProcessResourceParserTest, SupportsEscapedQuotesAndCrLf) {
        const ProcessResourceParser parser;
        const auto result = parser.parse_content(
            "clang,\"out\"\"name.o\",10,8,4\r\n",
            "resources.csv"
        );

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().observations.size(), 1u);
        EXPECT_EQ(result.value().observations.front().output, "out\"name.o");
    }

    TEST(ProcessResourceParserTest, RejectsMalformedRows) {
        const ProcessResourceParser parser;
        EXPECT_TRUE(parser.parse_content("clang,out,1,2,3\n").is_err());
        EXPECT_TRUE(parser.parse_content("clang,out,1,1\n").is_err());
        EXPECT_TRUE(parser.parse_content("clang,out,not-a-number,1,3\n").is_err());
        EXPECT_TRUE(parser.parse_content("").is_err());
    }

    TEST(ProcessResourceParserTest, AttachesReportAndCapabilityToTrace) {
        BuildTrace trace;
        const ProcessResourceParser parser;

        const auto path = fs::temp_directory_path() / "bha-process-resources-test.csv";
        {
            std::ofstream output(path);
            ASSERT_TRUE(output.is_open());
            output << "clang,object.o,100,75,4096\n";
        }

        const auto result = parser.attach_to_trace(trace, path);
        std::error_code cleanup_error;
        fs::remove(path, cleanup_error);

        ASSERT_TRUE(result.is_ok());
        ASSERT_TRUE(trace.process_resource_report.has_value());
        ASSERT_EQ(trace.metric_capabilities.size(), 1u);
        EXPECT_EQ(trace.metric_capabilities.front().metric, "process.resource_counters");
    }

}  // namespace bha::parsers::test
