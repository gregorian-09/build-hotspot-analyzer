//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/graph/graph.hpp"

#include <gtest/gtest.h>

namespace bha::graph
{
    class DirectedGraphTest : public ::testing::Test {
    protected:
        DirectedGraph graph_;
    };

    TEST_F(DirectedGraphTest, EmptyGraph) {
        EXPECT_EQ(graph_.node_count(), 0u);
        EXPECT_EQ(graph_.edge_count(), 0u);
        EXPECT_TRUE(graph_.nodes().empty());
    }

    TEST_F(DirectedGraphTest, AddNode) {
        graph_.add_node("A", std::chrono::milliseconds(100));

        EXPECT_EQ(graph_.node_count(), 1u);
        EXPECT_TRUE(graph_.has_node("A"));
        EXPECT_FALSE(graph_.has_node("B"));
        EXPECT_TRUE(graph_.node_time("A") == std::chrono::milliseconds(100));
    }

    TEST_F(DirectedGraphTest, AddEdge) {
        graph_.add_edge("A", "B");

        EXPECT_EQ(graph_.node_count(), 2u);
        EXPECT_EQ(graph_.edge_count(), 1u);
        EXPECT_TRUE(graph_.has_edge("A", "B"));
        EXPECT_FALSE(graph_.has_edge("B", "A"));
    }

    TEST_F(DirectedGraphTest, Successors) {
        graph_.add_edge("A", "B");
        graph_.add_edge("A", "C");
        graph_.add_edge("B", "C");

        auto succs = graph_.successors("A");
        EXPECT_EQ(succs.size(), 2u);

        succs = graph_.successors("B");
        EXPECT_EQ(succs.size(), 1u);

        succs = graph_.successors("C");
        EXPECT_TRUE(succs.empty());
    }

    TEST_F(DirectedGraphTest, Predecessors) {
        graph_.add_edge("A", "C");
        graph_.add_edge("B", "C");

        auto preds = graph_.predecessors("C");
        EXPECT_EQ(preds.size(), 2u);

        preds = graph_.predecessors("A");
        EXPECT_TRUE(preds.empty());
    }

    TEST_F(DirectedGraphTest, RootsAndLeaves) {
        graph_.add_edge("A", "B");
        graph_.add_edge("A", "C");
        graph_.add_edge("B", "D");
        graph_.add_edge("C", "D");

        const auto roots = graph_.roots();
        EXPECT_EQ(roots.size(), 1u);
        EXPECT_EQ(roots[0], "A");

        const auto leaves = graph_.leaves();
        EXPECT_EQ(leaves.size(), 1u);
        EXPECT_EQ(leaves[0], "D");
    }

    class CycleDetectionTest : public ::testing::Test {
    protected:
        DirectedGraph graph_;
    };

    TEST_F(CycleDetectionTest, NoCycles) {
        graph_.add_edge("A", "B");
        graph_.add_edge("B", "C");
        graph_.add_edge("A", "C");

        auto [has_cycles, cycles] = detect_cycles(graph_);
        EXPECT_FALSE(has_cycles);
        EXPECT_TRUE(cycles.empty());
    }

    TEST_F(CycleDetectionTest, SimpleCycle) {
        graph_.add_edge("A", "B");
        graph_.add_edge("B", "C");
        graph_.add_edge("C", "A");

        auto [has_cycles, cycles] = detect_cycles(graph_);
        EXPECT_TRUE(has_cycles);
        EXPECT_GE(cycles.size(), 1u);
    }

    TEST_F(CycleDetectionTest, SelfLoop) {
        graph_.add_edge("A", "A");

        auto [has_cycles, cycles] = detect_cycles(graph_);
        EXPECT_TRUE(has_cycles);
    }

    TEST_F(CycleDetectionTest, MultipleCycles) {
        graph_.add_edge("A", "B");
        graph_.add_edge("B", "A");
        graph_.add_edge("C", "D");
        graph_.add_edge("D", "C");

        auto [has_cycles, cycles] = detect_cycles(graph_, 10);
        EXPECT_TRUE(has_cycles);
        EXPECT_GE(cycles.size(), 2u);
    }

    class TopologicalSortTest : public ::testing::Test {
    protected:
        DirectedGraph graph_;
    };

