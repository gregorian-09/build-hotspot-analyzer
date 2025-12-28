//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/msvc_parser.hpp"

#include <gtest/gtest.h>

namespace bha::parsers
{
    class MSVCParserTest : public ::testing::Test {
    protected:
        void SetUp() override {
            parser_ = std::make_unique<MSVCTraceParser>();
        }

        std::unique_ptr<MSVCTraceParser> parser_;
    };

    TEST_F(MSVCParserTest, Name) {
        EXPECT_EQ(parser_->name(), "MSVC");
    }

    TEST_F(MSVCParserTest, CompilerType) {
        EXPECT_EQ(parser_->compiler_type(), CompilerType::MSVC);
    }

    TEST_F(MSVCParserTest, SupportedExtensions) {
        const auto extensions = parser_->supported_extensions();
        EXPECT_GE(extensions.size(), 1u);
    }

    TEST_F(MSVCParserTest, CanParseContent_Valid) {
        const std::string content = R"(
time(C:\project\src\main.cpp)=1.234s
time(c1xx.dll)=0.850s < 0.750s (Frontend), 0.100s (Template instantiation) >
time(c2.dll)=0.384s
)";
        EXPECT_TRUE(parser_->can_parse_content(content));
    }

    TEST_F(MSVCParserTest, CanParseContent_Invalid) {
        constexpr std::string_view invalid_content = "some random text";
        EXPECT_FALSE(parser_->can_parse_content(invalid_content));
    }

    TEST_F(MSVCParserTest, ParseContent_BasicOutput) {
        const std::string content = R"(
time(C:\project\src\main.cpp)=2.000s
time(c1xx.dll)=1.200s
time(c2.dll)=0.800s
)";

        auto result = parser_->parse_content(content, {});

        ASSERT_TRUE(result.is_ok());
        const auto& unit = result.value();

        EXPECT_GT(unit.metrics.total_time.count(), 0);
        EXPECT_GT(unit.metrics.frontend_time.count(), 0);
        EXPECT_GT(unit.metrics.backend_time.count(), 0);
    }
}