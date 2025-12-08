//
// Created by gregorian on 08/12/2025.
//

#include <gtest/gtest.h>
#include "bha/graph/graph_algorithms.h"

using namespace bha::graph;
using namespace bha::core;

class GraphAlgorithmsTest : public ::testing::Test {
protected:
    static DependencyGraph create_simple_graph() {
        DependencyGraph graph;
        graph.add_edge("A", "B");
        graph.add_edge("B", "C");
        graph.add_edge("C", "D");
        return graph;
    }

    static DependencyGraph create_dag() {
        DependencyGraph graph;
        graph.add_edge("A", "B");
        graph.add_edge("A", "C");
        graph.add_edge("B", "D");
        graph.add_edge("C", "D");
        return graph;
    }

    static DependencyGraph create_cyclic_graph() {
        DependencyGraph graph;
        graph.add_edge("A", "B");
        graph.add_edge("B", "C");
        graph.add_edge("C", "A");
        return graph;
    }

    static DependencyGraph create_complex_graph() {
        DependencyGraph graph;
        graph.add_edge("main.cpp", "utils.h");
        graph.add_edge("main.cpp", "config.h");
        graph.add_edge("utils.h", "types.h");
        graph.add_edge("config.h", "types.h");
        graph.add_edge("types.h", "common.h");
        return graph;
    }
};

TEST_F(GraphAlgorithmsTest, TopologicalSort_SimpleGraph) {
    const auto graph = create_simple_graph();
    const auto sorted = topological_sort(graph);

    EXPECT_FALSE(sorted.empty());
}

TEST_F(GraphAlgorithmsTest, TopologicalSort_DAG) {
    const auto graph = create_dag();
    const auto sorted = topological_sort(graph);

    EXPECT_FALSE(sorted.empty());
    EXPECT_GE(sorted.size(), 4);
}

TEST_F(GraphAlgorithmsTest, TopologicalSortChecked_ValidDAG) {
    auto graph = create_dag();
    auto result = topological_sort_checked(graph);

    ASSERT_TRUE(result.is_success());
    const auto& sorted = result.value();
    EXPECT_FALSE(sorted.empty());
}

TEST_F(GraphAlgorithmsTest, TopologicalSortChecked_CyclicGraph) {
    const auto graph = create_cyclic_graph();
    const auto result = topological_sort_checked(graph);

    EXPECT_TRUE(result.is_failure());
}

TEST_F(GraphAlgorithmsTest, HasCycle_DAG) {
    const auto graph = create_dag();
    EXPECT_FALSE(has_cycle(graph));
}

TEST_F(GraphAlgorithmsTest, HasCycle_CyclicGraph) {
    const auto graph = create_cyclic_graph();
    EXPECT_TRUE(has_cycle(graph));
}

TEST_F(GraphAlgorithmsTest, HasCycle_EmptyGraph) {
    const DependencyGraph graph;
    EXPECT_FALSE(has_cycle(graph));
}

TEST_F(GraphAlgorithmsTest, FindCycles_NoCycles) {
    const auto graph = create_dag();
    const auto cycles = find_cycles(graph);

    // DAG should have no cycles
    EXPECT_TRUE(cycles.empty());
}

TEST_F(GraphAlgorithmsTest, FindCycles_WithCycle) {
    const auto graph = create_cyclic_graph();
    const auto cycles = find_cycles(graph);

    EXPECT_FALSE(cycles.empty());
}

TEST_F(GraphAlgorithmsTest, StronglyConnectedComponents_DAG) {
    const auto graph = create_dag();
    const auto components = strongly_connected_components(graph);

    EXPECT_TRUE(components.empty());
}

TEST_F(GraphAlgorithmsTest, StronglyConnectedComponents_CyclicGraph) {
    const auto graph = create_cyclic_graph();
    const auto components = strongly_connected_components(graph);

    EXPECT_FALSE(components.empty());
}

TEST_F(GraphAlgorithmsTest, FindPath_PathExists) {
    const auto graph = create_simple_graph();
    const auto path = find_path(graph, "A", "D");

    EXPECT_FALSE(path.empty());
    EXPECT_EQ(path.front(), "A");
    EXPECT_EQ(path.back(), "D");
}

TEST_F(GraphAlgorithmsTest, FindPath_NoPath) {
    const auto graph = create_simple_graph();
    const auto path = find_path(graph, "D", "A");

    EXPECT_TRUE(path.empty());
}

TEST_F(GraphAlgorithmsTest, FindPath_SameNode) {
    const auto graph = create_simple_graph();
    const auto path = find_path(graph, "A", "A");

    EXPECT_FALSE(path.empty());
}

TEST_F(GraphAlgorithmsTest, FindLongestPath) {
    const auto graph = create_dag();
    const auto longest = find_longest_path(graph);

    EXPECT_FALSE(longest.empty());
}

TEST_F(GraphAlgorithmsTest, CalculateDepth_RootNode) {
    const auto graph = create_dag();
    const int depth = calculate_depth(graph, "A");

    EXPECT_GE(depth, 0);
}

TEST_F(GraphAlgorithmsTest, CalculateDepth_LeafNode) {
    const auto graph = create_dag();
    const int depth = calculate_depth(graph, "D");

    EXPECT_GE(depth, 0);
}

TEST_F(GraphAlgorithmsTest, CalculateMaxDepth) {
    const auto graph = create_dag();
    const int max_depth = calculate_max_depth(graph);

    EXPECT_GT(max_depth, 0);
}

