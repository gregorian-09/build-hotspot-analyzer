#include "bha/parsers/p1689_module_parser.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace bha::parsers {
    namespace {
        constexpr std::string_view valid_document = R"json({
            "revision": 0,
            "version": 1,
            "rules": [
                {
                    "primary-output": "M.o",
                    "provides": [
                        {
                            "logical-name": "M",
                            "source-path": "M.cppm",
                            "is-interface": true
                        }
                    ]
                },
                {
                    "primary-output": "User.o",
                    "provides": [
                        {"logical-name": "User"}
                    ],
                    "requires": [
                        {"logical-name": "M"}
                    ]
                }
            ]
        })json";
    }  // namespace

    TEST(P1689ModuleParserTest, ParsesVersionedRulesAndReferences) {
        const P1689ModuleParser parser;
        const auto result = parser.parse_content(valid_document, "deps.json");

        ASSERT_TRUE(result.is_ok());
        const auto& graph = result.value();
        EXPECT_EQ(graph.producer, "clang-scan-deps");
        EXPECT_EQ(graph.format_version, 1);
        EXPECT_EQ(graph.revision, 0);
        ASSERT_EQ(graph.rules.size(), 2u);
        ASSERT_EQ(graph.rules.front().provides.size(), 1u);
        EXPECT_EQ(graph.rules.front().provides.front().logical_name, "M");
        ASSERT_TRUE(graph.rules.front().provides.front().source_path.has_value());
        EXPECT_EQ(*graph.rules.front().provides.front().source_path, "M.cppm");
        ASSERT_TRUE(graph.rules.front().provides.front().is_interface.has_value());
        EXPECT_TRUE(*graph.rules.front().provides.front().is_interface);
        ASSERT_EQ(graph.rules.back().requirements.size(), 1u);
        EXPECT_EQ(graph.rules.back().requirements.front().logical_name, "M");
        ASSERT_EQ(graph.metric_capabilities.size(), 1u);
        EXPECT_EQ(graph.metric_capabilities.front().metric, "module.dependencies");
        EXPECT_EQ(graph.metric_capabilities.front().provenance.evidence, EvidenceKind::Observed);
    }

    TEST(P1689ModuleParserTest, RejectsUnsupportedVersionAndDuplicateOwnership) {
        const P1689ModuleParser parser;

        const auto unsupported = parser.parse_content(
            R"json({"revision": 0, "version": 2, "rules": []})json"
        );
        EXPECT_TRUE(unsupported.is_err());

        const auto duplicate = parser.parse_content(
            R"json({
                "revision": 0,
                "version": 1,
                "rules": [
                    {"primary-output": "a.o", "provides": [{"logical-name": "M"}]},
                    {"primary-output": "b.o", "provides": [{"logical-name": "M"}]}
                ]
            })json"
        );
        EXPECT_TRUE(duplicate.is_err());
    }

    TEST(P1689ModuleParserTest, RejectsInvalidRequirementShape) {
        const P1689ModuleParser parser;
        const auto result = parser.parse_content(
            R"json({
                "revision": 0,
                "version": 1,
                "rules": [
                    {
                        "primary-output": "User.o",
                        "requires": [{"logical-name": "M", "is-interface": true}]
                    }
                ]
            })json"
        );

        EXPECT_TRUE(result.is_err());
    }

    TEST(P1689ModuleParserTest, AttachesGraphAndObservedCapabilityToTrace) {
        const P1689ModuleParser parser;
        BuildTrace trace;
        const auto path = std::filesystem::temp_directory_path() /
            ("bha-p1689-parser-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ) + ".json");
        {
            std::ofstream output(path);
            ASSERT_TRUE(output.is_open());
            output << valid_document;
        }

        const auto result = parser.attach_to_trace(trace, path);
        std::error_code cleanup_error;
        std::filesystem::remove(path, cleanup_error);

        ASSERT_TRUE(result.is_ok());
        ASSERT_TRUE(trace.module_dependency_graph.has_value());
        ASSERT_EQ(trace.metric_capabilities.size(), 1u);
        EXPECT_EQ(trace.metric_capabilities.front().metric, "module.dependencies");
    }
}  // namespace bha::parsers
