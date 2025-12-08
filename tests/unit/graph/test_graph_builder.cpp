//
// Created by grego on 08/12/2025.
//

#include <gtest/gtest.h>
#include "bha/graph/graph_builder.h"

using namespace bha::graph;
using namespace bha::core;

class GraphBuilderTest : public ::testing::Test {
protected:
    static CompilationUnit create_simple_compilation_unit(const std::string& file_path) {
        CompilationUnit cu;
        cu.file_path = file_path;
        cu.id = "cu_" + file_path;
        cu.configuration = "Release";
        cu.compiler_type = "g++";
        cu.compiler_version = "11.0";
        cu.total_time_ms = 100.0;
        return cu;
    }

    static CompilationUnit create_compilation_unit_with_includes(
        const std::string& file_path,
        const std::vector<std::string>& includes) {
        CompilationUnit cu = create_simple_compilation_unit(file_path);
        cu.direct_includes = includes;
        return cu;
    }

    static CompilationUnit create_compilation_unit_with_timing(
        const std::string& file_path,
        const double total_time) {
        CompilationUnit cu = create_simple_compilation_unit(file_path);
        cu.total_time_ms = total_time;
        cu.preprocessing_time_ms = total_time * 0.3;
        cu.parsing_time_ms = total_time * 0.4;
        cu.codegen_time_ms = total_time * 0.2;
        cu.optimization_time_ms = total_time * 0.1;
        return cu;
    }
};

TEST_F(GraphBuilderTest, BuildFromEmptyCompilationUnits) {
    constexpr GraphBuilder builder;
    constexpr std::vector<CompilationUnit> empty_units;

    const auto result = builder.build_from_compilation_units(empty_units);
    ASSERT_TRUE(result.is_success());

    const auto& graph = result.value();
    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_EQ(graph.edge_count(), 0);
}

TEST_F(GraphBuilderTest, BuildFromSingleCompilationUnit) {
    constexpr GraphBuilder builder;

    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/src/main.cpp",
        {"header1.h", "header2.h"}
    );

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    EXPECT_GT(graph.node_count(), 0);
    EXPECT_TRUE(graph.has_node("/src/main.cpp"));
}

TEST_F(GraphBuilderTest, BuildFromMultipleCompilationUnits) {
    constexpr GraphBuilder builder;

    const CompilationUnit cu1 = create_compilation_unit_with_includes(
        "/src/file1.cpp",
        {"header1.h"}
    );

    const CompilationUnit cu2 = create_compilation_unit_with_includes(
        "/src/file2.cpp",
        {"header2.h"}
    );

    const std::vector units = {cu1, cu2};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    EXPECT_GE(graph.node_count(), 2);
    EXPECT_TRUE(graph.has_node("/src/file1.cpp"));
    EXPECT_TRUE(graph.has_node("/src/file2.cpp"));
}

TEST_F(GraphBuilderTest, BuildFromTrace) {
    constexpr GraphBuilder builder;

    BuildTrace trace;
    trace.trace_id = "trace_001";
    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/src/main.cpp",
        {"header.h"}
    );
    trace.compilation_units.push_back(cu);

    const auto result = builder.build_from_trace(trace);
    ASSERT_TRUE(result.is_success());

    const auto& graph = result.value();
    EXPECT_GT(graph.node_count(), 0);
}

TEST_F(GraphBuilderTest, SetMergeTransitive) {
    GraphBuilder builder;
    builder.set_merge_transitive(true);
    builder.set_merge_transitive(false);

    EXPECT_NO_THROW({
        builder.set_merge_transitive(true);
    });
}

TEST_F(GraphBuilderTest, SetIncludeSystemHeaders) {
    GraphBuilder builder;
    builder.set_include_system_headers(true);
    builder.set_include_system_headers(false);

    EXPECT_NO_THROW({
        builder.set_include_system_headers(true);
    });
}

