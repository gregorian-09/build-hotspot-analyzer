//
// Created by gregorian on 16/10/2025.
//

#ifndef GRAPH_ALGORITHMS_H
#define GRAPH_ALGORITHMS_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>

namespace bha::graph {

    /**
     * Sorts the dependency graph in topological order.
     *
     * @param graph The dependency graph to sort.
     * @return A vector of node names in a valid topological order.
     */
    std::vector<std::string> topological_sort(const core::DependencyGraph& graph);

    /**
     * Performs topological sort, but returns an error if a cycle is detected.
     *
     * @param graph The dependency graph to sort.
     * @return A Result holding the sorted list or an Error if sorting failed (e.g., cycle present).
     */
    core::Result<std::vector<std::string>> topological_sort_checked(
        const core::DependencyGraph& graph
    );

    /**
     * Finds all simple cycles in the given dependency graph.
     *
     * @param graph The dependency graph to inspect.
     * @return A vector of cycles, each cycle represented as a vector of node names.
     */
    std::vector<std::vector<std::string>> find_cycles(const core::DependencyGraph& graph);

    /**
     * Checks whether the dependency graph contains any cycle.
     *
     * @param graph The dependency graph to check.
     * @return True if the graph has a cycle, false otherwise.
     */
    bool has_cycle(const core::DependencyGraph& graph);

    /**
     * Computes the strongly connected components of the graph.
     *
     * @param graph The graph to analyze.
     * @return A vector of components, each component is a vector of node names.
     */
    std::vector<std::vector<std::string>> strongly_connected_components(
        const core::DependencyGraph& graph
    );

    /**
     * Finds a path from one node to another in the graph, if it exists.
     *
     * @param graph The dependency graph.
     * @param start Name of the starting node.
     * @param end Name of the destination node.
     * @return A vector of node names forming a path, or an empty vector if no path exists.
     */
    std::vector<std::string> find_path(
        const core::DependencyGraph& graph,
        const std::string& start,
        const std::string& end
    );

    /**
     * Finds the longest acyclic path in the graph.
     *
     * @param graph The dependency graph.
     * @return A vector of node names representing one of the longest paths.
     */
    std::vector<std::string> find_longest_path(const core::DependencyGraph& graph);

    /**
     * Computes the depth (maximum distance from roots) of a single node.
     *
     * @param graph The dependency graph.
     * @param node The name of the node whose depth is to be computed.
     * @return The depth as an integer.
     */
    int calculate_depth(
        const core::DependencyGraph& graph,
        const std::string& node
    );

    /**
     * Computes the maximum depth over all nodes in the graph.
     *
     * @param graph The dependency graph.
     * @return The largest depth among all nodes.
     */
    int calculate_max_depth(const core::DependencyGraph& graph);

    /**
     * Computes depths for all nodes.
     *
     * @param graph The dependency graph.
     * @return A map from node names to their depth.
     */
    std::unordered_map<std::string, int> calculate_all_depths(
        const core::DependencyGraph& graph
    );

    /**
     * Gets root nodes (nodes with no incoming edges).
     *
     * @param graph The dependency graph.
     * @return A vector of root node names.
     */
    std::vector<std::string> get_root_nodes(const core::DependencyGraph& graph);

    /**
     * Gets leaf nodes (nodes with no outgoing edges).
     *
     * @param graph The dependency graph.
     * @return A vector of leaf node names.
     */
    std::vector<std::string> get_leaf_nodes(const core::DependencyGraph& graph);

    /**
     * Computes the fan-out (number of outgoing edges) for each node.
     *
     * @param graph The dependency graph.
     * @return A map from node to its fan-out count.
     */
    std::unordered_map<std::string, size_t> calculate_fanout(
        const core::DependencyGraph& graph
    );

    /**
     * Computes the fan-in (number of incoming edges) for each node.
     *
     * @param graph The dependency graph.
     * @return A map from node to its fan-in count.
     */
    std::unordered_map<std::string, size_t> calculate_fanin(
        const core::DependencyGraph& graph
    );

    /**
     * Retrieves all transitive dependencies of a node.
     *
     * @param graph The dependency graph.
     * @param node The node whose dependencies are to be found.
     * @return A vector of node names that are reachable (directly or indirectly).
     */
    std::vector<std::string> get_transitive_dependencies(
        const core::DependencyGraph& graph,
        const std::string& node
    );

    /**
     * Retrieves all transitive dependents of a node.
     *
     * @param graph The dependency graph.
     * @param node The node whose dependents are to be found.
     * @return A vector of node names that depend (directly or indirectly) on the node.
     */
    std::vector<std::string> get_transitive_dependents(
        const core::DependencyGraph& graph,
        const std::string& node
    );

    /**
     * Depth-first traversal from a given node, calling a callback on each visit.
     *
     * @param graph The dependency graph.
     * @param node The starting node name.
     * @param visited A set tracking nodes already visited (to prevent repeats).
     * @param callback Function called with each visited node.
     */
    void dfs(
        const core::DependencyGraph& graph,
        const std::string& node,
        std::unordered_set<std::string>& visited,
        const std::function<void(const std::string&)>& callback
    );

    /**
     * Breadth-first traversal starting at a node, calling callback with node and level.
     *
     * @param graph The dependency graph.
     * @param start Name of the start node.
     * @param callback Function receiving (node, level) for each visited node.
     */
    void bfs(
        const core::DependencyGraph& graph,
        const std::string& start,
        const std::function<void(const std::string&, int level)>& callback
    );

    /**
     * Returns a graph where all edges are reversed (direction flipped).
     *
     * @param graph The original graph.
     * @return A new dependency graph with inverted edges.
     */
    core::DependencyGraph reverse_graph(const core::DependencyGraph& graph);

    /**
     * Extracts a subgraph consisting only of a selected set of nodes and their internal edges.
     *
     * @param graph The original dependency graph.
     * @param nodes The set of node names to include.
     * @return A subgraph comprising those nodes and edges between them.
     */
    core::DependencyGraph subgraph(
        const core::DependencyGraph& graph,
        const std::vector<std::string>& nodes
    );

    /**
     * Finds a “critical path” through the graph weighted by node weights (e.g. compile times).
     *
     * @param graph The dependency graph.
     * @param node_weights A map from node name to its weight (e.g. compile time).
     * @return A vector of node names forming the heaviest dependency chain.
     */
    std::vector<std::string> find_critical_path(
        const core::DependencyGraph& graph,
        const std::unordered_map<std::string, double>& node_weights
    );

    /**
     * Checks whether the dependency graph is acyclic (a DAG).
     *
     * @param graph The dependency graph.
     * @return True if no cycles exist; false otherwise.
     */
    bool is_dag(const core::DependencyGraph& graph);

    /**
     * Counts how many distinct paths exist from one node to another.
     *
     * @param graph The dependency graph.
     * @param start Name of the start node.
     * @param end Name of the end node.
     * @return Number of distinct paths (could be large).
     */
    int count_paths(
        const core::DependencyGraph& graph,
        const std::string& start,
        const std::string& end
    );

}  // namespace bha::graph

#endif //GRAPH_ALGORITHMS_H
