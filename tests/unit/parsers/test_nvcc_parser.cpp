//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/nvcc_parser.hpp"

#include <gtest/gtest.h>

namespace bha::parsers
{
    class NVCCParserTest : public ::testing::Test {
    protected:
        void SetUp() override {
            parser_ = std::make_unique<NVCCTraceParser>();
        }

        std::unique_ptr<NVCCTraceParser> parser_;
    };

    TEST_F(NVCCParserTest, Name) {
        EXPECT_EQ(parser_->name(), "NVCC");
    }

    TEST_F(NVCCParserTest, CompilerType) {
        EXPECT_EQ(parser_->compiler_type(), CompilerType::NVCC);
    }

    TEST_F(NVCCParserTest, SupportedExtensions) {
        const auto extensions = parser_->supported_extensions();
        EXPECT_GE(extensions.size(), 1u);
    }

    TEST_F(NVCCParserTest, CanParseContent_Valid) {
        const std::string content = R"(
nvcc compilation log
compile: 0.5s
ptxas: 0.3s
fatbinary: 0.1s
)";
        EXPECT_TRUE(parser_->can_parse_content(content));
    }

    TEST_F(NVCCParserTest, CanParseContent_Invalid) {
        constexpr std::string_view invalid_content = "some random text without cuda";
        EXPECT_FALSE(parser_->can_parse_content(invalid_content));
    }

    TEST_F(NVCCParserTest, ParseContent_BasicOutput) {
        const std::string content = R"(
nvcc timing information:
host compile: 1.0s
ptxas: 0.5s
cicc: 0.3s
fatbinary: 0.2s
)";

        auto result = parser_->parse_content(content, "/src/kernel.cu");

        ASSERT_TRUE(result.is_ok());
        const auto& unit = result.value();

        EXPECT_EQ(unit.source_file, fs::path("/src/kernel.cu"));
        EXPECT_GT(unit.metrics.total_time.count(), 0);
    }
}