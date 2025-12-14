//
// Created by gregorian on 05/12/2025.
//

#include <gtest/gtest.h>
#include "bha/parsers/unified_format.h"
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace bha::parsers;
using namespace bha::core;
namespace fs = std::filesystem;

class UnifiedFormatTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "bha_unified_format_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    static CompilationUnit create_simple_unit() {
        CompilationUnit unit;
        unit.id = "unit_1";
        unit.file_path = "/path/to/main.cpp";
        unit.configuration = "Release";
        unit.total_time_ms = 1234.5;
        unit.preprocessing_time_ms = 100.0;
        unit.parsing_time_ms = 200.0;
        unit.codegen_time_ms = 500.0;
        unit.optimization_time_ms = 434.5;
        unit.compiler_type = "Clang";
        unit.compiler_version = "14.0.0";
        unit.compile_flags = {"-O2", "-std=c++17", "-Wall"};
        unit.direct_includes = {"vector", "string", "memory"};
        unit.file_size_bytes = 5000;
        unit.preprocessed_size_bytes = 50000;
        return unit;
    }

    static CompilationUnit create_complex_unit() {
        auto unit = create_simple_unit();
        unit.id = "unit_complex";

        TemplateInstantiation templ1;
        templ1.template_name = "std::vector<int>";
        templ1.time_ms = 25.5;
        templ1.instantiation_depth = 2;
        templ1.call_stack = {"main", "process"};
        unit.template_instantiations.push_back(templ1);

        TemplateInstantiation templ2;
        templ2.template_name = "std::map<std::string, int>";
        templ2.time_ms = 50.0;
        templ2.instantiation_depth = 3;
        templ2.call_stack = {"main", "init", "setup"};
        unit.template_instantiations.push_back(templ2);

        unit.all_includes = unit.direct_includes;
        unit.all_includes.emplace_back("algorithm");
        unit.all_includes.emplace_back("numeric");

        return unit;
    }

    static BuildTrace create_simple_trace() {
        BuildTrace trace;
        trace.trace_id = "trace_001";
        trace.build_system = "CMake";
        trace.build_system_version = "3.20.0";
        trace.configuration = "Release";
        trace.platform = "Linux";
        trace.total_build_time_ms = 5000.0;
        trace.is_clean_build = true;
        trace.commit_sha = "abc123def456";
        trace.branch = "main";

        trace.compilation_units.push_back(create_simple_unit());

        return trace;
    }

    static BuildTrace create_complex_trace() {
        BuildTrace trace = create_simple_trace();
        trace.trace_id = "trace_complex";
        trace.compilation_units.clear();

        // Add multiple compilation units
        for (int i = 0; i < 3; i++) {
            auto unit = create_complex_unit();
            unit.id = "unit_" + std::to_string(i);
            unit.file_path = "/path/to/file" + std::to_string(i) + ".cpp";
            trace.compilation_units.push_back(unit);
        }

        trace.dependency_graph.add_node("/path/to/header.h");
        trace.dependency_graph.add_node("/path/to/file0.cpp");
        trace.dependency_graph.add_node("/path/to/file1.cpp");
        trace.dependency_graph.add_edge("/path/to/file0.cpp", "/path/to/header.h", EdgeType::DIRECT_INCLUDE);
        trace.dependency_graph.add_edge("/path/to/file1.cpp", "/path/to/header.h", EdgeType::DIRECT_INCLUDE);

        trace.targets["executable"] = {"/path/to/file0.cpp", "/path/to/file1.cpp"};
        trace.build_order = {"/path/to/file0.cpp", "/path/to/file1.cpp"};

        trace.metrics.total_files_compiled = 2;
        trace.metrics.total_headers_parsed = 5;
        trace.metrics.average_file_time_ms = 2500.0;
        trace.metrics.total_dependencies = 8;

        return trace;
    }

    static BuildTrace create_large_trace() {
        BuildTrace trace = create_complex_trace();
        trace.trace_id = "trace_large";
        trace.compilation_units.clear();

        for (int i = 0; i < 50; i++) {
            auto unit = create_complex_unit();
            unit.id = "unit_large_" + std::to_string(i);
            unit.file_path = "/path/to/file_" + std::to_string(i) + ".cpp";
            unit.total_time_ms = 1000.0 + (i * 10);
            trace.compilation_units.push_back(unit);
        }

        return trace;
    }

    static BuildTrace create_partial_trace() {
        BuildTrace trace;
        trace.trace_id = "trace_partial";
        trace.build_system = "Make";
        trace.compilation_units.push_back(create_simple_unit());
        return trace;
    }
};

