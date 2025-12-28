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
}