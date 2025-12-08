//
// Created by gregorian on 08/12/2025.
//

#include <gtest/gtest.h>
#include "bha/core/types.h"

using namespace bha::core;

TEST(TemplateInstantiationTest, DefaultConstruct) {
    const TemplateInstantiation ti;
    EXPECT_TRUE(ti.template_name.empty());
    EXPECT_TRUE(ti.instantiation_context.empty());
    EXPECT_EQ(ti.time_ms, 0.0);
    EXPECT_EQ(ti.instantiation_depth, 0);
    EXPECT_TRUE(ti.call_stack.empty());
}

TEST(TemplateInstantiationTest, PopulateData) {
    TemplateInstantiation ti;
    ti.template_name = "std::vector<int>";
    ti.instantiation_context = "main.cpp:42";
    ti.time_ms = 123.45;
    ti.instantiation_depth = 3;
    ti.call_stack = {"vector", "allocator", "traits"};

    EXPECT_EQ(ti.template_name, "std::vector<int>");
    EXPECT_EQ(ti.time_ms, 123.45);
    EXPECT_EQ(ti.instantiation_depth, 3);
    ASSERT_EQ(ti.call_stack.size(), 3);
}

TEST(CompilationUnitTest, DefaultConstruct) {
    const CompilationUnit cu;
    EXPECT_TRUE(cu.id.empty());
    EXPECT_TRUE(cu.file_path.empty());
    EXPECT_EQ(cu.total_time_ms, 0.0);
    EXPECT_EQ(cu.preprocessing_time_ms, 0.0);
    EXPECT_EQ(cu.parsing_time_ms, 0.0);
    EXPECT_EQ(cu.codegen_time_ms, 0.0);
    EXPECT_EQ(cu.optimization_time_ms, 0.0);
    EXPECT_TRUE(cu.compiler_type.empty());
    EXPECT_TRUE(cu.direct_includes.empty());
    EXPECT_TRUE(cu.all_includes.empty());
    EXPECT_TRUE(cu.template_instantiations.empty());
    EXPECT_EQ(cu.file_size_bytes, 0);
    EXPECT_EQ(cu.preprocessed_size_bytes, 0);
}

TEST(CompilationUnitTest, PopulateFullData) {
    CompilationUnit cu;
    cu.id = "cu_001";
    cu.file_path = "/src/main.cpp";
    cu.configuration = "Release";
    cu.total_time_ms = 1000.0;
    cu.preprocessing_time_ms = 200.0;
    cu.parsing_time_ms = 500.0;
    cu.codegen_time_ms = 200.0;
    cu.optimization_time_ms = 100.0;
    cu.compiler_type = "clang";
    cu.compiler_version = "15.0.0";
    cu.compile_flags = {"-std=c++20", "-O3"};
    cu.direct_includes = {"header1.h", "header2.h"};
    cu.all_includes = {"header1.h", "header2.h", "system.h"};
    cu.commit_sha = "abc123";
    cu.file_size_bytes = 10240;
    cu.preprocessed_size_bytes = 51200;

    EXPECT_EQ(cu.id, "cu_001");
    EXPECT_EQ(cu.file_path, "/src/main.cpp");
    EXPECT_EQ(cu.total_time_ms, 1000.0);
    EXPECT_EQ(cu.compiler_type, "clang");
    ASSERT_EQ(cu.compile_flags.size(), 2);
    ASSERT_EQ(cu.direct_includes.size(), 2);
    ASSERT_EQ(cu.all_includes.size(), 3);
}

TEST(EdgeTypeTest, ToString) {
    EXPECT_EQ(to_string(EdgeType::DIRECT_INCLUDE), "DIRECT_INCLUDE");
    EXPECT_EQ(to_string(EdgeType::TRANSITIVE), "TRANSITIVE");
    EXPECT_EQ(to_string(EdgeType::PCH_REFERENCE), "PCH_REFERENCE");
}

