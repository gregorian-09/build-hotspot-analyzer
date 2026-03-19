//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/intel_parser.hpp"

#include <gtest/gtest.h>

namespace bha::parsers {
    class IntelClassicParserTest : public ::testing::Test {
    protected:
        void SetUp() override {
            parser_ = std::make_unique<IntelClassicParser>();
        }

        std::unique_ptr<IntelClassicParser> parser_;
    };

    TEST_F(IntelClassicParserTest, Name) {
        EXPECT_EQ(parser_->name(), "Intel ICC");
    }

    TEST_F(IntelClassicParserTest, CompilerType) {
        EXPECT_EQ(parser_->compiler_type(), CompilerType::IntelClassic);
    }

    TEST_F(IntelClassicParserTest, SupportedExtensions) {
        const auto extensions = parser_->supported_extensions();
        EXPECT_GE(extensions.size(), 1u);
    }

    TEST_F(IntelClassicParserTest, CanParseContent_Valid) {
        const std::string content = R"(
Intel(R) C++ Compiler for applications
LOOP BEGIN at main.cpp(10,5)
remark: vectorized loop
LOOP END
)";
        EXPECT_TRUE(parser_->can_parse_content(content));
    }

    TEST_F(IntelClassicParserTest, CanParseContent_LoopOnlyReportWithoutIccToken) {
        const std::string content = R"(
LOOP BEGIN at C:\project\src\main.cpp(42,7)
remark: loop was vectorized
LOOP END
)";
        EXPECT_TRUE(parser_->can_parse_content(content));
    }

    TEST_F(IntelClassicParserTest, ParseContent_ExtractsSourceFromParenLoopLocation) {
        const std::string content = R"(
LOOP BEGIN at C:\project\src\main.cpp(42,7)
0.50 seconds
LOOP END
)";
        auto result = parser_->parse_content(content, {});
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().source_file, fs::path("C:\\project\\src\\main.cpp"));
        EXPECT_GT(result.value().metrics.total_time.count(), 0);
    }

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
} 
