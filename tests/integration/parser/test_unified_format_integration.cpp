//
// Created by gregorian on 05/12/2025.
//

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "bha/parsers/unified_format.h"
#include "bha/parsers/parser.h"
#include "bha/graph/graph_builder.h"
#include "bha/analysis/analysis_engine.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <chrono>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace bha::parsers;
using namespace bha::graph;
using namespace bha::analysis;
using namespace bha::core;

class UnifiedFormatIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "bha_unified_format_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    fs::path temp_dir;

    static CompilationUnit create_sample_compilation_unit(const std::string& file_path) {
        CompilationUnit unit;
        unit.id = "unit-001";
        unit.file_path = file_path;
        unit.configuration = "Release";
        unit.compiler_type = "clang";
        unit.compiler_version = "14.0.0";
        unit.compile_flags = {"-O3", "-std=c++17", "-Wall"};

        unit.preprocessing_time_ms = 150.0;
        unit.parsing_time_ms = 250.0;
        unit.codegen_time_ms = 350.0;
        unit.optimization_time_ms = 250.0;
        unit.total_time_ms = 1000.0;

        unit.direct_includes = {
            "/usr/include/vector",
            "/usr/include/map",
            "/project/include/header.h"
        };
        unit.all_includes = {
            "/usr/include/vector",
            "/usr/include/memory",
            "/usr/include/algorithm",
            "/usr/include/map",
            "/project/include/header.h",
            "/project/include/base.h"
        };

        unit.file_size_bytes = 15000;
        unit.preprocessed_size_bytes = 125000;
        unit.build_timestamp = std::chrono::system_clock::now();
        unit.commit_sha = "abc123def456ghi789";

        for (int i = 0; i < 3; ++i) {
            TemplateInstantiation templ;
            templ.template_name = "std::vector<int>";
            templ.instantiation_context = "file.cpp:10";
            templ.time_ms = 25.0 + (i * 5);
            templ.instantiation_depth = i + 1;
            templ.call_stack = {"main", "process", "generate"};
            unit.template_instantiations.push_back(templ);
        }

        return unit;
    }

    static BuildTrace create_sample_build_trace() {
        BuildTrace trace;
        trace.trace_id = "trace-001";
        trace.build_system = "CMake";
        trace.build_system_version = "3.20";
        trace.configuration = "Release";
        trace.platform = "Linux x86_64";

        const auto now = std::chrono::system_clock::now();
        trace.build_start = now;
        trace.build_end = now + std::chrono::milliseconds(5000);
        trace.total_build_time_ms = 5000.0;

        trace.commit_sha = "abc123def456";
        trace.branch = "main";
        trace.is_clean_build = true;

        for (int i = 0; i < 3; ++i) {
            auto unit = create_sample_compilation_unit("/project/src/file" + std::to_string(i) + ".cpp");
            unit.id = "unit-" + std::to_string(i);
            trace.compilation_units.push_back(unit);
        }

        for (const auto& unit : trace.compilation_units) {
            trace.dependency_graph.add_node(unit.file_path);
            for (const auto& include : unit.direct_includes) {
                trace.dependency_graph.add_node(include);
                trace.dependency_graph.add_edge(unit.file_path, include, EdgeType::DIRECT_INCLUDE);
            }
        }

        trace.metrics.total_files_compiled = static_cast<int>(trace.compilation_units.size());
        trace.metrics.total_headers_parsed = 6;
        trace.metrics.average_file_time_ms = 1000.0;
        trace.metrics.total_dependencies = static_cast<int>(trace.dependency_graph.edge_count());

        return trace;
    }
};

TEST_F(UnifiedFormatIntegrationTest, SerializeCompilationUnit) {
    auto unit = create_sample_compilation_unit("/project/src/main.cpp");

    auto result = UnifiedFormatSerializer::serialize_compilation_unit(unit);

    ASSERT_TRUE(result.is_success());
    auto json_str = std::move(result).value();
    EXPECT_FALSE(json_str.empty());

    auto parsed = json::parse(json_str);
    EXPECT_EQ(parsed["id"], unit.id);
    EXPECT_EQ(parsed["file_path"], unit.file_path);
    EXPECT_EQ(parsed["compiler"]["type"], unit.compiler_type);
}

TEST_F(UnifiedFormatIntegrationTest, DeserializeCompilationUnit) {
    auto original = create_sample_compilation_unit("/project/src/main.cpp");

    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(original);
    ASSERT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(
        std::move(serialize_result).value()
    );
    ASSERT_TRUE(deserialize_result.is_success());

    auto deserialized = std::move(deserialize_result).value();
    EXPECT_EQ(deserialized.id, original.id);
    EXPECT_EQ(deserialized.file_path, original.file_path);
    EXPECT_EQ(deserialized.compiler_type, original.compiler_type);
    EXPECT_DOUBLE_EQ(deserialized.total_time_ms, original.total_time_ms);
}