TEST_F(GraphBuilderTest, SetWeightByCompileTime) {
    GraphBuilder builder;
    builder.set_weight_by_compile_time(true);
    builder.set_weight_by_compile_time(false);

    EXPECT_NO_THROW({
        builder.set_weight_by_compile_time(true);
    });
}

TEST_F(GraphBuilderTest, BuildWithSystemHeadersIncluded) {
    GraphBuilder builder;
    builder.set_include_system_headers(true);

    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/src/main.cpp",
        {"<vector>", "<iostream>", "my_header.h"}
    );

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();
    EXPECT_GT(graph.node_count(), 0);
}

TEST_F(GraphBuilderTest, BuildWithSystemHeadersExcluded) {
    GraphBuilder builder;
    builder.set_include_system_headers(false);

    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/src/main.cpp",
        {"/usr/include/vector", "my_header.h"}
    );

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    // System headers should be excluded
    EXPECT_FALSE(graph.has_node("/usr/include/vector"));
    EXPECT_TRUE(graph.has_node("my_header.h"));
}

TEST_F(GraphBuilderTest, BuildWithMergeTransitiveEnabled) {
    GraphBuilder builder;
    builder.set_merge_transitive(true);

    const CompilationUnit cu1 = create_compilation_unit_with_includes(
        "/src/file1.cpp",
        {"file1.h"}
    );

    const CompilationUnit cu2 = create_compilation_unit_with_includes(
        "/src/file2.cpp",
        {"file2.h"}
    );

    const std::vector units = {cu1, cu2};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
}

TEST_F(GraphBuilderTest, BuildWithMergeTransitiveDisabled) {
    GraphBuilder builder;
    builder.set_merge_transitive(false);

    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/src/main.cpp",
        {"header1.h", "header2.h"}
    );

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
}

TEST_F(GraphBuilderTest, BuildWithWeightingEnabled) {
    GraphBuilder builder;
    builder.set_weight_by_compile_time(true);

    CompilationUnit cu = create_compilation_unit_with_timing(
        "/src/main.cpp",
        500.0
    );
    cu.direct_includes = {"header.h"};

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
}

TEST_F(GraphBuilderTest, BuildWithMultipleIncludes) {
    constexpr GraphBuilder builder;

    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/src/complex.cpp",
        {"a.h", "b.h", "c.h", "d.h", "e.h"}
    );

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    EXPECT_TRUE(graph.has_node("/src/complex.cpp"));
    EXPECT_GE(graph.edge_count(), 5);
}

TEST_F(GraphBuilderTest, BuildPreservesCompilationUnitMetadata) {
    constexpr GraphBuilder builder;

    CompilationUnit cu = create_compilation_unit_with_timing(
        "/src/test.cpp",
        1234.5
    );
    cu.direct_includes = {"test.h"};

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    EXPECT_TRUE(graph.has_node("/src/test.cpp"));
    EXPECT_TRUE(graph.has_node("test.h"));
}

TEST_F(GraphBuilderTest, BuildFromComplexTrace) {
    constexpr GraphBuilder builder;

    BuildTrace trace;
    trace.trace_id = "complex_trace";
    trace.build_system = "cmake";
    trace.configuration = "Release";

    for (int i = 0; i < 5; ++i) {
        CompilationUnit cu = create_compilation_unit_with_includes(
            "/src/file_" + std::to_string(i) + ".cpp",
            {"header.h", "utils.h"}
        );
        trace.compilation_units.push_back(cu);
    }

    const auto result = builder.build_from_trace(trace);
    ASSERT_TRUE(result.is_success());

    const auto& graph = result.value();
    EXPECT_GE(graph.node_count(), 5);
}

TEST_F(GraphBuilderTest, BuildWithDifferentCompilers) {
    constexpr GraphBuilder builder;

    CompilationUnit cu1;
    cu1.file_path = "/src/file1.cpp";
    cu1.compiler_type = "g++";
    cu1.compiler_version = "11.0";
    cu1.direct_includes = {"a.h"};

    CompilationUnit cu2;
    cu2.file_path = "/src/file2.cpp";
    cu2.compiler_type = "clang++";
    cu2.compiler_version = "13.0";
    cu2.direct_includes = {"b.h"};

    const std::vector units = {cu1, cu2};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    EXPECT_TRUE(graph.has_node("/src/file1.cpp"));
    EXPECT_TRUE(graph.has_node("/src/file2.cpp"));
}

