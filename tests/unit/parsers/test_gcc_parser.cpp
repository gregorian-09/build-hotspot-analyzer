//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/gcc_parser.hpp"

#include <gtest/gtest.h>

namespace bha::parsers
{
    class GCCParserTest : public ::testing::Test {
    protected:
        void SetUp() override {
            parser_ = std::make_unique<GCCTraceParser>();
        }

        std::unique_ptr<GCCTraceParser> parser_;
    };

    TEST_F(GCCParserTest, Name) {
        EXPECT_EQ(parser_->name(), "GCC");
    }

    TEST_F(GCCParserTest, CompilerType) {
        EXPECT_EQ(parser_->compiler_type(), CompilerType::GCC);
    }

    TEST_F(GCCParserTest, SupportedExtensions) {
        const auto extensions = parser_->supported_extensions();
        EXPECT_GE(extensions.size(), 1u);
    }

    TEST_F(GCCParserTest, CanParseContent_Valid) {
        const std::string content = R"(
Time variable                                   usr           sys          wall
phase parsing                         :   0.12 (  8%)   0.01 (  5%)   0.13 (  8%)
phase lang. deferred                  :   0.02 (  1%)   0.00 (  0%)   0.02 (  1%)
)";
        EXPECT_TRUE(parser_->can_parse_content(content));
    }

    TEST_F(GCCParserTest, CanParseContent_Invalid) {
        constexpr std::string_view invalid_content = "some random text";
        EXPECT_FALSE(parser_->can_parse_content(invalid_content));
    }

    TEST_F(GCCParserTest, ParseContent_BasicReport) {
        const std::string content = R"(
Time variable                                   usr           sys          wall
phase parsing                         :   0.50 ( 25%)   0.10 (  5%)   0.60 ( 30%)
phase template instantiation          :   0.30 ( 15%)   0.05 (  2%)   0.35 ( 17%)
phase code generation                 :   0.40 ( 20%)   0.08 (  4%)   0.48 ( 24%)
)";

        auto result = parser_->parse_content(content, "/src/test.cpp");

        ASSERT_TRUE(result.is_ok());
        const auto& unit = result.value();

        EXPECT_EQ(unit.source_file, fs::path("/src/test.cpp"));
        EXPECT_GT(unit.metrics.total_time.count(), 0);
        EXPECT_GT(unit.metrics.breakdown.parsing.count(), 0);
    }
}