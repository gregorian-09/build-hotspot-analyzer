//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BHA_GRAPH_HPP
#define BHA_GRAPH_HPP

/**
 * @file graph.hpp
 * @brief Graph data structures and algorithms.
 *
 * Provides directed graph representation optimized for:
 * - Include dependency analysis
 * - Cycle detection with path reporting
 * - Critical path calculation
 * - Topological sorting
 */

#include "bha/types.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace bha::graph {

    /**
     * Edge weight for timing information.
     */
    struct EdgeWeight {
        Duration time = Duration::zero();
        std::size_t count = 1;
    };

    /**
     * A cycle in the graph with the path of nodes.
     */
    struct Cycle {
        std::vector<std::string> nodes;
        Duration total_time = Duration::zero();
    };

    /**
     * Result of cycle detection.
     */
    struct CycleDetectionResult {
        bool has_cycles = false;
        std::vector<Cycle> cycles;
    };

    /**
     * A path through the graph.
     */
    struct Path {
        std::vector<std::string> nodes;
        Duration total_time = Duration::zero();
    };

    /**
     * Critical path analysis result.
     */
    struct CriticalPathResult {
        Path critical_path;
        std::vector<Path> alternative_paths;
        Duration total_time = Duration::zero();
    };

    /**
     * Node statistics.
     */
    struct NodeStats {
        std::string node;
        std::size_t in_degree = 0;
        std::size_t out_degree = 0;
        std::size_t depth = 0;
        Duration cumulative_time = Duration::zero();
    };

    /**
     * Directed graph with weighted edges.
     *
     * Uses adjacency list representation for efficient traversal.
     * Thread-safe for read operations after construction.
     */
    class DirectedGraph {
    public:
        DirectedGraph() = default;

        /**
         * Adds a node to the graph.
         */
        void add_node(const std::string& node, Duration time = Duration::zero());

        /**
         * Adds a directed edge from source to target.
         */
        void add_edge(const std::string& from, const std::string& to,
                      EdgeWeight weight = {});

        /**
         * Checks if the graph contains a node.
         */
        [[nodiscard]] bool has_node(const std::string& node) const;

        /**
         * Checks if an edge exists.
         */
        [[nodiscard]] bool has_edge(const std::string& from, const std::string& to) const;

        /**
         * Returns all nodes in the graph.
         */
        [[nodiscard]] std::vector<std::string> nodes() const;

        /**
         * Returns the number of nodes.
         */
        [[nodiscard]] std::size_t node_count() const;

        /**
         * Returns the number of edges.
         */
        [[nodiscard]] std::size_t edge_count() const;

        /**
         * Returns outgoing edges from a node.
         */
        [[nodiscard]] std::vector<std::string> successors(const std::string& node) const;

        /**
         * Returns incoming edges to a node.
         */
        [[nodiscard]] std::vector<std::string> predecessors(const std::string& node) const;

        /**
         * Gets the weight of an edge.
         */
        [[nodiscard]] std::optional<EdgeWeight> edge_weight(
            const std::string& from, const std::string& to) const;

        /**
         * Gets the time associated with a node.
         */
        [[nodiscard]] Duration node_time(const std::string& node) const;

        /**
         * Calculates node statistics.
         */
        [[nodiscard]] NodeStats node_stats(const std::string& node) const;

        /**
         * Returns nodes with no incoming edges (sources/roots).
         */
        [[nodiscard]] std::vector<std::string> roots() const;

        /**
         * Returns nodes with no outgoing edges (sinks/leaves).
         */
        [[nodiscard]] std::vector<std::string> leaves() const;

    private:
        struct NodeData {
            Duration time = Duration::zero();
            std::unordered_map<std::string, EdgeWeight> successors;
        };

        std::unordered_map<std::string, NodeData> adjacency_;
        std::unordered_map<std::string, std::unordered_set<std::string>> predecessors_;
        std::size_t edge_count_ = 0;
    };

    // ============================================================================
    // Graph Algorithms
    // ============================================================================

    /**
     * Detects cycles in the graph.
     *
     * Uses Tarjan's algorithm for strongly connected components.
     * Returns up to max_cycles cycles found.
     */
    [[nodiscard]] CycleDetectionResult detect_cycles(
        const DirectedGraph& graph,
        std::size_t max_cycles = 10
    );

    /**
     * Returns a topological ordering of the graph.
     *
     * Returns error if the graph contains cycles.
     */
    [[nodiscard]] Result<std::vector<std::string>, Error> topological_sort(
        const DirectedGraph& graph
    );

    /**
     * Finds the critical path (longest path) in a DAG.
     *
     * The critical path is the path with maximum cumulative time.
     * Returns error if the graph contains cycles.
     */
    [[nodiscard]] Result<CriticalPathResult, Error> find_critical_path(
        const DirectedGraph& graph
    );

    /**
     * Finds all paths between two nodes.
     *
     * Limited to max_paths to prevent explosion.
     */
    [[nodiscard]] std::vector<Path> find_all_paths(
        const DirectedGraph& graph,
        const std::string& from,
        const std::string& to,
        std::size_t max_paths = 100
    );

    /**
     * Computes the transitive closure of the graph.
     *
     * Returns a set of all (from, to) pairs where to is reachable from from.
     */
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> transitive_closure(
        const DirectedGraph& graph
    );

    /**
     * Finds nodes that would break the most cycles if removed.
     *
     * Useful for identifying headers that cause circular dependencies.
     */
    [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> find_cycle_breakers(
        const DirectedGraph& graph,
        std::size_t max_results = 10
    );

    /**
     * Computes the depth of each node from roots.
     */
    [[nodiscard]] std::unordered_map<std::string, std::size_t> compute_depths(
        const DirectedGraph& graph
    );

}  // namespace bha::graph

#endif //BHA_GRAPH_HPP