TEST_F(UnifiedFormatIntegrationTest, SerializeBuildTrace) {
    auto trace = create_sample_build_trace();

    auto result = UnifiedFormatSerializer::serialize_build_trace(trace);

    ASSERT_TRUE(result.is_success());
    auto json_str = std::move(result).value();
    EXPECT_FALSE(json_str.empty());

    auto parsed = json::parse(json_str);
    EXPECT_EQ(parsed["metadata"]["trace_id"], trace.trace_id);
    EXPECT_EQ(parsed["metadata"]["build_system"], trace.build_system);
}

TEST_F(UnifiedFormatIntegrationTest, DeserializeBuildTrace) {
    auto original = create_sample_build_trace();

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(original);
    ASSERT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(
        std::move(serialize_result).value()
    );
    ASSERT_TRUE(deserialize_result.is_success());

    auto deserialized = std::move(deserialize_result).value();
    EXPECT_EQ(deserialized.trace_id, original.trace_id);
    EXPECT_EQ(deserialized.build_system, original.build_system);
    EXPECT_EQ(deserialized.compilation_units.size(), original.compilation_units.size());
}

TEST_F(UnifiedFormatIntegrationTest, RoundTripSerializationCompilationUnit) {
    auto original = create_sample_compilation_unit("/project/src/roundtrip.cpp");

    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(original);
    ASSERT_TRUE(serialize_result.is_success());
    auto json_str = std::move(serialize_result).value();

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(json_str);
    ASSERT_TRUE(deserialize_result.is_success());
    auto deserialized = std::move(deserialize_result).value();

    EXPECT_EQ(deserialized.id, original.id);
    EXPECT_EQ(deserialized.file_path, original.file_path);
    EXPECT_EQ(deserialized.compiler_type, original.compiler_type);
    EXPECT_EQ(deserialized.compiler_version, original.compiler_version);
    EXPECT_DOUBLE_EQ(deserialized.total_time_ms, original.total_time_ms);
    EXPECT_DOUBLE_EQ(deserialized.preprocessing_time_ms, original.preprocessing_time_ms);
    EXPECT_EQ(deserialized.direct_includes.size(), original.direct_includes.size());
    EXPECT_EQ(deserialized.template_instantiations.size(), original.template_instantiations.size());
}

TEST_F(UnifiedFormatIntegrationTest, RoundTripSerializationBuildTrace) {
    auto original = create_sample_build_trace();

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(original);
    ASSERT_TRUE(serialize_result.is_success());
    auto json_str = std::move(serialize_result).value();

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(json_str);
    ASSERT_TRUE(deserialize_result.is_success());
    auto deserialized = std::move(deserialize_result).value();

    EXPECT_EQ(deserialized.trace_id, original.trace_id);
    EXPECT_EQ(deserialized.build_system, original.build_system);
    EXPECT_EQ(deserialized.configuration, original.configuration);
    EXPECT_EQ(deserialized.compilation_units.size(), original.compilation_units.size());
    EXPECT_DOUBLE_EQ(deserialized.total_build_time_ms, original.total_build_time_ms);
}

TEST_F(UnifiedFormatIntegrationTest, SaveToFileAndLoadFromFile) {
    auto trace = create_sample_build_trace();
    fs::path output_file = temp_dir / "trace.json";

    auto save_result = UnifiedFormatSerializer::save_to_file(trace, output_file.string());
    ASSERT_TRUE(save_result.is_success());
    EXPECT_TRUE(fs::exists(output_file));

    auto load_result = UnifiedFormatSerializer::load_from_file(output_file.string());
    ASSERT_TRUE(load_result.is_success());

    auto loaded = std::move(load_result).value();
    EXPECT_EQ(loaded.trace_id, trace.trace_id);
    EXPECT_EQ(loaded.build_system, trace.build_system);
}

TEST_F(UnifiedFormatIntegrationTest, DataPreservationThroughSerialization) {
    auto original = create_sample_build_trace();
    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(original);
    ASSERT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(
        std::move(serialize_result).value()
    );
    ASSERT_TRUE(deserialize_result.is_success());

    auto restored = std::move(deserialize_result).value();

    ASSERT_EQ(restored.compilation_units.size(), original.compilation_units.size());

    for (size_t i = 0; i < original.compilation_units.size(); ++i) {
        const auto& orig_unit = original.compilation_units[i];
        const auto& rest_unit = restored.compilation_units[i];

        EXPECT_EQ(rest_unit.id, orig_unit.id);
        EXPECT_EQ(rest_unit.file_path, orig_unit.file_path);
        EXPECT_EQ(rest_unit.all_includes.size(), orig_unit.all_includes.size());
        EXPECT_EQ(rest_unit.template_instantiations.size(), orig_unit.template_instantiations.size());
        EXPECT_DOUBLE_EQ(rest_unit.total_time_ms, orig_unit.total_time_ms);
    }
}

