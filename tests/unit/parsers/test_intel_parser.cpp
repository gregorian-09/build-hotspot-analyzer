//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/intel_parser.hpp"

#include <gtest/gtest.h>

namespace bha::parsers {
    class IntelOneAPIParserTest : public ::testing::Test {
    protected:
        void SetUp() override {
            parser_ = std::make_unique<IntelOneAPIParser>();
        }

        std::unique_ptr<IntelOneAPIParser> parser_;
    };

    TEST_F(IntelOneAPIParserTest, Name) {
        EXPECT_EQ(parser_->name(), "Intel ICX");
    }

    TEST_F(IntelOneAPIParserTest, CompilerType) {
        EXPECT_EQ(parser_->compiler_type(), CompilerType::IntelOneAPI);
    }

    TEST_F(IntelOneAPIParserTest, CanParseContent_ClangFormat) {
        const std::string content = R"json({"traceEvents": [], "icx": true})json";
        EXPECT_TRUE(parser_->can_parse_content(content));
    }

    TEST_F(IntelOneAPIParserTest, CanParseContent_ClangTraceWithoutIntelBanner) {
        const std::string content = R"json({
  "traceEvents": [
    {"name": "ExecuteCompiler", "ph": "X", "ts": 0, "dur": 1234},
    {"name": "Total Frontend", "ph": "X", "ts": 0, "dur": 700}
  ]
})json";
        EXPECT_TRUE(parser_->can_parse_content(content));
    }

    TEST_F(IntelOneAPIParserTest, RejectsNonTraceOptimizationText) {
        constexpr std::string_view content =
            "Intel(R) C++ Compiler\nLOOP BEGIN at main.cpp(10,5)\n0.50 seconds\n";
        EXPECT_FALSE(parser_->can_parse_content(content));
    }
} 
