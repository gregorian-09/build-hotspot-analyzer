//
// Created by gregorian on 08/12/2025.
//

#include <gtest/gtest.h>
#include "bha/graph/compressed_graph.h"

using namespace bha::graph;
using namespace bha::core;

class CompressedGraphTest : public ::testing::Test {
protected:
    static DependencyGraph create_simple_graph() {
        DependencyGraph graph;
        graph.add_edge("main.cpp", "utils.h");
        graph.add_edge("utils.h", "types.h");
        graph.add_edge("main.cpp", "config.h");
        graph.add_edge("config.h", "types.h");
        return graph;
    }

    static DependencyGraph create_empty_graph() {
        return {};
    }

    static DependencyGraph create_single_node_graph() {
        DependencyGraph graph;
        graph.add_node("single.h");
        return graph;
    }

    static DependencyGraph create_fully_connected_graph() {
        DependencyGraph graph;
        for (const std::vector<std::string> nodes = {"A", "B", "C"}; const auto& from : nodes) {
            for (const auto& to : nodes) {
                if (from != to) {
                    graph.add_edge(from, to);
                }
            }
        }
        return graph;
    }

    static DependencyGraph create_large_graph() {
        DependencyGraph graph;
        // Create a graph with 100 nodes and various edges
        for (int i = 0; i < 100; ++i) {
            std::string from = "file_" + std::to_string(i) + ".cpp";
            for (int j = i + 1; j < i + 3 && j < 100; ++j) {
                std::string to = "file_" + std::to_string(j) + ".h";
                graph.add_edge(from, to);
            }
        }
        return graph;
    }

    static DependencyGraph create_complex_graph() {
        DependencyGraph graph;
        graph.add_edge("/src/main.cpp", "/include/app.h");
        graph.add_edge("/src/main.cpp", "/include/config.h");
        graph.add_edge("/src/app.cpp", "/include/app.h");
        graph.add_edge("/src/app.cpp", "/include/utils.h");
        graph.add_edge("/include/app.h", "/include/types.h");
        graph.add_edge("/include/config.h", "/include/constants.h");
        graph.add_edge("/include/utils.h", "/include/types.h");
        graph.add_edge("/include/types.h", "/include/common.h");
        return graph;
    }
};

TEST_F(CompressedGraphTest, DefaultConstruction) {
    const CompressedGraph graph;
    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_EQ(graph.edge_count(), 0);
}

TEST_F(CompressedGraphTest, AddNode) {
    CompressedGraph graph;
    const int id1 = graph.add_node("file1.h");
    const int id2 = graph.add_node("file2.h");

    EXPECT_NE(id1, id2);
    EXPECT_TRUE(graph.has_node(id1));
    EXPECT_TRUE(graph.has_node(id2));
    EXPECT_EQ(graph.node_count(), 2);
}

TEST_F(CompressedGraphTest, AddNodeIdempotent) {
    CompressedGraph graph;
    const int id1 = graph.add_node("same_file.h");
    const int id2 = graph.add_node("same_file.h");

    EXPECT_EQ(id1, id2);
    EXPECT_EQ(graph.node_count(), 1);
}

TEST_F(CompressedGraphTest, AddEdge) {
    CompressedGraph graph;
    const int id1 = graph.add_node("source.cpp");
    const int id2 = graph.add_node("target.h");

    graph.add_edge(id1, id2, 1.5);

    EXPECT_TRUE(graph.has_edge(id1, id2));
    EXPECT_EQ(graph.edge_count(), 1);
}

TEST_F(CompressedGraphTest, GetNeighbors) {
    CompressedGraph graph;
    const int id1 = graph.add_node("A");
    const int id2 = graph.add_node("B");
    const int id3 = graph.add_node("C");

    graph.add_edge(id1, id2);
    graph.add_edge(id1, id3);

    auto neighbors = graph.get_neighbors(id1);
    EXPECT_EQ(neighbors.size(), 2);
    EXPECT_TRUE(std::ranges::find(neighbors.begin(), neighbors.end(), id2) != neighbors.end());
    EXPECT_TRUE(std::ranges::find(neighbors.begin(), neighbors.end(), id3) != neighbors.end());
}

TEST_F(CompressedGraphTest, GetReverseNeighbors) {
    CompressedGraph graph;
    const int id1 = graph.add_node("A");
    const int id2 = graph.add_node("B");
    const int id3 = graph.add_node("C");

    graph.add_edge(id1, id3);
    graph.add_edge(id2, id3);

    auto rev_neighbors = graph.get_reverse_neighbors(id3);
    EXPECT_EQ(rev_neighbors.size(), 2);
    EXPECT_TRUE(std::ranges::find(rev_neighbors.begin(), rev_neighbors.end(), id1) != rev_neighbors.end());
    EXPECT_TRUE(std::ranges::find(rev_neighbors.begin(), rev_neighbors.end(), id2) != rev_neighbors.end());
}