TEST_F(UnifiedFormatTest, SerializeSimpleCompilationUnit) {
    const auto unit = create_simple_unit();
    auto result = UnifiedFormatSerializer::serialize_compilation_unit(unit);
    EXPECT_TRUE(result.is_success());

    auto json = std::move(result).value();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"id\""), std::string::npos);
    EXPECT_NE(json.find("\"file_path\""), std::string::npos);
    EXPECT_NE(json.find("main.cpp"), std::string::npos);
}

TEST_F(UnifiedFormatTest, SerializeComplexCompilationUnit) {
    const auto unit = create_complex_unit();
    auto result = UnifiedFormatSerializer::serialize_compilation_unit(unit);
    EXPECT_TRUE(result.is_success());

    auto json = std::move(result).value();
    EXPECT_NE(json.find("template_instantiations"), std::string::npos);
    EXPECT_NE(json.find("std::vector"), std::string::npos);
}

TEST_F(UnifiedFormatTest, SerializeSimpleBuildTrace) {
    const auto trace = create_simple_trace();
    auto result = UnifiedFormatSerializer::serialize_build_trace(trace);
    EXPECT_TRUE(result.is_success());

    auto json = std::move(result).value();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"trace_id\""), std::string::npos);
    EXPECT_NE(json.find("\"build_system\""), std::string::npos);
    EXPECT_NE(json.find("CMake"), std::string::npos);
}

TEST_F(UnifiedFormatTest, SerializeComplexBuildTrace) {
    const auto trace = create_complex_trace();
    auto result = UnifiedFormatSerializer::serialize_build_trace(trace);
    EXPECT_TRUE(result.is_success());

    auto json = std::move(result).value();
    EXPECT_NE(json.find("dependency_graph"), std::string::npos);
    EXPECT_NE(json.find("metrics"), std::string::npos);
}

TEST_F(UnifiedFormatTest, SerializeLargeBuildTrace) {
    const auto trace = create_large_trace();
    auto result = UnifiedFormatSerializer::serialize_build_trace(trace);
    EXPECT_TRUE(result.is_success());

    auto json = std::move(result).value();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"trace_id\""), std::string::npos);
}

TEST_F(UnifiedFormatTest, SerializePartialBuildTrace) {
    const auto trace = create_partial_trace();
    auto result = UnifiedFormatSerializer::serialize_build_trace(trace);
    EXPECT_TRUE(result.is_success());

    auto json = std::move(result).value();
    EXPECT_NE(json.find("\"trace_id\""), std::string::npos);
}

TEST_F(UnifiedFormatTest, DeserializeSimpleCompilationUnit) {
    auto unit = create_simple_unit();
    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(unit);
    EXPECT_TRUE(serialize_result.is_success());
    auto json = std::move(serialize_result).value();

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(json);
    EXPECT_TRUE(deserialize_result.is_success());

    auto deserialized = std::move(deserialize_result).value();
    EXPECT_EQ(deserialized.id, unit.id);
    EXPECT_EQ(deserialized.file_path, unit.file_path);
}

TEST_F(UnifiedFormatTest, DeserializeComplexCompilationUnit) {
    auto unit = create_complex_unit();
    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(unit);
    EXPECT_TRUE(serialize_result.is_success());
    auto json = std::move(serialize_result).value();

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(json);
    EXPECT_TRUE(deserialize_result.is_success());

    auto deserialized = std::move(deserialize_result).value();
    EXPECT_EQ(deserialized.id, unit.id);
    EXPECT_EQ(deserialized.template_instantiations.size(), unit.template_instantiations.size());
}

TEST_F(UnifiedFormatTest, DeserializeSimpleBuildTrace) {
    auto trace = create_simple_trace();
    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(trace);
    EXPECT_TRUE(serialize_result.is_success());
    auto json = std::move(serialize_result).value();

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(json);
    EXPECT_TRUE(deserialize_result.is_success());

    auto deserialized = std::move(deserialize_result).value();
    EXPECT_EQ(deserialized.trace_id, trace.trace_id);
    EXPECT_EQ(deserialized.build_system, trace.build_system);
}

TEST_F(UnifiedFormatTest, DeserializeComplexBuildTrace) {
    auto trace = create_complex_trace();
    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(trace);
    EXPECT_TRUE(serialize_result.is_success());
    auto json = std::move(serialize_result).value();

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(json);
    EXPECT_TRUE(deserialize_result.is_success());

    auto deserialized = std::move(deserialize_result).value();
    EXPECT_EQ(deserialized.trace_id, trace.trace_id);
    EXPECT_EQ(deserialized.compilation_units.size(), trace.compilation_units.size());
    EXPECT_EQ(deserialized.dependency_graph.node_count(), trace.dependency_graph.node_count());
}

