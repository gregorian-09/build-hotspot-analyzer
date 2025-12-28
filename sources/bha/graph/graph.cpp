//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/graph/graph.hpp"

#include <algorithm>
#include <queue>
#include <stack>
#include <functional>
#include <ranges>

namespace bha::graph {

    // ============================================================================
    // DirectedGraph Implementation
    // ============================================================================

    void DirectedGraph::add_node(const std::string& node, Duration time) {
        if (!adjacency_.contains(node)) {
            adjacency_[node] = NodeData{time, {}};
            predecessors_[node] = {};
        } else {
            adjacency_[node].time = time;
        }
    }

    void DirectedGraph::add_edge(const std::string& from, const std::string& to,
                                 const EdgeWeight weight) {
        if (!has_node(from)) {
            add_node(from);
        }
        if (!has_node(to)) {
            add_node(to);
        }

        if (!adjacency_[from].successors.contains(to)) {
            adjacency_[from].successors[to] = weight;
            predecessors_[to].insert(from);
            ++edge_count_;
        } else {
            adjacency_[from].successors[to].count += weight.count;
            adjacency_[from].successors[to].time += weight.time;
        }
    }

    bool DirectedGraph::has_node(const std::string& node) const {
        return adjacency_.contains(node);
    }

    bool DirectedGraph::has_edge(const std::string& from, const std::string& to) const {
        const auto it = adjacency_.find(from);
        if (it == adjacency_.end()) return false;
        return it->second.successors.contains(to);
    }

    std::vector<std::string> DirectedGraph::nodes() const {
        std::vector<std::string> result;
        result.reserve(adjacency_.size());
        for (const auto& node : adjacency_ | std::views::keys) {
            result.push_back(node);
        }
        return result;
    }

    std::size_t DirectedGraph::node_count() const {
        return adjacency_.size();
    }

    std::size_t DirectedGraph::edge_count() const {
        return edge_count_;
    }

    std::vector<std::string> DirectedGraph::successors(const std::string& node) const {
        std::vector<std::string> result;
        if (const auto it = adjacency_.find(node); it != adjacency_.end()) {
            for (const auto& succ : it->second.successors | std::views::keys) {
                result.push_back(succ);
            }
        }
        return result;
    }

    std::vector<std::string> DirectedGraph::predecessors(const std::string& node) const {
        std::vector<std::string> result;
        if (const auto it = predecessors_.find(node); it != predecessors_.end()) {
            for (const auto& pred : it->second) {
                result.push_back(pred);
            }
        }
        return result;
    }

    std::optional<EdgeWeight> DirectedGraph::edge_weight(
        const std::string& from, const std::string& to) const {
        const auto it = adjacency_.find(from);
        if (it == adjacency_.end()) return std::nullopt;

        const auto edge_it = it->second.successors.find(to);
        if (edge_it == it->second.successors.end()) return std::nullopt;

        return edge_it->second;
    }

    Duration DirectedGraph::node_time(const std::string& node) const {
        const auto it = adjacency_.find(node);
        if (it == adjacency_.end()) return Duration::zero();
        return it->second.time;
    }

    NodeStats DirectedGraph::node_stats(const std::string& node) const {
        NodeStats stats;
        stats.node = node;

        if (const auto it = adjacency_.find(node); it != adjacency_.end()) {
            stats.out_degree = it->second.successors.size();
        }

        if (const auto pred_it = predecessors_.find(node); pred_it != predecessors_.end()) {
            stats.in_degree = pred_it->second.size();
        }

        return stats;
    }

    std::vector<std::string> DirectedGraph::roots() const {
        std::vector<std::string> result;
        for (const auto& [node, preds] : predecessors_) {
            if (preds.empty()) {
                result.push_back(node);
            }
        }
        return result;
    }

