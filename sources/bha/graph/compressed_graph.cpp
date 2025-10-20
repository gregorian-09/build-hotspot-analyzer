//
// Created by gregorian on 20/10/2025.
//

#include "bha/graph/compressed_graph.h"
#include <algorithm>

namespace bha::graph {

    CompressedGraph::CompressedGraph(const core::DependencyGraph& graph) {
        const auto nodes = graph.get_all_nodes();

        for (const auto& node : nodes) {
            add_node(node);
        }

        for (const auto& node : nodes) {
            const int from_id = get_id(node);

            for (auto edges = graph.get_edges(node); const auto& edge : edges) {
                const int to_id = get_id(edge.target);
                add_edge(from_id, to_id, edge.weight);
            }
        }
    }

    int CompressedGraph::add_node(const std::string& path) {
        if (const auto it = path_to_id_.find(path); it != path_to_id_.end()) {
            return it->second;
        }

        const int id = static_cast<int>(id_to_path_.size());
        id_to_path_.push_back(path);
        path_to_id_[path] = id;

        adjacency_list_.resize(id + 1);
        reverse_adjacency_list_.resize(id + 1);
        edge_weights_.resize(id + 1);

        return id;
    }

    void CompressedGraph::add_edge(const int from_id, const int to_id, const double weight) {
        if (from_id < 0 || from_id >= static_cast<int>(adjacency_list_.size()) ||
            to_id < 0 || to_id >= static_cast<int>(adjacency_list_.size())) {
            return;
        }

        adjacency_list_[from_id].push_back(to_id);
        reverse_adjacency_list_[to_id].push_back(from_id);
        edge_weights_[from_id].push_back(weight);
    }

    bool CompressedGraph::has_node(const int node_id) const {
        return node_id >= 0 && node_id < static_cast<int>(id_to_path_.size());
    }

    bool CompressedGraph::has_edge(const int from_id, const int to_id) const {
        if (!has_node(from_id) || !has_node(to_id)) {
            return false;
        }

        const auto& neighbors = adjacency_list_[from_id];
        return std::ranges::find(neighbors, to_id) != neighbors.end();
    }

    std::vector<int> CompressedGraph::get_neighbors(const int node_id) const {
        if (!has_node(node_id)) {
            return {};
        }

        return adjacency_list_[node_id];
    }

    std::vector<int> CompressedGraph::get_reverse_neighbors(const int node_id) const {
        if (!has_node(node_id)) {
            return {};
        }

        return reverse_adjacency_list_[node_id];
    }

    int CompressedGraph::get_id(const std::string& path) const {
        const auto it = path_to_id_.find(path);
        if (it == path_to_id_.end()) {
            return -1;
        }
        return it->second;
    }

    std::string CompressedGraph::get_path(const int node_id) const {
        if (!has_node(node_id)) {
            return "";
        }

        return id_to_path_[node_id];
    }

    size_t CompressedGraph::node_count() const {
        return id_to_path_.size();
    }

    size_t CompressedGraph::edge_count() const {
        size_t count = 0;
        for (const auto& neighbors : adjacency_list_) {
            count += neighbors.size();
        }
        return count;
    }

    size_t CompressedGraph::memory_usage_bytes() const {
        size_t total = 0;

        total += id_to_path_.capacity() * sizeof(std::string);
        for (const auto& path : id_to_path_) {
            total += path.capacity();
        }

        total += path_to_id_.size() * (sizeof(std::string) + sizeof(int));

        for (const auto& neighbors : adjacency_list_) {
            total += neighbors.capacity() * sizeof(int);
        }

        for (const auto& neighbors : reverse_adjacency_list_) {
            total += neighbors.capacity() * sizeof(int);
        }

        for (const auto& weights : edge_weights_) {
            total += weights.capacity() * sizeof(double);
        }

        return total;
    }

    core::DependencyGraph CompressedGraph::to_dependency_graph() const {
        core::DependencyGraph graph;

        for (const auto & i : id_to_path_) {
            graph.add_node(i);
        }

        for (size_t from = 0; from < adjacency_list_.size(); ++from) {
            const auto& neighbors = adjacency_list_[from];
            const auto& weights = edge_weights_[from];

            for (size_t i = 0; i < neighbors.size(); ++i) {
                core::DependencyEdge edge;
                edge.target = id_to_path_[neighbors[i]];
                edge.type = core::EdgeType::DIRECT_INCLUDE;
                edge.weight = weights[i];
                edge.line_number = 0;
                edge.is_system_header = false;

                graph.add_edge(id_to_path_[from], edge);
            }
        }

        return graph;
    }

    void CompressedGraph::clear() {
        adjacency_list_.clear();
        reverse_adjacency_list_.clear();
        edge_weights_.clear();
        id_to_path_.clear();
        path_to_id_.clear();
    }

    size_t estimate_memory_savings(
        const core::DependencyGraph& original,
        const CompressedGraph& compressed
    ) {
        size_t original_size = 0;

        const auto nodes = original.get_all_nodes();
        original_size += nodes.size() * sizeof(std::string);
        for (const auto& node : nodes) {
            original_size += node.capacity();

            auto edges = original.get_edges(node);
            original_size += edges.size() * sizeof(core::DependencyEdge);
            for (const auto& edge : edges) {
                original_size += edge.target.capacity();
            }
        }

        const size_t compressed_size = compressed.memory_usage_bytes();

        return original_size > compressed_size ? (original_size - compressed_size) : 0;
    }
}