//
// Created by gregorian on 20/10/2025.
//

#ifndef COMPRESSED_GRAPH_H
#define COMPRESSED_GRAPH_H

#include "bha/core/types.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace bha::graph {

    /**
     * @class CompressedGraph
     * Represents a memory-efficient version of a DependencyGraph using integer node IDs.
     *
     * The CompressedGraph class provides a compact adjacency representation of a dependency graph
     * by mapping file paths (strings) to integer node identifiers. This reduces memory usage and
     * improves traversal performance in large dependency networks. The class supports both
     * forward and reverse adjacency lookups, weight storage, and conversion back to the full
     * DependencyGraph structure.
     */
    class CompressedGraph {
    public:
        /**
         * Default constructor. Initializes an empty compressed graph.
         */
        CompressedGraph() = default;

        /**
         * Constructs a compressed graph from a full DependencyGraph.
         *
         * @param graph The original dependency graph to compress.
         */
        explicit CompressedGraph(const core::DependencyGraph& graph);

        /**
         * Adds a new node to the graph.
         *
         * If the node already exists, its existing ID is returned.
         *
         * @param path The string path representing the node (e.g., file path).
         * @return The integer ID assigned to the node.
         */
        int add_node(const std::string& path);

        /**
         * Adds a directed edge between two nodes.
         *
         * @param from_id The ID of the source node.
         * @param to_id The ID of the target node.
         * @param weight Optional weight of the edge (default is 0.0).
         */
        void add_edge(int from_id, int to_id, double weight = 0.0);

        /**
         * Checks whether a node exists in the graph.
         *
         * @param node_id The ID of the node.
         * @return True if the node exists, false otherwise.
         */
        bool has_node(int node_id) const;

        /**
         * Checks whether an edge exists between two nodes.
         *
         * @param from_id The ID of the source node.
         * @param to_id The ID of the target node.
         * @return True if the edge exists, false otherwise.
         */
        bool has_edge(int from_id, int to_id) const;

        /**
         * Retrieves all direct neighbors (outgoing edges) of a node.
         *
         * @param node_id The ID of the node.
         * @return A vector of node IDs representing neighbors.
         */
        std::vector<int> get_neighbors(int node_id) const;

        /**
         * Retrieves all reverse neighbors (incoming edges) of a node.
         *
         * @param node_id The ID of the node.
         * @return A vector of node IDs representing reverse neighbors.
         */
        std::vector<int> get_reverse_neighbors(int node_id) const;

        /**
         * Retrieves the numeric ID for a given file path.
         *
         * @param path The file path of the node.
         * @return The integer node ID.
         * @throws std::out_of_range if the path is not present in the graph.
         */
        int get_id(const std::string& path) const;

        /**
         * Retrieves the file path for a given node ID.
         *
         * @param node_id The integer node ID.
         * @return The string path associated with the node.
         * @throws std::out_of_range if the ID does not exist.
         */
        std::string get_path(int node_id) const;

        /**
         * Returns the total number of nodes in the graph.
         *
         * @return The number of nodes.
         */
        size_t node_count() const;

        /**
         * Returns the total number of edges in the graph.
         *
         * @return The number of edges.
         */
        size_t edge_count() const;

        /**
         * Estimates the memory usage of the compressed graph in bytes.
         *
         * @return The total memory consumption of internal data structures.
         */
        size_t memory_usage_bytes() const;

        /**
         * Converts the compressed graph back into a full DependencyGraph.
         *
         * @return A reconstructed DependencyGraph.
         */
        core::DependencyGraph to_dependency_graph() const;

        /**
         * Clears all nodes and edges from the graph.
         */
        void clear();

    private:
        std::vector<std::vector<int>> adjacency_list_; ///< Outgoing edges for each node.
        std::vector<std::vector<int>> reverse_adjacency_list_; ///< Incoming edges for each node.
        std::vector<std::vector<double>> edge_weights_; ///< Edge weights for each adjacency entry.

        std::vector<std::string> id_to_path_; ///< Mapping from node ID to file path.
        std::unordered_map<std::string, int> path_to_id_; ///< Mapping from file path to node ID.
    };

    /**
     * Estimates memory savings achieved by compressing a DependencyGraph.
     *
     * Compares the memory usage of the original DependencyGraph against its compressed version.
     *
     * @param original The original uncompressed dependency graph.
     * @param compressed The compressed graph.
     * @return Estimated number of bytes saved by compression.
     */
    size_t estimate_memory_savings(
        const core::DependencyGraph& original,
        const CompressedGraph& compressed
    );

} // namespace bha::graph

#endif //COMPRESSED_GRAPH_H