TEST(EdgeTypeTest, FromString) {
    EXPECT_EQ(edge_type_from_string("DIRECT_INCLUDE"), EdgeType::DIRECT_INCLUDE);
    EXPECT_EQ(edge_type_from_string("TRANSITIVE"), EdgeType::TRANSITIVE);
    EXPECT_EQ(edge_type_from_string("PCH_REFERENCE"), EdgeType::PCH_REFERENCE);
}

TEST(DependencyEdgeTest, DefaultConstruct) {
    const DependencyEdge edge;
    EXPECT_TRUE(edge.target.empty());
    EXPECT_EQ(edge.type, EdgeType::DIRECT_INCLUDE);
    EXPECT_EQ(edge.line_number, 0);
    EXPECT_FALSE(edge.is_system_header);
    EXPECT_EQ(edge.weight, 0.0);
}

TEST(DependencyEdgeTest, ConstructWithTarget) {
    const DependencyEdge edge("header.h");
    EXPECT_EQ(edge.target, "header.h");
    EXPECT_EQ(edge.type, EdgeType::DIRECT_INCLUDE);
}

TEST(DependencyEdgeTest, ConstructWithTargetAndType) {
    const DependencyEdge edge("system.h", EdgeType::TRANSITIVE);
    EXPECT_EQ(edge.target, "system.h");
    EXPECT_EQ(edge.type, EdgeType::TRANSITIVE);
}

TEST(DependencyGraphTest, DefaultConstruct) {
    const DependencyGraph graph;
    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_EQ(graph.edge_count(), 0);
    EXPECT_TRUE(graph.get_all_nodes().empty());
}

TEST(DependencyGraphTest, AddNode) {
    DependencyGraph graph;
    graph.add_node("file1.cpp");
    graph.add_node("file2.cpp");

    EXPECT_EQ(graph.node_count(), 2);
    EXPECT_TRUE(graph.has_node("file1.cpp"));
    EXPECT_TRUE(graph.has_node("file2.cpp"));
    EXPECT_FALSE(graph.has_node("file3.cpp"));
}

TEST(DependencyGraphTest, AddDuplicateNode) {
    DependencyGraph graph;
    graph.add_node("file.cpp");
    graph.add_node("file.cpp");
    EXPECT_EQ(graph.node_count(), 1);
}

TEST(DependencyGraphTest, AddEdge) {
    DependencyGraph graph;
    graph.add_edge("main.cpp", "header.h");

    EXPECT_TRUE(graph.has_node("main.cpp"));
    EXPECT_TRUE(graph.has_node("header.h"));
    EXPECT_TRUE(graph.has_edge("main.cpp", "header.h"));
    EXPECT_FALSE(graph.has_edge("header.h", "main.cpp"));
    EXPECT_EQ(graph.edge_count(), 1);
}

TEST(DependencyGraphTest, AddEdgeWithType) {
    DependencyGraph graph;
    graph.add_edge("file1.cpp", "file2.h", EdgeType::TRANSITIVE);

    EXPECT_TRUE(graph.has_edge("file1.cpp", "file2.h"));
    const auto edges = graph.get_edges("file1.cpp");
    ASSERT_EQ(edges.size(), 1);
    EXPECT_EQ(edges[0].target, "file2.h");
    EXPECT_EQ(edges[0].type, EdgeType::TRANSITIVE);
}

TEST(DependencyGraphTest, AddEdgeWithObject) {
    DependencyGraph graph;
    DependencyEdge edge("target.h", EdgeType::PCH_REFERENCE);
    edge.line_number = 42;
    edge.is_system_header = true;
    edge.weight = 1.5;

    graph.add_edge("source.cpp", edge);

    const auto edges = graph.get_edges("source.cpp");
    ASSERT_EQ(edges.size(), 1);
    EXPECT_EQ(edges[0].target, "target.h");
    EXPECT_EQ(edges[0].type, EdgeType::PCH_REFERENCE);
    EXPECT_EQ(edges[0].line_number, 42);
    EXPECT_TRUE(edges[0].is_system_header);
    EXPECT_EQ(edges[0].weight, 1.5);
}

