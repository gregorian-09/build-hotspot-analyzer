//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/clang_parser.hpp"

#include <gtest/gtest.h>
#include <filesystem>

namespace bha::parsers
{
    namespace fs = std::filesystem;

    class ClangParserTest : public ::testing::Test {
    protected:
        void SetUp() override {
            parser_ = std::make_unique<ClangTraceParser>();
        }

        std::unique_ptr<ClangTraceParser> parser_;
    };

    TEST_F(ClangParserTest, Name) {
        EXPECT_EQ(parser_->name(), "Clang");
    }

    TEST_F(ClangParserTest, CompilerType) {
        EXPECT_EQ(parser_->compiler_type(), CompilerType::Clang);
    }

    TEST_F(ClangParserTest, SupportedExtensions) {
        const auto extensions = parser_->supported_extensions();
        ASSERT_EQ(extensions.size(), 1u);
        EXPECT_EQ(extensions[0], ".json");
    }

    TEST_F(ClangParserTest, CanParseContent_Valid) {
        constexpr std::string_view valid_content = R"({"traceEvents": []})";
        EXPECT_TRUE(parser_->can_parse_content(valid_content));
    }

    TEST_F(ClangParserTest, CanParseContent_Invalid) {
        constexpr std::string_view invalid_content = R"({"data": []})";
        EXPECT_FALSE(parser_->can_parse_content(invalid_content));
    }

    TEST_F(ClangParserTest, ParseContent_EmptyTrace) {
        constexpr std::string_view content = R"({"traceEvents": []})";
        auto result = parser_->parse_content(content, "/test/source.cpp");

        ASSERT_TRUE(result.is_ok());
        const auto& unit = result.value();
        EXPECT_EQ(unit.source_file, fs::path("/test/source.cpp"));
    }

    TEST_F(ClangParserTest, ParseContent_BasicTrace) {
        const std::string content = R"({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":1000000,"name":"ExecuteCompiler","args":{"detail":"/src/main.cpp"}},
                {"pid":1,"tid":0,"ph":"X","ts":100,"dur":800000,"name":"Total Frontend"},
                {"pid":1,"tid":0,"ph":"X","ts":900000,"dur":200000,"name":"Total Backend"}
            ]
        })";

        auto result = parser_->parse_content(content, {});

        ASSERT_TRUE(result.is_ok());
        const auto& unit = result.value();

        EXPECT_EQ(unit.source_file, fs::path("/src/main.cpp"));
        EXPECT_TRUE(unit.metrics.total_time == std::chrono::seconds(1));
        EXPECT_TRUE(unit.metrics.frontend_time == std::chrono::milliseconds(800));
        EXPECT_TRUE(unit.metrics.backend_time == std::chrono::milliseconds(200));
    }

    TEST_F(ClangParserTest, ParseContent_TemplateInstantiations) {
        const std::string content = R"json({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":100000,"name":"InstantiateClass","args":{"detail":"TemplateA"}},
                {"pid":1,"tid":0,"ph":"X","ts":100,"dur":50000,"name":"InstantiateClass","args":{"detail":"TemplateA"}},
                {"pid":1,"tid":0,"ph":"X","ts":200,"dur":80000,"name":"InstantiateFunction","args":{"detail":"FunctionB"}}
            ]
        })json";

        auto result = parser_->parse_content(content, {});

        ASSERT_TRUE(result.is_ok());
        auto& unit = result.value();

        EXPECT_GE(unit.templates.size(), 2u);

        const auto it = std::ranges::find_if(unit.templates,
                                             [](const auto& t) { return t.full_signature == "TemplateA"; });
        ASSERT_NE(it, unit.templates.end());
        EXPECT_GE(it->count, 2u);
    }

    TEST_F(ClangParserTest, ParseContent_IncludeInfo) {
        const std::string content = R"({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":50000,"name":"Source","args":{"detail":"/include/header.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":100,"dur":30000,"name":"Source","args":{"detail":"/include/utils.h"}}
            ]
        })";

        auto result = parser_->parse_content(content, {});

        ASSERT_TRUE(result.is_ok());
        const auto& unit = result.value();

        ASSERT_EQ(unit.includes.size(), 2u);
        EXPECT_EQ(unit.metrics.direct_includes, 2u);
    }

    TEST_F(ClangParserTest, ParseContent_InvalidJson) {
        constexpr std::string_view invalid_json = "not json at all";
        auto result = parser_->parse_content(invalid_json, {});

        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code(), ErrorCode::ParseError);
    }

    TEST_F(ClangParserTest, ParseContent_MissingTraceEvents) {
        constexpr std::string_view missing_events = R"({"data": []})";
        auto result = parser_->parse_content(missing_events, {});

        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code(), ErrorCode::ParseError);
    }

    TEST_F(ClangParserTest, ParseFile_NotFound) {
        auto result = parser_->parse_file("/nonexistent/file.json");

        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
    }
}