    TEST_F(TopologicalSortTest, SimpleDAG) {
        graph_.add_edge("A", "B");
        graph_.add_edge("A", "C");
        graph_.add_edge("B", "D");
        graph_.add_edge("C", "D");

        auto result = topological_sort(graph_);
        ASSERT_TRUE(result.is_ok());

        const auto& order = result.value();
        EXPECT_EQ(order.size(), 4u);

        std::unordered_map<std::string, std::size_t> positions;
        for (std::size_t i = 0; i < order.size(); ++i) {
            positions[order[i]] = i;
        }

        EXPECT_LT(positions["A"], positions["B"]);
        EXPECT_LT(positions["A"], positions["C"]);
        EXPECT_LT(positions["B"], positions["D"]);
        EXPECT_LT(positions["C"], positions["D"]);
    }

    TEST_F(TopologicalSortTest, FailsWithCycle) {
        graph_.add_edge("A", "B");
        graph_.add_edge("B", "A");

        const auto result = topological_sort(graph_);
        EXPECT_FALSE(result.is_ok());
    }

    class CriticalPathTest : public ::testing::Test {
    protected:
        DirectedGraph graph_;
    };

    TEST_F(CriticalPathTest, SimpleDAG) {
        graph_.add_node("A", std::chrono::milliseconds(100));
        graph_.add_node("B", std::chrono::milliseconds(200));
        graph_.add_node("C", std::chrono::milliseconds(50));
        graph_.add_node("D", std::chrono::milliseconds(100));

        graph_.add_edge("A", "B");
        graph_.add_edge("A", "C");
        graph_.add_edge("B", "D");
        graph_.add_edge("C", "D");

        auto result = find_critical_path(graph_);
        ASSERT_TRUE(result.is_ok());

        const auto& cp = result.value();
        EXPECT_FALSE(cp.critical_path.nodes.empty());
        EXPECT_GT(cp.total_time.count(), 0);
    }

    TEST_F(CriticalPathTest, FailsWithCycle) {
        graph_.add_edge("A", "B");
        graph_.add_edge("B", "A");

        const auto result = find_critical_path(graph_);
        EXPECT_FALSE(result.is_ok());
    }

    class FindAllPathsTest : public ::testing::Test {
    protected:
        DirectedGraph graph_;
    };

    TEST_F(FindAllPathsTest, MultiplePaths) {
        graph_.add_edge("A", "B");
        graph_.add_edge("A", "C");
        graph_.add_edge("B", "D");
        graph_.add_edge("C", "D");

        const auto paths = find_all_paths(graph_, "A", "D");
        EXPECT_EQ(paths.size(), 2u);
    }

    TEST_F(FindAllPathsTest, NoPath) {
        graph_.add_edge("A", "B");
        graph_.add_node("C");

        const auto paths = find_all_paths(graph_, "A", "C");
        EXPECT_TRUE(paths.empty());
    }

    class CycleBreakersTest : public ::testing::Test {
    protected:
        DirectedGraph graph_;
    };

    TEST_F(CycleBreakersTest, FindsBreakers) {
        graph_.add_edge("A", "B");
        graph_.add_edge("B", "C");
        graph_.add_edge("C", "A");

        const auto breakers = find_cycle_breakers(graph_);
        EXPECT_FALSE(breakers.empty());
    }

    TEST_F(CycleBreakersTest, NoCyclesNoBrakers) {
        graph_.add_edge("A", "B");
        graph_.add_edge("B", "C");

        const auto breakers = find_cycle_breakers(graph_);
        EXPECT_TRUE(breakers.empty());
    }

    class ComputeDepthsTest : public ::testing::Test {
    protected:
        DirectedGraph graph_;
    };

    TEST_F(ComputeDepthsTest, SimpleTree) {
        graph_.add_edge("A", "B");
        graph_.add_edge("A", "C");
        graph_.add_edge("B", "D");

        auto depths = compute_depths(graph_);

        EXPECT_EQ(depths["A"], 0u);
        EXPECT_EQ(depths["B"], 1u);
        EXPECT_EQ(depths["C"], 1u);
        EXPECT_EQ(depths["D"], 2u);
    }
}