TEST(DependencyGraphTest, GetDependencies) {
    DependencyGraph graph;
    graph.add_edge("main.cpp", "header1.h");
    graph.add_edge("main.cpp", "header2.h");
    graph.add_edge("main.cpp", "header3.h");

    auto deps = graph.get_dependencies("main.cpp");
    ASSERT_EQ(deps.size(), 3);
    EXPECT_TRUE(std::ranges::find(deps.begin(), deps.end(), "header1.h") != deps.end());
    EXPECT_TRUE(std::ranges::find(deps.begin(), deps.end(), "header2.h") != deps.end());
    EXPECT_TRUE(std::ranges::find(deps.begin(), deps.end(), "header3.h") != deps.end());
}

TEST(DependencyGraphTest, GetReverseDependencies) {
    DependencyGraph graph;
    graph.add_edge("file1.cpp", "common.h");
    graph.add_edge("file2.cpp", "common.h");
    graph.add_edge("file3.cpp", "common.h");

    auto reverse_deps = graph.get_reverse_dependencies("common.h");
    ASSERT_EQ(reverse_deps.size(), 3);
    EXPECT_TRUE(std::ranges::find(reverse_deps.begin(), reverse_deps.end(), "file1.cpp") != reverse_deps.end());
    EXPECT_TRUE(std::ranges::find(reverse_deps.begin(), reverse_deps.end(), "file2.cpp") != reverse_deps.end());
    EXPECT_TRUE(std::ranges::find(reverse_deps.begin(), reverse_deps.end(), "file3.cpp") != reverse_deps.end());
}

TEST(DependencyGraphTest, GetEdges) {
    DependencyGraph graph;
    graph.add_edge("source.cpp", "header1.h");
    graph.add_edge("source.cpp", "header2.h", EdgeType::TRANSITIVE);

    const auto edges = graph.get_edges("source.cpp");
    ASSERT_EQ(edges.size(), 2);
}

TEST(DependencyGraphTest, GetAllNodes) {
    DependencyGraph graph;
    graph.add_node("file1.cpp");
    graph.add_node("file2.cpp");
    graph.add_node("file3.h");

    const auto nodes = graph.get_all_nodes();
    ASSERT_EQ(nodes.size(), 3);
    EXPECT_TRUE(std::ranges::find(nodes.begin(), nodes.end(), "file1.cpp") != nodes.end());
    EXPECT_TRUE(std::ranges::find(nodes.begin(), nodes.end(), "file2.cpp") != nodes.end());
    EXPECT_TRUE(std::ranges::find(nodes.begin(), nodes.end(), "file3.h") != nodes.end());
}

TEST(DependencyGraphTest, Clear) {
    DependencyGraph graph;
    graph.add_edge("file1.cpp", "file2.h");
    graph.add_edge("file2.cpp", "file3.h");

    EXPECT_GT(graph.node_count(), 0);
    EXPECT_GT(graph.edge_count(), 0);

    graph.clear();

    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_EQ(graph.edge_count(), 0);
    EXPECT_TRUE(graph.get_all_nodes().empty());
}

TEST(SuggestionTypeTest, ToString) {
    EXPECT_EQ(to_string(SuggestionType::FORWARD_DECLARATION), "FORWARD_DECLARATION");
    EXPECT_EQ(to_string(SuggestionType::HEADER_SPLIT), "HEADER_SPLIT");
    EXPECT_EQ(to_string(SuggestionType::PIMPL_PATTERN), "PIMPL_PATTERN");
    EXPECT_EQ(to_string(SuggestionType::PCH_ADDITION), "PCH_ADDITION");
    EXPECT_EQ(to_string(SuggestionType::PCH_REMOVAL), "PCH_REMOVAL");
    EXPECT_EQ(to_string(SuggestionType::INCLUDE_REMOVAL), "INCLUDE_REMOVAL");
    EXPECT_EQ(to_string(SuggestionType::MOVE_TO_CPP), "MOVE_TO_CPP");
    EXPECT_EQ(to_string(SuggestionType::EXPLICIT_TEMPLATE_INSTANTIATION), "EXPLICIT_TEMPLATE_INSTANTIATION");
}