    std::vector<std::string> DirectedGraph::leaves() const {
        std::vector<std::string> result;
        for (const auto& [node, data] : adjacency_) {
            if (data.successors.empty()) {
                result.push_back(node);
            }
        }
        return result;
    }

    // ============================================================================
    // Algorithm Implementations
    // ============================================================================

    CycleDetectionResult detect_cycles(const DirectedGraph& graph, const std::size_t max_cycles) {
        CycleDetectionResult result;

        std::unordered_map<std::string, int> color;
        std::unordered_map<std::string, std::string> parent;

        for (const auto& node : graph.nodes()) {
            color[node] = 0;
        }

        std::function<void(const std::string&, std::vector<std::string>&)> dfs =
            [&](const std::string& node, std::vector<std::string>& path) {
                if (result.cycles.size() >= max_cycles) return;

                color[node] = 1;
                path.push_back(node);

                for (const auto& succ : graph.successors(node)) {
                    if (color[succ] == 1) {
                        Cycle cycle;
                        bool in_cycle = false;
                        for (const auto& n : path) {
                            if (n == succ) in_cycle = true;
                            if (in_cycle) {
                                cycle.nodes.push_back(n);
                                cycle.total_time += graph.node_time(n);
                            }
                        }
                        cycle.nodes.push_back(succ);
                        result.cycles.push_back(std::move(cycle));
                        result.has_cycles = true;
                    } else if (color[succ] == 0) {
                        dfs(succ, path);
                    }
                }

                path.pop_back();
                color[node] = 2;
            };

        for (const auto& node : graph.nodes()) {
            if (color[node] == 0 && result.cycles.size() < max_cycles) {
                std::vector<std::string> path;
                dfs(node, path);
            }
        }

        return result;
    }

    Result<std::vector<std::string>, Error> topological_sort(const DirectedGraph& graph) {
        auto [has_cycles, cycles] = detect_cycles(graph, 1);
        if (has_cycles) {
            return Result<std::vector<std::string>, Error>::failure(
                Error::analysis_error("Graph contains cycles, cannot perform topological sort")
            );
        }

        std::unordered_map<std::string, std::size_t> in_degree;
        for (const auto& node : graph.nodes()) {
            in_degree[node] = graph.predecessors(node).size();
        }

        std::queue<std::string> queue;
        for (const auto& [node, degree] : in_degree) {
            if (degree == 0) {
                queue.push(node);
            }
        }

        std::vector<std::string> result;
        result.reserve(graph.node_count());

        while (!queue.empty()) {
            auto node = queue.front();
            queue.pop();
            result.push_back(node);

            for (const auto& succ : graph.successors(node)) {
                if (--in_degree[succ] == 0) {
                    queue.push(succ);
                }
            }
        }

        return Result<std::vector<std::string>, Error>::success(std::move(result));
    }

    Result<CriticalPathResult, Error> find_critical_path(const DirectedGraph& graph) {
        auto topo_result = topological_sort(graph);
        if (!topo_result.is_ok()) {
            return Result<CriticalPathResult, Error>::failure(topo_result.error());
        }

        const auto& topo_order = topo_result.value();
        std::unordered_map<std::string, Duration> dist;
        std::unordered_map<std::string, std::string> prev;

        for (const auto& node : topo_order) {
            dist[node] = Duration::zero();
        }

        for (const auto& node : topo_order) {
            Duration node_time = graph.node_time(node);
            for (const auto& succ : graph.successors(node)) {
                auto edge = graph.edge_weight(node, succ);
                Duration edge_time = edge ? edge->time : Duration::zero();

                if (Duration new_dist = dist[node] + node_time + edge_time; new_dist > dist[succ]) {
                    dist[succ] = new_dist;
                    prev[succ] = node;
                }
            }
        }

        std::string end_node;
        Duration max_dist = Duration::zero();
        for (const auto& [node, d] : dist) {
            if (Duration total = d + graph.node_time(node); total > max_dist) {
                max_dist = total;
                end_node = node;
            }
        }

        CriticalPathResult result;
        result.total_time = max_dist;

        if (!end_node.empty()) {
            std::string current = end_node;
            while (!current.empty()) {
                result.critical_path.nodes.push_back(current);
                result.critical_path.total_time += graph.node_time(current);
                auto it = prev.find(current);
                current = (it != prev.end()) ? it->second : "";
            }
            std::ranges::reverse(result.critical_path.nodes);
        }

        return Result<CriticalPathResult, Error>::success(std::move(result));
    }

