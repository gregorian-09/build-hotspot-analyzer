//
// Created by gregorian on 20/10/2025.
//

#include "bha/analysis/dependency_analyzer.h"
#include "bha/graph/graph_algorithms.h"
#include "bha/utils/string_utils.h"
#include <queue>

namespace bha::analysis {

    core::Result<std::vector<std::vector<std::string>>> DependencyAnalyzer::detect_cycles(
        const core::DependencyGraph& graph
    ) {
        auto cycles = graph::find_cycles(graph);
        return core::Result<std::vector<std::vector<std::string>>>::success(std::move(cycles));
    }

    core::Result<std::vector<std::string>> DependencyAnalyzer::find_redundant_includes(
        const std::string& file,
        const core::DependencyGraph& graph
    ) {
        std::vector<std::string> redundant;

        if (!graph.has_node(file)) {
            return core::Result<std::vector<std::string>>::success(std::move(redundant));
        }

        for (const auto direct_deps = graph.get_dependencies(file); const auto& dep : direct_deps) {
            auto transitive = graph::get_transitive_dependencies(graph, dep);

            for (const auto& other_dep : direct_deps) {
                if (dep != other_dep) {
                    if (std::ranges::find(transitive, other_dep) != transitive.end()) {
                        redundant.push_back(other_dep);
                    }
                }
            }
        }

        std::sort(redundant.begin(), redundant.end());
        redundant.erase(std::unique(redundant.begin(), redundant.end()), redundant.end());

        return core::Result<std::vector<std::string>>::success(std::move(redundant));
    }

    core::Result<std::vector<std::string>> DependencyAnalyzer::find_fanout_headers(
        const core::DependencyGraph& graph,
        const int min_dependents
    ) {
        std::vector<std::string> fanout_headers;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            if (auto dependents = graph.get_reverse_dependencies(node); static_cast<int>(dependents.size()) >= min_dependents) {
                fanout_headers.push_back(node);
            }
        }

        std::sort(fanout_headers.begin(), fanout_headers.end(),
                  [&graph](const std::string& a, const std::string& b) {
                      return graph.get_reverse_dependencies(a).size() >
                             graph.get_reverse_dependencies(b).size();
                  });

        return core::Result<std::vector<std::string>>::success(std::move(fanout_headers));
    }

    core::Result<std::unordered_map<std::string, int>> DependencyAnalyzer::calculate_include_depths(
        const core::DependencyGraph& graph
    ) {
        auto depths = graph::calculate_all_depths(graph);
        return core::Result<std::unordered_map<std::string, int>>::success(std::move(depths));
    }

    core::Result<std::vector<DependencyIssue>> DependencyAnalyzer::analyze_all_issues(
        const core::DependencyGraph& graph
    ) {
        std::vector<DependencyIssue> issues;

        if (auto cycles_result = detect_cycles(graph); cycles_result.is_success()) {
            for (const auto& cycle : cycles_result.value()) {
                DependencyIssue issue;
                issue.type = DependencyIssue::Type::CIRCULAR_DEPENDENCY;
                issue.files = cycle;
                issue.description = "Circular dependency detected involving " +
                                  std::to_string(cycle.size()) + " files";
                issue.suggestion = "Break the cycle by using forward declarations or restructuring";
                issue.severity = estimate_severity(issue.type, static_cast<int>(cycle.size()));
                issues.push_back(issue);
            }
        }

        if (auto fanout_result = find_fanout_headers(graph, 10); fanout_result.is_success()) {
            for (const auto& header : fanout_result.value()) {
                int dependents = static_cast<int>(graph.get_reverse_dependencies(header).size());

                DependencyIssue issue;
                issue.type = DependencyIssue::Type::HIGH_FANOUT;
                issue.files = {header};
                issue.description = "Header included by " + std::to_string(dependents) + " files";
                issue.suggestion = "Consider splitting this header or using forward declarations";
                issue.severity = estimate_severity(issue.type, dependents);
                issues.push_back(issue);
            }
        }

        if (auto depths_result = calculate_include_depths(graph); depths_result.is_success()) {
            for (const auto& [file, depth] : depths_result.value()) {
                if (depth > 10) {
                    DependencyIssue issue;
                    issue.type = DependencyIssue::Type::DEEP_NESTING;
                    issue.files = {file};
                    issue.description = "Include depth of " + std::to_string(depth);
                    issue.suggestion = "Reduce dependency chain length";
                    issue.severity = estimate_severity(issue.type, depth);
                    issues.push_back(issue);
                }
            }
        }

        std::sort(issues.begin(), issues.end(),
                  [](const DependencyIssue& a, const DependencyIssue& b) {
                      return a.severity > b.severity;
                  });

        return core::Result<std::vector<DependencyIssue>>::success(std::move(issues));
    }

    int DependencyAnalyzer::calculate_transitive_depth(
        const std::string& file,
        const core::DependencyGraph& graph
    ) {
        return graph::calculate_depth(graph, file);
    }

    std::vector<std::string> DependencyAnalyzer::get_include_tree(
        const std::string& file,
        const core::DependencyGraph& graph,
        const int max_depth
    ) {
        std::vector<std::string> tree;
        std::unordered_set<std::string> visited;

        std::queue<std::pair<std::string, int>> queue;
        queue.emplace(file, 0);
        visited.insert(file);

        while (!queue.empty()) {
            auto [current, depth] = queue.front();
            queue.pop();

            if (max_depth >= 0 && depth >= max_depth) {
                continue;
            }

            tree.push_back(current);

            for (auto deps = graph.get_dependencies(current); const auto& dep : deps) {
                if (!visited.contains(dep)) {
                    visited.insert(dep);
                    queue.emplace(dep, depth + 1);
                }
            }
        }

        return tree;
    }

    core::Result<std::unordered_map<std::string, std::vector<std::string>>>
    DependencyAnalyzer::find_common_dependencies(
        const core::DependencyGraph& graph
    ) {
        std::unordered_map<std::string, std::vector<std::string>> common_deps;

        const auto nodes = graph.get_all_nodes();
        std::unordered_map<std::string, std::vector<std::string>> dep_to_files;

        for (const auto& node : nodes) {
            auto deps = graph.get_dependencies(node);
            for (const auto& dep : deps) {
                dep_to_files[dep].push_back(node);
            }
        }

        for (const auto& [dep, files] : dep_to_files) {
            if (files.size() > 1) {
                common_deps[dep] = files;
            }
        }

        return core::Result<std::unordered_map<std::string, std::vector<std::string>>>::success(
            std::move(common_deps)
        );
    }

    bool DependencyAnalyzer::is_system_header(const std::string& file) {
        return utils::starts_with(file, "/usr/") ||
               utils::starts_with(file, "/opt/") ||
               utils::starts_with(file, "C:\\Program Files") ||
               utils::contains(file, "/include/c++/");
    }

    int DependencyAnalyzer::estimate_severity(const DependencyIssue::Type type, const int magnitude) {
        switch (type) {
            case DependencyIssue::Type::CIRCULAR_DEPENDENCY:
                return 90 + std::min(magnitude, 10);

            case DependencyIssue::Type::HIGH_FANOUT:
                return 50 + std::min(magnitude, 50);

            case DependencyIssue::Type::DEEP_NESTING:
                return 30 + std::min(magnitude * 5, 50);

            case DependencyIssue::Type::REDUNDANT_INCLUDE:
                return 20;

            case DependencyIssue::Type::MISSING_FORWARD_DECL:
                return 40;

            default:
                return 50;
        }
    }

} // namespace bha::analysis