TEST_F(UnifiedFormatTest, DeserializeLargeBuildTrace) {
    auto trace = create_large_trace();
    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(trace);
    EXPECT_TRUE(serialize_result.is_success());
    auto json = std::move(serialize_result).value();

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(json);
    EXPECT_TRUE(deserialize_result.is_success());

    auto deserialized = std::move(deserialize_result).value();
    EXPECT_EQ(deserialized.compilation_units.size(), 50);
}

TEST_F(UnifiedFormatTest, RoundTripSimpleCompilationUnit) {
    auto original = create_simple_unit();

    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.id, original.id);
    EXPECT_EQ(recovered.file_path, original.file_path);
    EXPECT_EQ(recovered.configuration, original.configuration);
    EXPECT_EQ(recovered.total_time_ms, original.total_time_ms);
    EXPECT_EQ(recovered.compiler_type, original.compiler_type);
}

TEST_F(UnifiedFormatTest, RoundTripComplexCompilationUnit) {
    auto original = create_complex_unit();

    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.template_instantiations.size(), original.template_instantiations.size());

    for (size_t i = 0; i < original.template_instantiations.size(); i++) {
        EXPECT_EQ(recovered.template_instantiations[i].template_name,
                 original.template_instantiations[i].template_name);
        EXPECT_EQ(recovered.template_instantiations[i].time_ms,
                 original.template_instantiations[i].time_ms);
    }
}

TEST_F(UnifiedFormatTest, RoundTripSimpleBuildTrace) {
    auto original = create_simple_trace();

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.trace_id, original.trace_id);
    EXPECT_EQ(recovered.build_system, original.build_system);
    EXPECT_EQ(recovered.configuration, original.configuration);
    EXPECT_EQ(recovered.compilation_units.size(), original.compilation_units.size());
}

TEST_F(UnifiedFormatTest, RoundTripComplexBuildTrace) {
    auto original = create_complex_trace();

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.trace_id, original.trace_id);
    EXPECT_EQ(recovered.compilation_units.size(), original.compilation_units.size());
    EXPECT_EQ(recovered.dependency_graph.node_count(), original.dependency_graph.node_count());
    EXPECT_EQ(recovered.dependency_graph.edge_count(), original.dependency_graph.edge_count());
}

TEST_F(UnifiedFormatTest, RoundTripLargeBuildTrace) {
    auto original = create_large_trace();

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.compilation_units.size(), original.compilation_units.size());
}

TEST_F(UnifiedFormatTest, RoundTripPartialBuildTrace) {
    auto original = create_partial_trace();

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.trace_id, original.trace_id);
}

TEST_F(UnifiedFormatTest, HandleMissingOptionalFields) {
    CompilationUnit unit;
    unit.id = "minimal_unit";
    unit.file_path = "test.cpp";

    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(unit);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());
}

TEST_F(UnifiedFormatTest, SaveBuildTraceToFile) {
    const auto trace = create_simple_trace();
    const fs::path file_path = temp_dir / "trace.json";

    const auto result = UnifiedFormatSerializer::save_to_file(trace, file_path.string());
    EXPECT_TRUE(result.is_success());
    EXPECT_TRUE(fs::exists(file_path));
}

TEST_F(UnifiedFormatTest, LoadBuildTraceFromFile) {
    auto original = create_simple_trace();
    fs::path file_path = temp_dir / "trace.json";

    auto save_result = UnifiedFormatSerializer::save_to_file(original, file_path.string());
    EXPECT_TRUE(save_result.is_success());

    auto load_result = UnifiedFormatSerializer::load_from_file(file_path.string());
    EXPECT_TRUE(load_result.is_success());

    auto recovered = std::move(load_result).value();
    EXPECT_EQ(recovered.trace_id, original.trace_id);
}

TEST_F(UnifiedFormatTest, RoundTripFileIO) {
    auto original = create_complex_trace();
    fs::path file_path = temp_dir / "complex_trace.json";

    auto save_result = UnifiedFormatSerializer::save_to_file(original, file_path.string());
    EXPECT_TRUE(save_result.is_success());

    auto load_result = UnifiedFormatSerializer::load_from_file(file_path.string());
    EXPECT_TRUE(load_result.is_success());

    auto recovered = std::move(load_result).value();
    EXPECT_EQ(recovered.trace_id, original.trace_id);
    EXPECT_EQ(recovered.compilation_units.size(), original.compilation_units.size());
}