    std::vector<Path> find_all_paths(
        const DirectedGraph& graph,
        const std::string& from,
        const std::string& to,
        const std::size_t max_paths) {

        std::vector<Path> result;

        if (!graph.has_node(from) || !graph.has_node(to)) {
            return result;
        }

        std::function<void(const std::string&, Path&, std::unordered_set<std::string>&)> dfs =
            [&](const std::string& node, Path& current, std::unordered_set<std::string>& visited) {
                if (result.size() >= max_paths) return;

                current.nodes.push_back(node);
                current.total_time += graph.node_time(node);
                visited.insert(node);

                if (node == to) {
                    result.push_back(current);
                } else {
                    for (const auto& succ : graph.successors(node)) {
                        if (!visited.contains(succ)) {
                            dfs(succ, current, visited);
                        }
                    }
                }

                current.nodes.pop_back();
                current.total_time -= graph.node_time(node);
                visited.erase(node);
            };

        Path path;
        std::unordered_set<std::string> visited;
        dfs(from, path, visited);

        return result;
    }

    std::vector<std::pair<std::string, std::string>> transitive_closure(
        const DirectedGraph& graph) {

        std::vector<std::pair<std::string, std::string>> result;

        for (const auto all_nodes = graph.nodes(); const auto& start : all_nodes) {
            std::unordered_set<std::string> reachable;
            std::queue<std::string> queue;
            queue.push(start);
            reachable.insert(start);

            while (!queue.empty()) {
                auto node = queue.front();
                queue.pop();

                for (const auto& succ : graph.successors(node)) {
                    if (!reachable.contains(succ)) {
                        reachable.insert(succ);
                        queue.push(succ);
                        result.emplace_back(start, succ);
                    }
                }
            }
        }

        return result;
    }

    std::vector<std::pair<std::string, std::size_t>> find_cycle_breakers(
        const DirectedGraph& graph,
        const std::size_t max_results) {

        auto [has_cycles, cycles] = detect_cycles(graph, 100);
        if (!has_cycles) {
            return {};
        }

        std::unordered_map<std::string, std::size_t> cycle_participation;

        for (const auto& [nodes, total_time] : cycles) {
            for (const auto& node : nodes) {
                cycle_participation[node]++;
            }
        }

        std::vector<std::pair<std::string, std::size_t>> result;
        for (const auto& [node, count] : cycle_participation) {
            result.emplace_back(node, count);
        }

        std::ranges::sort(result,
                          [](const auto& a, const auto& b) { return a.second > b.second; });

        if (result.size() > max_results) {
            result.resize(max_results);
        }

        return result;
    }

    std::unordered_map<std::string, std::size_t> compute_depths(const DirectedGraph& graph) {
        std::unordered_map<std::string, std::size_t> depths;
        const auto roots = graph.roots();

        std::queue<std::pair<std::string, std::size_t>> queue;
        for (const auto& root : roots) {
            queue.emplace(root, 0);
            depths[root] = 0;
        }

        while (!queue.empty()) {
            auto [node, depth] = queue.front();
            queue.pop();

            for (const auto& succ : graph.successors(node)) {
                if (std::size_t new_depth = depth + 1; !depths.contains(succ) || depths[succ] < new_depth) {
                    depths[succ] = new_depth;
                    queue.emplace(succ, new_depth);
                }
            }
        }

        return depths;
    }

}  // namespace bha::graph