TEST(SuggestionTypeTest, FromString) {
    EXPECT_EQ(suggestion_type_from_string("FORWARD_DECLARATION"), SuggestionType::FORWARD_DECLARATION);
    EXPECT_EQ(suggestion_type_from_string("HEADER_SPLIT"), SuggestionType::HEADER_SPLIT);
    EXPECT_EQ(suggestion_type_from_string("PIMPL_PATTERN"), SuggestionType::PIMPL_PATTERN);
}

TEST(PriorityTest, ToString) {
    EXPECT_EQ(to_string(Priority::CRITICAL), "CRITICAL");
    EXPECT_EQ(to_string(Priority::HIGH), "HIGH");
    EXPECT_EQ(to_string(Priority::MEDIUM), "MEDIUM");
    EXPECT_EQ(to_string(Priority::LOW), "LOW");
}

TEST(PriorityTest, FromString) {
    EXPECT_EQ(priority_from_string("CRITICAL"), Priority::CRITICAL);
    EXPECT_EQ(priority_from_string("HIGH"), Priority::HIGH);
    EXPECT_EQ(priority_from_string("MEDIUM"), Priority::MEDIUM);
    EXPECT_EQ(priority_from_string("LOW"), Priority::LOW);
}

TEST(ChangeTypeTest, ToString) {
    EXPECT_EQ(to_string(ChangeType::ADD), "ADD");
    EXPECT_EQ(to_string(ChangeType::REMOVE), "REMOVE");
    EXPECT_EQ(to_string(ChangeType::REPLACE), "REPLACE");
}

TEST(ChangeTypeTest, FromString) {
    EXPECT_EQ(change_type_from_string("ADD"), ChangeType::ADD);
    EXPECT_EQ(change_type_from_string("REMOVE"), ChangeType::REMOVE);
    EXPECT_EQ(change_type_from_string("REPLACE"), ChangeType::REPLACE);
}

TEST(HotspotTest, Creation) {
    Hotspot h;
    h.file_path = "slow_file.cpp";
    h.time_ms = 5000.0;
    h.impact_score = 95.5;
    h.num_dependent_files = 150;
    h.category = "critical";

    EXPECT_EQ(h.file_path, "slow_file.cpp");
    EXPECT_EQ(h.time_ms, 5000.0);
    EXPECT_EQ(h.impact_score, 95.5);
    EXPECT_EQ(h.num_dependent_files, 150);
    EXPECT_EQ(h.category, "critical");
}

TEST(PCHMetricsTest, Creation) {
    PCHMetrics pch;
    pch.pch_file = "precompiled.pch";
    pch.pch_build_time_ms = 10000.0;
    pch.average_time_saved_per_file_ms = 500.0;
    pch.files_using_pch = 200;
    pch.total_time_saved_ms = 100000.0;
    pch.pch_hit_rate = 0.95;

    EXPECT_EQ(pch.pch_file, "precompiled.pch");
    EXPECT_EQ(pch.pch_build_time_ms, 10000.0);
    EXPECT_EQ(pch.files_using_pch, 200);
    EXPECT_EQ(pch.pch_hit_rate, 0.95);
}