TEST_F(CompressedGraphTest, GetPathMapping) {
    CompressedGraph graph;
    const int id = graph.add_node("test_file.h");

    EXPECT_EQ(graph.get_path(id), "test_file.h");
    EXPECT_EQ(graph.get_id("test_file.h"), id);
}

TEST_F(CompressedGraphTest, GetIdReturnsMinusOneForInvalidPath) {
    const CompressedGraph graph;
    EXPECT_EQ(graph.get_id("nonexistent.h"), -1);
}

TEST_F(CompressedGraphTest, GetPathReturnsEmptyForInvalidId) {
    const CompressedGraph graph;
    EXPECT_EQ(graph.get_path(999), "");
}

TEST_F(CompressedGraphTest, ConstructFromDependencyGraph) {
    const auto dep_graph = create_simple_graph();
    const CompressedGraph graph(dep_graph);

    EXPECT_EQ(graph.node_count(), dep_graph.node_count());
    EXPECT_EQ(graph.edge_count(), dep_graph.edge_count());
}

TEST_F(CompressedGraphTest, CompressEmptyGraph) {
    const auto dep_graph = create_empty_graph();
    const CompressedGraph graph(dep_graph);

    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_EQ(graph.edge_count(), 0);
}

TEST_F(CompressedGraphTest, CompressSingleNodeGraph) {
    const auto dep_graph = create_single_node_graph();
    const CompressedGraph graph(dep_graph);

    EXPECT_EQ(graph.node_count(), 1);
    EXPECT_EQ(graph.edge_count(), 0);
    EXPECT_TRUE(graph.has_node(0));
}

TEST_F(CompressedGraphTest, DecompressPreservesStructure) {
    auto original = create_complex_graph();
    CompressedGraph compressed(original);
    auto decompressed = compressed.to_dependency_graph();

    EXPECT_EQ(decompressed.node_count(), original.node_count());
    EXPECT_EQ(decompressed.edge_count(), original.edge_count());

    // Verify all edges are preserved
    for (const auto& node : original.get_all_nodes()) {
        auto orig_deps = original.get_dependencies(node);
        auto decomp_deps = decompressed.get_dependencies(node);
        EXPECT_EQ(orig_deps.size(), decomp_deps.size());
    }
}

TEST_F(CompressedGraphTest, PreservesEdgeWeights) {
    CompressedGraph graph;
    const int id1 = graph.add_node("A.cpp");
    const int id2 = graph.add_node("B.h");
    const int id3 = graph.add_node("C.h");

    graph.add_edge(id1, id2, 10.5);
    graph.add_edge(id1, id3, 20.3);

    const auto neighbors = graph.get_neighbors(id1);
    EXPECT_EQ(neighbors.size(), 2);
    EXPECT_TRUE(graph.has_edge(id1, id2));
    EXPECT_TRUE(graph.has_edge(id1, id3));
}

TEST_F(CompressedGraphTest, MemoryUsageBytes) {
    CompressedGraph graph;
    for (int i = 0; i < 10; ++i) {
        graph.add_node("file_" + std::to_string(i) + ".h");
    }

    const size_t memory = graph.memory_usage_bytes();
    EXPECT_GT(memory, 0);
}

TEST_F(CompressedGraphTest, MemoryEfficiencyLargeGraph) {
    const auto original = create_large_graph();
    const CompressedGraph compressed(original);

    const size_t compressed_memory = compressed.memory_usage_bytes();
    EXPECT_GT(compressed_memory, 0);

    // Compressed graph should use reasonable amount of memory
    EXPECT_LT(compressed_memory, 1000000);  // Less than 1MB for 100 nodes
}

TEST_F(CompressedGraphTest, FullyConnectedGraph) {
    auto original = create_fully_connected_graph();
    CompressedGraph compressed(original);

    EXPECT_EQ(compressed.node_count(), 3);
    EXPECT_EQ(compressed.edge_count(), 6);  // 3 * 2 directed edges

    auto decompressed = compressed.to_dependency_graph();
    EXPECT_EQ(decompressed.node_count(), 3);
    EXPECT_EQ(decompressed.edge_count(), 6);
}

TEST_F(CompressedGraphTest, ClearGraph) {
    CompressedGraph graph;
    graph.add_node("A");
    graph.add_node("B");
    graph.add_edge(0, 1);

    EXPECT_GT(graph.node_count(), 0);

    graph.clear();

    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_EQ(graph.edge_count(), 0);
}

