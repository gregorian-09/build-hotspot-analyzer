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
time(C:\path\to\c1xx.dll)=0.850s < 1617765075910 - 1617766036302 > BB [main.cpp]
time(C:\path\to\c2.dll)=0.384s < 1617766053824 - 1617766211553 > BB [main.cpp]
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

        EXPECT_EQ(unit.metrics.breakdown.parsing, Duration::zero());
        EXPECT_EQ(unit.metrics.breakdown.semantic_analysis, Duration::zero());
        EXPECT_EQ(unit.metrics.breakdown.template_instantiation, Duration::zero());
        EXPECT_EQ(unit.metrics.breakdown.optimization, Duration::zero());
        EXPECT_EQ(unit.metrics.breakdown.code_generation, Duration::zero());
        EXPECT_EQ(unit.metrics.breakdown.unclassified, std::chrono::seconds(2));
        EXPECT_EQ(unit.template_evidence, TemplateEvidence::None);
    }

    TEST_F(MSVCParserTest, AggregatesRepeatedBackendRows) {
        const std::string content = R"(
time(C:\project\src\main.cpp)=3.000s
time(C:\path\to\c1xx.dll)=1.200s
time(C:\path\to\c2.dll)=0.800s
time(C:\path\to\c2.dll)=1.000s
)";

        auto result = parser_->parse_content(content, {});

        ASSERT_TRUE(result.is_ok());
        const auto& unit = result.value();
        EXPECT_EQ(unit.metrics.frontend_time, std::chrono::duration_cast<Duration>(
            std::chrono::duration<double>(1.2)));
        EXPECT_EQ(unit.metrics.backend_time, std::chrono::duration_cast<Duration>(
            std::chrono::duration<double>(1.8)));
        EXPECT_EQ(unit.metrics.total_time, std::chrono::seconds(3));
    }

    TEST_F(MSVCParserTest, DoesNotInferTotalTimeFromCompilerComponents) {
        constexpr std::string_view content =
            "time(c1xx.dll)=1.2s\n"
            "time(c2.dll)=0.8s\n";

        const auto result = parser_->parse_content(content, {});

        ASSERT_TRUE(result.is_ok());
        const auto& unit = result.value();
        EXPECT_EQ(unit.metrics.frontend_time, std::chrono::duration_cast<Duration>(
            std::chrono::duration<double>(1.2)));
        EXPECT_EQ(unit.metrics.backend_time, std::chrono::duration_cast<Duration>(
            std::chrono::duration<double>(0.8)));
        EXPECT_EQ(unit.metrics.total_time, Duration::zero());
    }

    TEST_F(MSVCParserTest, RejectsMalformedDuration) {
        constexpr std::string_view content =
            "time(c1xx.dll)=not-a-duration\n"
            "time(c2.dll)=0.8s\n";
        auto result = parser_->parse_content(content, {});
        EXPECT_TRUE(result.is_err());
    }

    TEST_F(MSVCParserTest, RejectsLexicalComponentSubstring) {
        constexpr std::string_view content =
            "time(C:\\path\\not-c1xx.dll.backup)=1.0s\n";
        EXPECT_FALSE(parser_->can_parse_content(content));
    }

    TEST_F(MSVCParserTest, RejectsComponentAggregateOverflow) {
        const std::string content =
            "time(c1xx.dll)=9000000000.0s\n"
            "time(c1xx.dll)=300000000.0s\n";

        const auto result = parser_->parse_content(content, {});

        EXPECT_TRUE(result.is_err());
    }
}