TEST(MetricsSummaryTest, DefaultValues) {
    MetricsSummary ms;
    EXPECT_EQ(ms.total_files_compiled, 0);
    EXPECT_EQ(ms.total_headers_parsed, 0);
    EXPECT_EQ(ms.average_file_time_ms, 0.0);
    EXPECT_TRUE(ms.top_slow_files.empty());
    EXPECT_TRUE(ms.top_hot_headers.empty());
    EXPECT_TRUE(ms.critical_path.empty());
    EXPECT_TRUE(ms.expensive_templates.empty());
    EXPECT_FALSE(ms.pch_metrics.has_value());
}

TEST(BuildTraceTest, DefaultConstruct) {
    const BuildTrace trace;
    EXPECT_TRUE(trace.trace_id.empty());
    EXPECT_EQ(trace.total_build_time_ms, 0.0);
    EXPECT_TRUE(trace.build_system.empty());
    EXPECT_TRUE(trace.compilation_units.empty());
    EXPECT_TRUE(trace.is_clean_build);
    EXPECT_TRUE(trace.changed_files.empty());
    EXPECT_EQ(trace.dependency_graph.node_count(), 0);
}

TEST(SuggestionTest, FullPopulation) {
    Suggestion s;
    s.id = "sugg_001";
    s.type = SuggestionType::FORWARD_DECLARATION;
    s.priority = Priority::HIGH;
    s.confidence = 0.85;
    s.title = "Replace include with forward declaration";
    s.description = "Can use forward declaration instead of full include";
    s.file_path = "/src/file.h";
    s.related_files = {"file1.cpp", "file2.cpp"};
    s.estimated_time_savings_ms = 500.0;
    s.estimated_time_savings_percent = 5.0;
    s.is_safe = true;
    s.documentation_link = "https://docs.example.com/forward-decl";

    EXPECT_EQ(s.id, "sugg_001");
    EXPECT_EQ(s.type, SuggestionType::FORWARD_DECLARATION);
    EXPECT_EQ(s.priority, Priority::HIGH);
    EXPECT_EQ(s.confidence, 0.85);
    EXPECT_TRUE(s.is_safe);
    ASSERT_EQ(s.related_files.size(), 2);
}

TEST(CodeChangeTest, Creation) {
    CodeChange change;
    change.file_path = "/src/header.h";
    change.line_number = 42;
    change.before = "#include <vector>";
    change.after = "class vector;";
    change.type = ChangeType::REPLACE;

    EXPECT_EQ(change.file_path, "/src/header.h");
    EXPECT_EQ(change.line_number, 42);
    EXPECT_EQ(change.before, "#include <vector>");
    EXPECT_EQ(change.after, "class vector;");
    EXPECT_EQ(change.type, ChangeType::REPLACE);
}

TEST(ImpactReportTest, Creation) {
    ImpactReport report;
    report.affected_files = {"file1.cpp", "file2.cpp", "file3.cpp"};
    report.estimated_rebuild_time_ms = 15000.0;
    report.num_cascading_rebuilds = 3;
    report.fragile_headers = {"common.h", "types.h"};

    ASSERT_EQ(report.affected_files.size(), 3);
    EXPECT_EQ(report.estimated_rebuild_time_ms, 15000.0);
    EXPECT_EQ(report.num_cascading_rebuilds, 3);
    ASSERT_EQ(report.fragile_headers.size(), 2);
}

TEST(ComparisonReportTest, Creation) {
    ComparisonReport report;
    report.baseline_trace_id = "baseline_001";
    report.current_trace_id = "current_001";
    report.baseline_total_time_ms = 10000.0;
    report.current_total_time_ms = 11000.0;
    report.time_delta_ms = 1000.0;
    report.time_delta_percent = 10.0;
    report.is_regression = true;

    EXPECT_EQ(report.baseline_trace_id, "baseline_001");
    EXPECT_EQ(report.current_trace_id, "current_001");
    EXPECT_EQ(report.time_delta_ms, 1000.0);
    EXPECT_EQ(report.time_delta_percent, 10.0);
    EXPECT_TRUE(report.is_regression);
}