TEST_F(GraphAlgorithmsTest, CalculateAllDepths) {
    const auto graph = create_dag();
    const auto depths = calculate_all_depths(graph);

    EXPECT_FALSE(depths.empty());
    EXPECT_EQ(depths.size(), graph.node_count());
}

TEST_F(GraphAlgorithmsTest, GetRootNodes) {
    const auto graph = create_dag();
    const auto roots = get_root_nodes(graph);

    EXPECT_FALSE(roots.empty());
    EXPECT_TRUE(std::ranges::find(roots.begin(), roots.end(), "A") != roots.end());
}

TEST_F(GraphAlgorithmsTest, GetLeafNodes) {
    const auto graph = create_dag();
    const auto leaves = get_leaf_nodes(graph);

    EXPECT_FALSE(leaves.empty());
    EXPECT_TRUE(std::ranges::find(leaves.begin(), leaves.end(), "D") != leaves.end());
}

TEST_F(GraphAlgorithmsTest, CalculateFanout) {
    const auto graph = create_dag();
    auto fanout = calculate_fanout(graph);

    EXPECT_FALSE(fanout.empty());
    EXPECT_EQ(fanout["A"], 2);
}

TEST_F(GraphAlgorithmsTest, CalculateFanin) {
    const auto graph = create_dag();
    auto fanin = calculate_fanin(graph);

    EXPECT_FALSE(fanin.empty());
    EXPECT_EQ(fanin["D"], 2);
}

TEST_F(GraphAlgorithmsTest, GetTransitiveDependencies) {
    const auto graph = create_simple_graph();
    const auto deps = get_transitive_dependencies(graph, "A");

    EXPECT_FALSE(deps.empty());
    EXPECT_GE(deps.size(), 1);
}

TEST_F(GraphAlgorithmsTest, GetTransitiveDependents) {
    const auto graph = create_simple_graph();
    const auto dependents = get_transitive_dependents(graph, "D");

    EXPECT_FALSE(dependents.empty());
}

TEST_F(GraphAlgorithmsTest, DFS_Traversal) {
    const auto graph = create_simple_graph();
    std::unordered_set<std::string> visited;
    std::vector<std::string> traversal_order;

    dfs(graph, "A", visited, [&](const std::string& node) {
        traversal_order.push_back(node);
    });

    EXPECT_FALSE(traversal_order.empty());
    EXPECT_EQ(traversal_order[0], "A");
}

TEST_F(GraphAlgorithmsTest, BFS_Traversal) {
    const auto graph = create_simple_graph();
    std::vector<std::pair<std::string, int>> traversal;

    bfs(graph, "A", [&](const std::string& node, int level) {
        traversal.emplace_back(node, level);
    });

    EXPECT_FALSE(traversal.empty());
    EXPECT_EQ(traversal[0].first, "A");
    EXPECT_EQ(traversal[0].second, 0);
}

TEST_F(GraphAlgorithmsTest, ReverseGraph) {
    const auto graph = create_simple_graph();
    const auto reversed = reverse_graph(graph);

    EXPECT_EQ(reversed.node_count(), graph.node_count());
    EXPECT_TRUE(reversed.has_edge("D", "C"));
    EXPECT_TRUE(reversed.has_edge("C", "B"));
    EXPECT_TRUE(reversed.has_edge("B", "A"));
}

TEST_F(GraphAlgorithmsTest, Subgraph) {
    const auto graph = create_complex_graph();
    const std::vector<std::string> nodes = {"main.cpp", "utils.h", "types.h"};

    const auto sub = subgraph(graph, nodes);

    EXPECT_LE(sub.node_count(), nodes.size());
}

TEST_F(GraphAlgorithmsTest, FindCriticalPath) {
    const auto graph = create_dag();
    const std::unordered_map<std::string, double> weights = {
        {"A", 100.0},
        {"B", 200.0},
        {"C", 150.0},
        {"D", 300.0}
    };

    const auto critical = find_critical_path(graph, weights);

    EXPECT_FALSE(critical.empty());
}

TEST_F(GraphAlgorithmsTest, IsDAG_ValidDAG) {
    const auto graph = create_dag();
    EXPECT_TRUE(is_dag(graph));
}

TEST_F(GraphAlgorithmsTest, IsDAG_CyclicGraph) {
    const auto graph = create_cyclic_graph();
    EXPECT_FALSE(is_dag(graph));
}

TEST_F(GraphAlgorithmsTest, CountPaths) {
    const auto graph = create_dag();
    const int count = count_paths(graph, "A", "D");

    EXPECT_GE(count, 0);
}

TEST_F(GraphAlgorithmsTest, EmptyGraph_Operations) {
    const DependencyGraph empty;

    EXPECT_TRUE(topological_sort(empty).empty());
    EXPECT_FALSE(has_cycle(empty));
    EXPECT_TRUE(find_cycles(empty).empty());
    EXPECT_TRUE(get_root_nodes(empty).empty());
    EXPECT_TRUE(get_leaf_nodes(empty).empty());
    EXPECT_TRUE(is_dag(empty));
}

TEST_F(GraphAlgorithmsTest, SingleNode_Operations) {
    DependencyGraph graph;
    graph.add_node("single");

    const auto sorted = topological_sort(graph);
    EXPECT_EQ(sorted.size(), 1);
    EXPECT_FALSE(has_cycle(graph));
    EXPECT_TRUE(is_dag(graph));

    const auto roots = get_root_nodes(graph);
    EXPECT_EQ(roots.size(), 1);

    const auto leaves = get_leaf_nodes(graph);
    EXPECT_EQ(leaves.size(), 1);
}