TEST_F(UnifiedFormatTest, LoadNonexistentFile) {
    const fs::path nonexistent = temp_dir / "nonexistent.json";
    const auto result = UnifiedFormatSerializer::load_from_file(nonexistent.string());
    EXPECT_FALSE(result.is_success());
}

TEST_F(UnifiedFormatTest, PreserveAllCompilationUnitFields) {
    auto original = create_complex_unit();
    original.commit_sha = "abc123def456";
    original.file_size_bytes = 12345;
    original.preprocessed_size_bytes = 123456;

    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.file_size_bytes, original.file_size_bytes);
    EXPECT_EQ(recovered.preprocessed_size_bytes, original.preprocessed_size_bytes);
}

TEST_F(UnifiedFormatTest, PreserveCompilerFlags) {
    auto original = create_complex_unit();
    original.compile_flags = {"-O3", "-std=c++20", "-Wall", "-Wextra", "-Werror"};

    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.compile_flags.size(), original.compile_flags.size());
    for (size_t i = 0; i < original.compile_flags.size(); i++) {
        EXPECT_EQ(recovered.compile_flags[i], original.compile_flags[i]);
    }
}

TEST_F(UnifiedFormatTest, PreserveDependencyGraphStructure) {
    auto original = create_complex_trace();

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.dependency_graph.node_count(), original.dependency_graph.node_count());
    EXPECT_EQ(recovered.dependency_graph.edge_count(), original.dependency_graph.edge_count());
}

TEST_F(UnifiedFormatTest, PreserveTimingMetrics) {
    auto original = create_complex_trace();

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    for (size_t i = 0; i < original.compilation_units.size(); i++) {
        EXPECT_EQ(recovered.compilation_units[i].total_time_ms,
                 original.compilation_units[i].total_time_ms);
    }
}

TEST_F(UnifiedFormatTest, PreserveTemplateInstantiationDetails) {
    auto original = create_complex_unit();

    auto serialize_result = UnifiedFormatSerializer::serialize_compilation_unit(original);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_compilation_unit(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    for (size_t i = 0; i < original.template_instantiations.size(); i++) {
        EXPECT_EQ(recovered.template_instantiations[i].instantiation_depth,
                 original.template_instantiations[i].instantiation_depth);
        EXPECT_EQ(recovered.template_instantiations[i].call_stack.size(),
                 original.template_instantiations[i].call_stack.size());
    }
}

TEST_F(UnifiedFormatTest, ValidateSerializedJSON) {
    const auto trace = create_simple_trace();
    const auto result = UnifiedFormatSerializer::serialize_build_trace(trace);
    EXPECT_TRUE(result.is_success());

    const auto json = std::move(result).value();
    EXPECT_NE(json.find("{"), std::string::npos);
    EXPECT_NE(json.find("}"), std::string::npos);
}

TEST_F(UnifiedFormatTest, GetUnifiedFormatVersion) {
    auto version = UnifiedFormatSerializer::get_current_version();
    EXPECT_FALSE(version.empty());
    EXPECT_NE(version.find('.'), std::string::npos);
}

TEST_F(UnifiedFormatTest, ConsistentVersionInSerialization) {
    const auto trace = create_simple_trace();
    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(trace);
    EXPECT_TRUE(serialize_result.is_success());

    auto json = std::move(serialize_result).value();
    const auto version = UnifiedFormatSerializer::get_current_version();
    EXPECT_NE(json.find(version), std::string::npos);
}

TEST_F(UnifiedFormatTest, SerializeEmptyBuildTrace) {
    BuildTrace empty_trace;
    empty_trace.trace_id = "empty";

    const auto result = UnifiedFormatSerializer::serialize_build_trace(empty_trace);
    EXPECT_TRUE(result.is_success());
}

TEST_F(UnifiedFormatTest, DeserializeEmptyBuildTrace) {
    BuildTrace empty_trace;
    empty_trace.trace_id = "empty";

    auto serialize_result = UnifiedFormatSerializer::serialize_build_trace(empty_trace);
    EXPECT_TRUE(serialize_result.is_success());

    auto deserialize_result = UnifiedFormatSerializer::deserialize_build_trace(std::move(serialize_result).value());
    EXPECT_TRUE(deserialize_result.is_success());

    auto recovered = std::move(deserialize_result).value();
    EXPECT_EQ(recovered.trace_id, "empty");
    EXPECT_EQ(recovered.compilation_units.size(), 0);
}