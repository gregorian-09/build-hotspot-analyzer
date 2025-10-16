//
// Created by gregorian on 16/10/2025.
//

#include "bha/graph/graph_algorithms.h"
#include <algorithm>
#include <queue>
#include <ranges>
#include <stack>

namespace bha::graph {

    std::vector<std::string> topological_sort(const core::DependencyGraph& graph) {
        std::unordered_map<std::string, int> in_degree;
        const auto nodes = graph.get_all_nodes();

        for (const auto& node : nodes) {
            in_degree[node] = 0;
        }

        for (const auto& node : nodes) {
            for (auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                in_degree[dep]++;
            }
        }

        std::queue<std::string> queue;
        for (const auto& [node, degree] : in_degree) {
            if (degree == 0) {
                queue.push(node);
            }
        }

        std::vector<std::string> result;
        while (!queue.empty()) {
            std::string node = queue.front();
            queue.pop();
            result.push_back(node);

            for (auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                in_degree[dep]--;
                if (in_degree[dep] == 0) {
                    queue.push(dep);
                }
            }
        }

        return result;
    }

    core::Result<std::vector<std::string>> topological_sort_checked(
        const core::DependencyGraph& graph
    ) {
        auto result = topological_sort(graph);

        if (result.size() != graph.node_count()) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::CIRCULAR_DEPENDENCY,
                "Graph contains cycles, topological sort not possible"
            );
        }

        return core::Result<std::vector<std::string>>::success(std::move(result));
    }

    std::vector<std::vector<std::string>> find_cycles(const core::DependencyGraph& graph) {
        std::vector<std::vector<std::string>> cycles;
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> rec_stack;
        std::vector<std::string> path;

        const auto nodes = graph.get_all_nodes();

        std::function<void(const std::string&)> dfs_cycle = [&](const std::string& node) {
            visited.insert(node);
            rec_stack.insert(node);
            path.push_back(node);

            for (const auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                if (rec_stack.contains(dep)) {
                    if (const auto it = std::ranges::find(path.begin(), path.end(), dep); it != path.end()) {
                        std::vector cycle(it, path.end());
                        cycle.push_back(dep);
                        cycles.push_back(cycle);
                    }
                } else if (!visited.contains(dep)) {
                    dfs_cycle(dep);
                }
            }

            rec_stack.erase(node);
            path.pop_back();
        };

        for (const auto& node : nodes) {
            if (!visited.contains(node)) {
                dfs_cycle(node);
            }
        }

        return cycles;
    }

    bool has_cycle(const core::DependencyGraph& graph) {
        return !find_cycles(graph).empty();
    }

    std::vector<std::vector<std::string>> strongly_connected_components(
        const core::DependencyGraph& graph
    ) {
        std::vector<std::vector<std::string>> components;
        std::unordered_map<std::string, int> indices;
        std::unordered_map<std::string, int> lowlinks;
        std::unordered_set<std::string> on_stack;
        std::stack<std::string> stack;
        int index = 0;

        const auto nodes = graph.get_all_nodes();

        std::function<void(const std::string&)> strongconnect = [&](const std::string& node) {
            indices[node] = index;
            lowlinks[node] = index;
            index++;
            stack.push(node);
            on_stack.insert(node);

            for (const auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                if (!indices.contains(dep)) {
                    strongconnect(dep);
                    lowlinks[node] = std::min(lowlinks[node], lowlinks[dep]);
                } else if (on_stack.contains(dep)) {
                    lowlinks[node] = std::min(lowlinks[node], indices[dep]);
                }
            }

            if (lowlinks[node] == indices[node]) {
                std::vector<std::string> component;
                std::string w;
                do {
                    w = stack.top();
                    stack.pop();
                    on_stack.erase(w);
                    component.push_back(w);
                } while (w != node);

                if (component.size() > 1) {
                    components.push_back(component);
                }
            }
        };

        for (const auto& node : nodes) {
            if (!indices.contains(node)) {
                strongconnect(node);
            }
        }

        return components;
    }

    std::vector<std::string> find_path(
        const core::DependencyGraph& graph,
        const std::string& start,
        const std::string& end
    ) {
        if (!graph.has_node(start) || !graph.has_node(end)) {
            return {};
        }

        std::unordered_map<std::string, std::string> parent;
        std::queue<std::string> queue;
        std::unordered_set<std::string> visited;

        queue.push(start);
        visited.insert(start);

        while (!queue.empty()) {
            std::string node = queue.front();
            queue.pop();

            if (node == end) {
                std::vector<std::string> path;
                std::string current = end;
                while (current != start) {
                    path.push_back(current);
                    current = parent[current];
                }
                path.push_back(start);
                std::ranges::reverse(path);
                return path;
            }

            for (auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                if (!visited.contains(dep)) {
                    visited.insert(dep);
                    parent[dep] = node;
                    queue.push(dep);
                }
            }
        }

        return {};
    }

    std::vector<std::string> find_longest_path(const core::DependencyGraph& graph) {
        const auto sorted = topological_sort(graph);
        if (sorted.size() != graph.node_count()) {
            return {};
        }

        std::unordered_map<std::string, int> dist;
        std::unordered_map<std::string, std::string> parent;

        for (const auto& node : sorted) {
            dist[node] = 0;
        }

        for (const auto& node : sorted) {
            for (auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                if (dist[dep] < dist[node] + 1) {
                    dist[dep] = dist[node] + 1;
                    parent[dep] = node;
                }
            }
        }

        std::string longest_node;
        int max_dist = -1;
        for (const auto& [node, d] : dist) {
            if (d > max_dist) {
                max_dist = d;
                longest_node = node;
            }
        }

        std::vector<std::string> path;
        std::string current = longest_node;
        while (!current.empty()) {
            path.push_back(current);
            current = parent.contains(current) ? parent[current] : "";
        }

        std::ranges::reverse(path);
        return path;
    }

    int calculate_depth(const core::DependencyGraph& graph, const std::string& node) {
        if (!graph.has_node(node)) {
            return -1;
        }

        std::unordered_map<std::string, int> memo;

        std::function<int(const std::string&)> depth_helper = [&](const std::string& n) -> int {
            if (memo.contains(n)) {
                return memo[n];
            }

            const auto deps = graph.get_dependencies(n);
            if (deps.empty()) {
                memo[n] = 0;
                return 0;
            }

            int max_depth = 0;
            for (const auto& dep : deps) {
                max_depth = std::max(max_depth, depth_helper(dep) + 1);
            }

            memo[n] = max_depth;
            return max_depth;
        };

        return depth_helper(node);
    }

    int calculate_max_depth(const core::DependencyGraph& graph) {
        int max_depth = 0;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            max_depth = std::max(max_depth, calculate_depth(graph, node));
        }

        return max_depth;
    }

    std::unordered_map<std::string, int> calculate_all_depths(
        const core::DependencyGraph& graph
    ) {
        std::unordered_map<std::string, int> depths;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            depths[node] = calculate_depth(graph, node);
        }

        return depths;
    }

    std::vector<std::string> get_root_nodes(const core::DependencyGraph& graph) {
        std::vector<std::string> roots;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            if (auto deps = graph.get_reverse_dependencies(node); deps.empty()) {
                roots.push_back(node);
            }
        }

        return roots;
    }

    std::vector<std::string> get_leaf_nodes(const core::DependencyGraph& graph) {
        std::vector<std::string> leaves;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            if (auto deps = graph.get_dependencies(node); deps.empty()) {
                leaves.push_back(node);
            }
        }

        return leaves;
    }

    std::unordered_map<std::string, size_t> calculate_fanout(
        const core::DependencyGraph& graph
    ) {
        std::unordered_map<std::string, size_t> fanout;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            fanout[node] = graph.get_dependencies(node).size();
        }

        return fanout;
    }

    std::unordered_map<std::string, size_t> calculate_fanin(
        const core::DependencyGraph& graph
    ) {
        std::unordered_map<std::string, size_t> fanin;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            fanin[node] = graph.get_reverse_dependencies(node).size();
        }

        return fanin;
    }

    std::vector<std::string> get_transitive_dependencies(
        const core::DependencyGraph& graph,
        const std::string& node
    ) {
        std::vector<std::string> result;
        std::unordered_set<std::string> visited;

        dfs(graph, node, visited, [&](const std::string& n) {
            if (n != node) {
                result.push_back(n);
            }
        });

        return result;
    }

    std::vector<std::string> get_transitive_dependents(
        const core::DependencyGraph& graph,
        const std::string& node
    ) {
        const auto reversed = reverse_graph(graph);
        return get_transitive_dependencies(reversed, node);
    }

    void dfs(
        const core::DependencyGraph& graph,
        const std::string& node,
        std::unordered_set<std::string>& visited,
        const std::function<void(const std::string&)>& callback
    ) {
        std::vector<std::string> stack;
        stack.push_back(node);

        while (!stack.empty()) {
            std::string current = stack.back();
            stack.pop_back();

            if (visited.contains(current)) {
                continue;
            }

            visited.insert(current);
            callback(current);

            for (const auto deps = graph.get_dependencies(current); const auto & dep : std::ranges::reverse_view(deps)) {
                if (!visited.contains(dep)) {
                    stack.push_back(dep);
                }
            }
        }
    }

    void bfs(
        const core::DependencyGraph& graph,
        const std::string& start,
        const std::function<void(const std::string&, int level)>& callback
    ) {
        std::queue<std::pair<std::string, int>> queue;
        std::unordered_set<std::string> visited;

        queue.emplace(start, 0);
        visited.insert(start);

        while (!queue.empty()) {
            auto [node, level] = queue.front();
            queue.pop();

            callback(node, level);

            for (auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                if (!visited.contains(dep)) {
                    visited.insert(dep);
                    queue.emplace(dep, level + 1);
                }
            }
        }
    }

    core::DependencyGraph reverse_graph(const core::DependencyGraph& graph) {
        core::DependencyGraph reversed;
        const auto nodes = graph.get_all_nodes();

        for (const auto& node : nodes) {
            reversed.add_node(node);
        }

        for (const auto& node : nodes) {
            for (auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                reversed.add_edge(dep, node);
            }
        }

        return reversed;
    }

    core::DependencyGraph subgraph(
        const core::DependencyGraph& graph,
        const std::vector<std::string>& nodes
    ) {
        core::DependencyGraph sub;
        const std::unordered_set node_set(nodes.begin(), nodes.end());

        for (const auto& node : nodes) {
            if (graph.has_node(node)) {
                sub.add_node(node);
            }
        }

        for (const auto& node : nodes) {
            if (!graph.has_node(node)) continue;

            for (auto edges = graph.get_edges(node); const auto& edge : edges) {
                if (node_set.contains(edge.target)) {
                    sub.add_edge(node, edge);
                }
            }
        }

        return sub;
    }

    std::vector<std::string> find_critical_path(
        const core::DependencyGraph& graph,
        const std::unordered_map<std::string, double>& node_weights
    ) {
        const auto sorted = topological_sort(graph);
        if (sorted.size() != graph.node_count()) {
            return {};
        }

        std::unordered_map<std::string, double> dist;
        std::unordered_map<std::string, std::string> parent;

        for (const auto& node : sorted) {
            dist[node] = node_weights.contains(node) ? node_weights.at(node) : 0.0;
        }

        for (const auto& node : sorted) {
            for (auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                if (const double new_dist = dist[node] + (node_weights.contains(dep) ? node_weights.at(dep) : 0.0); new_dist > dist[dep]) {
                    dist[dep] = new_dist;
                    parent[dep] = node;
                }
            }
        }

        std::string longest_node;
        double max_dist = -1.0;
        for (const auto& [node, d] : dist) {
            if (d > max_dist) {
                max_dist = d;
                longest_node = node;
            }
        }

        std::vector<std::string> path;
        std::string current = longest_node;
        while (!current.empty()) {
            path.push_back(current);
            current = parent.contains(current) ? parent[current] : "";
        }

        std::ranges::reverse(path);
        return path;
    }

    bool is_dag(const core::DependencyGraph& graph) {
        return !has_cycle(graph);
    }

    int count_paths(
        const core::DependencyGraph& graph,
        const std::string& start,
        const std::string& end
    ) {
        if (!graph.has_node(start) || !graph.has_node(end)) {
            return 0;
        }

        std::unordered_map<std::string, int> memo;

        std::function<int(const std::string&)> count_helper = [&](const std::string& node) -> int {
            if (node == end) {
                return 1;
            }

            if (memo.contains(node)) {
                return memo[node];
            }

            int count = 0;
            for (const auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                count += count_helper(dep);
            }

            memo[node] = count;
            return count;
        };

        return count_helper(start);
    }

} // namespace bha::graph