TEST_F(UnifiedFormatIntegrationTest, VersionInformation) {
    const auto version = UnifiedFormatSerializer::get_current_version();

    EXPECT_FALSE(version.empty());
    EXPECT_THAT(version, ::testing::MatchesRegex("\\d+\\.\\d+"));
}

TEST_F(UnifiedFormatIntegrationTest, SerializeEmptyTrace) {
    BuildTrace empty_trace;
    empty_trace.trace_id = "empty-trace";

    auto result = UnifiedFormatSerializer::serialize_build_trace(empty_trace);

    ASSERT_TRUE(result.is_success());
    auto json_str = std::move(result).value();
    EXPECT_FALSE(json_str.empty());

    auto parsed = json::parse(json_str);
    EXPECT_EQ(parsed["metadata"]["trace_id"], "empty-trace");
}

TEST_F(UnifiedFormatIntegrationTest, IntegrationWithAnalysisEngine) {
    auto trace = create_sample_build_trace();

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(trace);
    ASSERT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(
        std::move(serialize_result).value()
    );
    ASSERT_TRUE(deserialize_result.is_success());
    auto restored = std::move(deserialize_result).value();

    GraphBuilder builder;
    auto graph_result = builder.build_from_trace(restored);
    ASSERT_TRUE(graph_result.is_success());

    auto graph = std::move(graph_result).value();
    BuildAnalysisEngine::Options options;
    auto analysis_result = BuildAnalysisEngine::analyze(restored, graph, options);

    ASSERT_TRUE(analysis_result.is_success());
    auto report = std::move(analysis_result).value();
    EXPECT_EQ(report.total_files_analyzed, restored.compilation_units.size());
}

TEST_F(UnifiedFormatIntegrationTest, RoundTripWithFileIO) {
    auto original = create_sample_build_trace();
    fs::path temp_file = temp_dir / "round_trip.json";

    auto save_result = UnifiedFormatSerializer::save_to_file(original, temp_file.string());
    ASSERT_TRUE(save_result.is_success());

    auto load_result = UnifiedFormatSerializer::load_from_file(temp_file.string());
    ASSERT_TRUE(load_result.is_success());
    auto restored = std::move(load_result).value();

    fs::path temp_file2 = temp_dir / "round_trip2.json";
    auto save_result2 = UnifiedFormatSerializer::save_to_file(restored, temp_file2.string());
    ASSERT_TRUE(save_result2.is_success());

    auto load_result2 = UnifiedFormatSerializer::load_from_file(temp_file2.string());
    ASSERT_TRUE(load_result2.is_success());
    auto restored2 = std::move(load_result2).value();

    EXPECT_EQ(restored.trace_id, restored2.trace_id);
    EXPECT_EQ(restored.compilation_units.size(), restored2.compilation_units.size());
}

TEST_F(UnifiedFormatIntegrationTest, MultipleSerializationFormatsConsistency) {
    auto unit1 = create_sample_compilation_unit("/project/file1.cpp");
    auto unit2 = create_sample_compilation_unit("/project/file2.cpp");

    auto result1 = UnifiedFormatSerializer::serialize_compilation_unit(unit1);
    auto result2 = UnifiedFormatSerializer::serialize_compilation_unit(unit2);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());

    auto json1 = json::parse(std::move(result1).value());
    auto json2 = json::parse(std::move(result2).value());

    EXPECT_TRUE(json1.contains("id"));
    EXPECT_TRUE(json1.contains("file_path"));
    EXPECT_TRUE(json1.contains("compiler_type"));

    EXPECT_TRUE(json2.contains("id"));
    EXPECT_TRUE(json2.contains("file_path"));
    EXPECT_TRUE(json2.contains("compiler_type"));
}

TEST_F(UnifiedFormatIntegrationTest, DeserializationErrorHandling) {
    const std::string invalid_json = "{ invalid json";

    const auto result = UnifiedFormatSerializer::deserialize_compilation_unit(invalid_json);
    ASSERT_TRUE(result.is_failure());
}

TEST_F(UnifiedFormatIntegrationTest, LargeTraceSerializationAndDeserialization) {
    BuildTrace trace;
    trace.trace_id = "large-trace";
    trace.build_system = "CMake";

    for (int i = 0; i < 100; ++i) {
        auto unit = create_sample_compilation_unit("/project/src/file" + std::to_string(i) + ".cpp");
        unit.id = "unit-" + std::to_string(i);
        trace.compilation_units.push_back(unit);
    }

    trace.total_build_time_ms = static_cast<int>(trace.compilation_units.size()) * 1000.0;

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(trace);
    ASSERT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(
        std::move(serialize_result).value()
    );
    ASSERT_TRUE(deserialize_result.is_success());

    auto restored = std::move(deserialize_result).value();
    EXPECT_EQ(restored.compilation_units.size(), 100);
}