TEST_F(GraphBuilderTest, BuildHandlesEmptyIncludesList) {
    constexpr GraphBuilder builder;

    CompilationUnit cu = create_simple_compilation_unit("/src/standalone.cpp");
    cu.direct_includes = {};

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    EXPECT_TRUE(graph.has_node("/src/standalone.cpp"));
    EXPECT_EQ(graph.edge_count(), 0);
}

TEST_F(GraphBuilderTest, BuildHandleDuplicateIncludes) {
    constexpr GraphBuilder builder;

    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/src/main.cpp",
        {"header.h", "header.h", "other.h", "header.h"}
    );

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    // Should handle duplicates correctly
    EXPECT_TRUE(graph.has_node("/src/main.cpp"));
}

TEST_F(GraphBuilderTest, BuildWithLargeNumberOfCompilationUnits) {
    constexpr GraphBuilder builder;

    std::vector<CompilationUnit> units;
    for (int i = 0; i < 100; ++i) {
        CompilationUnit cu = create_compilation_unit_with_includes(
            "/src/file_" + std::to_string(i) + ".cpp",
            {"common.h"}
        );
        units.push_back(cu);
    }

    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    EXPECT_GE(graph.node_count(), 100);
}

TEST_F(GraphBuilderTest, BuildWithNestedPaths) {
    constexpr GraphBuilder builder;

    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/project/src/deep/nested/file.cpp",
        {"/project/include/public/header.h", "/project/include/private/impl.h"}
    );

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    EXPECT_TRUE(graph.has_node("/project/src/deep/nested/file.cpp"));
}

