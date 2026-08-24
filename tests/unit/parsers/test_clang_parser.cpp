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

    TEST_F(ClangParserTest, CanParseContent_RejectsNonArrayTraceEvents) {
        constexpr std::string_view invalid_content = R"({"traceEvents": "not an array"})";
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

    TEST_F(ClangParserTest, DoesNotInferTotalTimeFromFrontendAndBackendPhases) {
        const std::string content = R"({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":800000,"name":"Total Frontend"},
                {"pid":1,"tid":0,"ph":"X","ts":900000,"dur":200000,"name":"Total Backend"}
            ]
        })";

        const auto result = parser_->parse_content(content, "/test/source.cpp");

        ASSERT_TRUE(result.is_ok());
        const auto& unit = result.value();
        EXPECT_EQ(unit.metrics.frontend_time, std::chrono::milliseconds(800));
        EXPECT_EQ(unit.metrics.backend_time, std::chrono::milliseconds(200));
        EXPECT_EQ(unit.metrics.total_time, Duration::zero());
    }

    TEST_F(ClangParserTest, ParseContent_TemplateInstantiations) {
        const std::string content = R"json({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":100000,"name":"InstantiateClass","args":{"detail":"TemplateA"}},
                {"pid":1,"tid":0,"ph":"X","ts":100,"dur":50000,"name":"InstantiateClass","args":{"detail":"TemplateA"}},
                {"pid":1,"tid":0,"ph":"X","ts":200,"dur":80000,"name":"InstantiateFunction","args":{"detail":"FunctionB"}},
                {"pid":1,"tid":0,"ph":"X","ts":300,"dur":40000,"name":"InstantiateUnrelated","args":{"detail":"NotATemplate"}},
                {"pid":1,"tid":0,"ph":"X","ts":400,"dur":30000,"name":"CodeGen Function","args":{"detail":"GeneratedFunction"}}
            ]
        })json";

        auto result = parser_->parse_content(content, {});

        ASSERT_TRUE(result.is_ok());
        auto& unit = result.value();

        EXPECT_GE(unit.templates.size(), 2u);

        const auto it = std::ranges::find_if(unit.templates,
                                             [](const auto& t) { return t.full_signature == "TemplateA"; });
        ASSERT_NE(it, unit.templates.end());
        EXPECT_EQ(it->count, 2u);
        EXPECT_EQ(unit.template_evidence, TemplateEvidence::PerSpecializationTiming);
        EXPECT_EQ(
            std::ranges::find_if(
                unit.templates,
                [](const auto& template_info) {
                    return template_info.full_signature == "NotATemplate" ||
                        template_info.full_signature == "GeneratedFunction";
                }
            ),
            unit.templates.end()
        );
    }

    TEST_F(ClangParserTest, ParseContent_RejectsTemplateInstantiationWithoutIdentity) {
        constexpr std::string_view content = R"json({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":100,"name":"InstantiateClass"}
            ]
        })json";

        const auto result = parser_->parse_content(content, "/test/source.cpp");

        ASSERT_TRUE(result.is_err());
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

    TEST_F(ClangParserTest, IncludeDepthNested) {
        const std::string content = R"({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":100,"name":"Source","args":{"detail":"/include/a.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":10,"dur":80,"name":"Source","args":{"detail":"/include/b.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":20,"dur":60,"name":"Source","args":{"detail":"/include/c.h"}}
            ]
        })";

        auto result = parser_->parse_content(content, {});
        ASSERT_TRUE(result.is_ok());

        const auto& includes = result.value().includes;
        ASSERT_EQ(includes.size(), 3u);

        for (const auto& inc : includes) {
            if (inc.header.filename() == "a.h") { EXPECT_EQ(inc.depth, 0u); }
            if (inc.header.filename() == "b.h") { EXPECT_EQ(inc.depth, 1u); }
            if (inc.header.filename() == "c.h") { EXPECT_EQ(inc.depth, 2u); }
        }
    }

    TEST_F(ClangParserTest, IncludeSelfTimeSubtractsNestedIntervals) {
        const std::string content = R"json({
            "traceEvents": [
                {"pid":1,"tid":7,"ph":"X","ts":0,"dur":1000,"name":"Source","args":{"detail":"/include/root.h"}},
                {"pid":1,"tid":7,"ph":"X","ts":100,"dur":400,"name":"Source","args":{"detail":"/include/nested.h"}},
                {"pid":1,"tid":7,"ph":"X","ts":200,"dur":100,"name":"Source","args":{"detail":"/include/leaf.h"}}
            ]
        })json";

        auto result = parser_->parse_content(content, {});
        ASSERT_TRUE(result.is_ok());

        const auto& includes = result.value().includes;
        ASSERT_EQ(includes.size(), 3u);
        const auto find_header = [&includes](const std::string_view name) {
            return std::ranges::find_if(includes, [name](const auto& include) {
                return include.header.filename() == name;
            });
        };

        const auto root = find_header("root.h");
        const auto nested = find_header("nested.h");
        const auto leaf = find_header("leaf.h");
        ASSERT_NE(root, includes.end());
        ASSERT_NE(nested, includes.end());
        ASSERT_NE(leaf, includes.end());
        ASSERT_TRUE(root->self_parse_time.has_value());
        ASSERT_TRUE(nested->self_parse_time.has_value());
        ASSERT_TRUE(leaf->self_parse_time.has_value());
        EXPECT_EQ(*root->self_parse_time, std::chrono::microseconds(600));
        EXPECT_EQ(*nested->self_parse_time, std::chrono::microseconds(300));
        EXPECT_EQ(*leaf->self_parse_time, std::chrono::microseconds(100));

        ASSERT_EQ(result.value().metric_capabilities.size(), 1u);
        EXPECT_EQ(result.value().metric_capabilities.front().metric, "frontend.source_self_time");
        EXPECT_EQ(
            result.value().metric_capabilities.front().provenance.timing_aggregation,
            TimingAggregation::Exclusive
        );
    }

    TEST_F(ClangParserTest, IncludeSelfTimeUnavailableWithoutThreadIdentity) {
        constexpr std::string_view content = R"json({
            "traceEvents": [
                {"ph":"X","ts":0,"dur":1000,"name":"Source","args":{"detail":"/include/root.h"}}
            ]
        })json";

        auto result = parser_->parse_content(content, {});
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().includes.size(), 1u);
        EXPECT_FALSE(result.value().includes.front().self_parse_time.has_value());
        ASSERT_EQ(result.value().metric_capabilities.size(), 1u);
        EXPECT_FALSE(result.value().metric_capabilities.front().provenance.limitation.empty());
    }

    TEST_F(ClangParserTest, IncludeDepthSiblings) {
        const std::string content = R"({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":100,"name":"Source","args":{"detail":"/include/a.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":5,"dur":90,"name":"Source","args":{"detail":"/include/b.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":10,"dur":80,"name":"Source","args":{"detail":"/include/c.h"}}
            ]
        })";

        auto result = parser_->parse_content(content, {});
        ASSERT_TRUE(result.is_ok());

        const auto& includes = result.value().includes;
        ASSERT_EQ(includes.size(), 3u);

        for (const auto& inc : includes) {
            if (inc.header.filename() == "a.h") { EXPECT_EQ(inc.depth, 0u); }
            if (inc.header.filename() == "b.h") { EXPECT_EQ(inc.depth, 1u); }
            if (inc.header.filename() == "c.h") { EXPECT_EQ(inc.depth, 2u); }
        }
    }

    TEST_F(ClangParserTest, IncludeDepthNonOverlapping) {
        const std::string content = R"({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":50,"name":"Source","args":{"detail":"/include/a.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":100,"dur":50,"name":"Source","args":{"detail":"/include/b.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":200,"dur":50,"name":"Source","args":{"detail":"/include/c.h"}}
            ]
        })";

        auto result = parser_->parse_content(content, {});
        ASSERT_TRUE(result.is_ok());

        const auto& includes = result.value().includes;
        ASSERT_EQ(includes.size(), 3u);

        for (const auto& inc : includes) {
            EXPECT_EQ(inc.depth, 0u);
        }
    }

    TEST_F(ClangParserTest, IncludeDepthMaxDepth) {
        const std::string content = R"({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":1000,"name":"Source","args":{"detail":"/include/root.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":10,"dur":500,"name":"Source","args":{"detail":"/include/lvl1.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":20,"dur":200,"name":"Source","args":{"detail":"/include/lvl2.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":30,"dur":100,"name":"Source","args":{"detail":"/include/lvl3.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":40,"dur":50,"name":"Source","args":{"detail":"/include/lvl4.h"}}
            ]
        })";

        auto result = parser_->parse_content(content, {});
        ASSERT_TRUE(result.is_ok());

        const auto& includes = result.value().includes;
        ASSERT_EQ(includes.size(), 5u);

        for (const auto& inc : includes) {
            if (inc.header.filename() == "root.h") { EXPECT_EQ(inc.depth, 0u); }
            if (inc.header.filename() == "lvl1.h") { EXPECT_EQ(inc.depth, 1u); }
            if (inc.header.filename() == "lvl2.h") { EXPECT_EQ(inc.depth, 2u); }
            if (inc.header.filename() == "lvl3.h") { EXPECT_EQ(inc.depth, 3u); }
            if (inc.header.filename() == "lvl4.h") { EXPECT_EQ(inc.depth, 4u); }
        }
    }

    TEST_F(ClangParserTest, IncludeDepthOutOfOrder) {
        const std::string content = R"({
            "traceEvents": [
                {"pid":1,"tid":0,"ph":"X","ts":50,"dur":50,"name":"Source","args":{"detail":"/include/a.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":0,"dur":100,"name":"Source","args":{"detail":"/include/b.h"}},
                {"pid":1,"tid":0,"ph":"X","ts":20,"dur":60,"name":"Source","args":{"detail":"/include/c.h"}}
            ]
        })";

        auto result = parser_->parse_content(content, {});
        ASSERT_TRUE(result.is_ok());

        const auto& includes = result.value().includes;
        ASSERT_EQ(includes.size(), 3u);

        for (const auto& inc : includes) {
            if (inc.header.filename() == "b.h") { EXPECT_EQ(inc.depth, 0u); }
            if (inc.header.filename() == "c.h") { EXPECT_EQ(inc.depth, 1u); }
            if (inc.header.filename() == "a.h") { EXPECT_EQ(inc.depth, 2u); }
        }
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

    TEST_F(ClangParserTest, ParseContent_RejectsMalformedCompleteEvent) {
        constexpr std::string_view content = R"json({
            "traceEvents": [
                {"name":"Source","ph":"X","ts":0,"dur":"invalid"}
            ]
        })json";

        const auto result = parser_->parse_content(content, "/test/source.cpp");

        EXPECT_TRUE(result.is_err());
    }

    TEST_F(ClangParserTest, ParseContent_RejectsNegativeDuration) {
        constexpr std::string_view content = R"json({
            "traceEvents": [
                {"name":"Source","ph":"X","ts":0,"dur":-1}
            ]
        })json";

        const auto result = parser_->parse_content(content, "/test/source.cpp");

        EXPECT_TRUE(result.is_err());
    }

    TEST_F(ClangParserTest, ParseContent_RejectsMalformedEventArguments) {
        constexpr std::string_view content = R"json({
            "traceEvents": [
                {"name":"Source","ph":"X","ts":0,"dur":1,"args":{"detail":42}}
            ]
        })json";

        const auto result = parser_->parse_content(content, "/test/source.cpp");

        EXPECT_TRUE(result.is_err());
    }

    TEST_F(ClangParserTest, ParseContent_RejectsIntegerOverflow) {
        constexpr std::string_view content = R"json({
            "traceEvents": [
                {"name":"Source","ph":"X","ts":0,"dur":1,"pid":18446744073709551615,"tid":1}
            ]
        })json";

        const auto result = parser_->parse_content(content, "/test/source.cpp");

        EXPECT_TRUE(result.is_err());
    }

    TEST_F(ClangParserTest, ParseContent_RejectsTemplateAggregateOverflow) {
        constexpr std::string_view content = R"json({
            "traceEvents": [
                {"name":"InstantiateClass","ph":"X","ts":0,"dur":9.0e15,"args":{"detail":"Box<int>"}},
                {"name":"InstantiateClass","ph":"X","ts":1,"dur":3.0e14,"args":{"detail":"Box<int>"}}
            ]
        })json";

        const auto result = parser_->parse_content(content, "/test/source.cpp");

        EXPECT_TRUE(result.is_err());
    }

    TEST_F(ClangParserTest, ParseFile_NotFound) {
        auto result = parser_->parse_file("/nonexistent/file.json");

        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
    }
}