TEST_F(CompressedGraphTest, MultipleEdgesBetweenNodes) {
    CompressedGraph graph;
    const int id1 = graph.add_node("source.cpp");
    const int id2 = graph.add_node("target.h");

    graph.add_edge(id1, id2, 1.0);
    graph.add_edge(id1, id2, 2.0);

    EXPECT_TRUE(graph.has_edge(id1, id2));
}

TEST_F(CompressedGraphTest, RoundTripCompression) {
    auto original = create_complex_graph();

    CompressedGraph compressed(original);
    auto decompressed = compressed.to_dependency_graph();
    CompressedGraph recompressed(decompressed);

    EXPECT_EQ(recompressed.node_count(), original.node_count());
    EXPECT_EQ(recompressed.edge_count(), original.edge_count());
}

TEST_F(CompressedGraphTest, PathNormalization) {
    CompressedGraph graph;
    const int id1 = graph.add_node("/absolute/path/file.h");
    const int id2 = graph.add_node("relative/path/file.cpp");

    EXPECT_EQ(graph.get_path(id1), "/absolute/path/file.h");
    EXPECT_EQ(graph.get_path(id2), "relative/path/file.cpp");
}

TEST_F(CompressedGraphTest, EmptyGraphOperations) {
    CompressedGraph graph;

    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_EQ(graph.edge_count(), 0);
    EXPECT_EQ(graph.memory_usage_bytes(), 0);

    auto decomp = graph.to_dependency_graph();
    EXPECT_EQ(decomp.node_count(), 0);
    EXPECT_EQ(decomp.edge_count(), 0);
}

TEST_F(CompressedGraphTest, EstimateMemorySavings) {
    const auto original = create_complex_graph();
    const CompressedGraph compressed(original);

    const size_t savings = estimate_memory_savings(original, compressed);
    // Should be non-negative
    EXPECT_GE(savings, 0);
}

TEST_F(CompressedGraphTest, NodeCountAfterOperations) {
    CompressedGraph graph;

    for (int i = 0; i < 50; ++i) {
        graph.add_node("node_" + std::to_string(i));
    }

    EXPECT_EQ(graph.node_count(), 50);

    for (int i = 0; i < 49; ++i) {
        graph.add_edge(i, i + 1);
    }

    EXPECT_EQ(graph.edge_count(), 49);
}

TEST_F(CompressedGraphTest, EdgeCountAfterOperations) {
    CompressedGraph graph;
    const int id1 = graph.add_node("A");
    const int id2 = graph.add_node("B");
    const int id3 = graph.add_node("C");

    EXPECT_EQ(graph.edge_count(), 0);

    graph.add_edge(id1, id2);
    EXPECT_EQ(graph.edge_count(), 1);

    graph.add_edge(id2, id3);
    EXPECT_EQ(graph.edge_count(), 2);

    graph.add_edge(id1, id3);
    EXPECT_EQ(graph.edge_count(), 3);
}

TEST_F(CompressedGraphTest, HasEdgeAfterAddition) {
    CompressedGraph graph;
    const int id1 = graph.add_node("from");
    const int id2 = graph.add_node("to");

    EXPECT_FALSE(graph.has_edge(id1, id2));

    graph.add_edge(id1, id2);

    EXPECT_TRUE(graph.has_edge(id1, id2));
}

TEST_F(CompressedGraphTest, NodesNotConnectedByDefault) {
    CompressedGraph graph;
    const int id1 = graph.add_node("A");
    const int id2 = graph.add_node("B");
    const int id3 = graph.add_node("C");

    EXPECT_FALSE(graph.has_edge(id1, id2));
    EXPECT_FALSE(graph.has_edge(id2, id3));
    EXPECT_FALSE(graph.has_edge(id1, id3));
}

TEST_F(CompressedGraphTest, DirectionalEdges) {
    CompressedGraph graph;
    const int id1 = graph.add_node("A");
    const int id2 = graph.add_node("B");

    graph.add_edge(id1, id2);

    EXPECT_TRUE(graph.has_edge(id1, id2));
    EXPECT_FALSE(graph.has_edge(id2, id1));
}

TEST_F(CompressedGraphTest, EmptyNeighborsForIsolatedNode) {
    CompressedGraph graph;
    const int id = graph.add_node("isolated");

    const auto neighbors = graph.get_neighbors(id);
    EXPECT_TRUE(neighbors.empty());

    const auto rev_neighbors = graph.get_reverse_neighbors(id);
    EXPECT_TRUE(rev_neighbors.empty());
}