TEST_F(GraphBuilderTest, BuildWithRelativePaths) {
    constexpr GraphBuilder builder;

    CompilationUnit cu;
    cu.file_path = "src/main.cpp";
    cu.direct_includes = {"../include/header.h", "utils.h"};

    const std::vector units = {cu};
    const auto result = builder.build_from_compilation_units(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();

    EXPECT_TRUE(graph.has_node("src/main.cpp"));
}

TEST_F(GraphBuilderTest, ExtractCompileTimes) {
    const CompilationUnit cu1 = create_compilation_unit_with_timing("/src/file1.cpp", 1000.0);
    const CompilationUnit cu2 = create_compilation_unit_with_timing("/src/file2.cpp", 2000.0);
    const CompilationUnit cu3 = create_compilation_unit_with_timing("/src/file3.cpp", 500.0);

    const std::vector units = {cu1, cu2, cu3};
    const auto times = extract_compile_times(units);

    EXPECT_EQ(times.size(), 3);
    EXPECT_EQ(times.at("/src/file1.cpp"), 1000.0);
    EXPECT_EQ(times.at("/src/file2.cpp"), 2000.0);
    EXPECT_EQ(times.at("/src/file3.cpp"), 500.0);
}

TEST_F(GraphBuilderTest, ExtractCompileTimesFromEmpty) {
    constexpr std::vector<CompilationUnit> units;
    const auto times = extract_compile_times(units);

    EXPECT_EQ(times.size(), 0);
}

TEST_F(GraphBuilderTest, MergeGraphsBasic) {
    DependencyGraph target;
    target.add_edge("file1.cpp", "header1.h");

    DependencyGraph source;
    source.add_edge("file2.cpp", "header2.h");

    merge_graphs(target, source);

    EXPECT_TRUE(target.has_node("file1.cpp"));
    EXPECT_TRUE(target.has_node("file2.cpp"));
    EXPECT_TRUE(target.has_edge("file1.cpp", "header1.h"));
    EXPECT_TRUE(target.has_edge("file2.cpp", "header2.h"));
}

TEST_F(GraphBuilderTest, MergeGraphsWithOverlappingNodes) {
    DependencyGraph target;
    target.add_edge("shared.h", "common.h");
    target.add_edge("file1.cpp", "shared.h");

    DependencyGraph source;
    source.add_edge("file2.cpp", "shared.h");
    source.add_edge("shared.h", "other.h");

    merge_graphs(target, source);

    EXPECT_TRUE(target.has_node("file1.cpp"));
    EXPECT_TRUE(target.has_node("file2.cpp"));
    EXPECT_TRUE(target.has_node("shared.h"));
    EXPECT_TRUE(target.has_edge("file1.cpp", "shared.h"));
    EXPECT_TRUE(target.has_edge("file2.cpp", "shared.h"));
}

TEST_F(GraphBuilderTest, MergeEmptyGraphs) {
    DependencyGraph target;
    target.add_node("test");

    const DependencyGraph source;

    merge_graphs(target, source);

    EXPECT_TRUE(target.has_node("test"));
    EXPECT_EQ(target.node_count(), 1);
}

TEST_F(GraphBuilderTest, MergeIntoEmptyGraph) {
    DependencyGraph target;

    DependencyGraph source;
    source.add_edge("a.cpp", "b.h");

    merge_graphs(target, source);

    EXPECT_TRUE(target.has_node("a.cpp"));
    EXPECT_TRUE(target.has_node("b.h"));
    EXPECT_TRUE(target.has_edge("a.cpp", "b.h"));
}

TEST_F(GraphBuilderTest, MergeGraphsPreservesEdges) {
    DependencyGraph target;
    target.add_edge("A", "B");
    target.add_edge("B", "C");

    DependencyGraph source;
    source.add_edge("D", "E");
    source.add_edge("E", "F");

    const size_t initial_edge_count = target.edge_count();
    merge_graphs(target, source);

    EXPECT_EQ(target.edge_count(), initial_edge_count + source.edge_count());
}

TEST_F(GraphBuilderTest, MergeComplexGraphs) {
    DependencyGraph target;
    for (int i = 0; i < 10; ++i) {
        for (int j = i + 1; j < 10; ++j) {
            target.add_edge("file_" + std::to_string(i) + ".cpp",
                          "header_" + std::to_string(j) + ".h");
        }
    }

    DependencyGraph source;
    for (int i = 10; i < 20; ++i) {
        for (int j = i + 1; j < 20; ++j) {
            source.add_edge("file_" + std::to_string(i) + ".cpp",
                          "header_" + std::to_string(j) + ".h");
        }
    }

    const size_t combined_nodes = target.node_count() + source.node_count();
    merge_graphs(target, source);

    EXPECT_GE(target.node_count(), combined_nodes - 10);  // Account for potential overlaps
}

TEST_F(GraphBuilderTest, BuildDependencyGraphFromUnits) {
    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/src/test.cpp",
        {"header.h"}
    );

    const std::vector units = {cu};
    const auto result = build_dependency_graph(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();
    EXPECT_GT(graph.node_count(), 0);
}

TEST_F(GraphBuilderTest, BuildDependencyGraphFromTrace) {
    BuildTrace trace;
    const CompilationUnit cu = create_compilation_unit_with_includes(
        "/src/main.cpp",
        {"header.h"}
    );
    trace.compilation_units.push_back(cu);

    const auto result = build_dependency_graph(trace);
    ASSERT_TRUE(result.is_success());

    const auto& graph = result.value();
    EXPECT_GT(graph.node_count(), 0);
}

TEST_F(GraphBuilderTest, BuildDependencyGraphFromEmptyUnits) {
    constexpr std::vector<CompilationUnit> units;
    const auto result = build_dependency_graph(units);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();
    EXPECT_EQ(graph.node_count(), 0);
}

TEST_F(GraphBuilderTest, BuildDependencyGraphFromEmptyTrace) {
    const BuildTrace trace;
    const auto result = build_dependency_graph(trace);

    ASSERT_TRUE(result.is_success());
    const auto& graph = result.value();
    EXPECT_EQ(graph.node_count(), 0);
}