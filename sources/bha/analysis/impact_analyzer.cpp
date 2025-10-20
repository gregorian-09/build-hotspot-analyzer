//
// Created by gregorian on 20/10/2025.
//

#include "bha/analysis/impact_analyzer.h"
#include "bha/graph/graph_algorithms.h"
#include "bha/utils/path_utils.h"
#include <algorithm>

namespace bha::analysis {

    core::Result<core::ImpactReport> ImpactAnalyzer::analyze_change_impact(
        const std::string& changed_file,
        const core::DependencyGraph& graph,
        const core::BuildTrace& trace
    ) {
        core::ImpactReport report;

        auto affected_result = get_affected_files(changed_file, graph);
        if (!affected_result.is_success()) {
            return core::Result<core::ImpactReport>::failure(affected_result.error());
        }

        report.affected_files = affected_result.value();
        report.num_cascading_rebuilds = static_cast<int>(report.affected_files.size());

        if (auto rebuild_time_result = estimate_rebuild_time(report.affected_files, trace); rebuild_time_result.is_success()) {
            report.estimated_rebuild_time_ms = rebuild_time_result.value();
        }

        if (auto fragile_result = find_fragile_headers(graph, 10); fragile_result.is_success()) {
            report.fragile_headers = fragile_result.value();
        }

        return core::Result<core::ImpactReport>::success(std::move(report));
    }

    core::Result<std::vector<std::string>> ImpactAnalyzer::get_affected_files(
        const std::string& changed_file,
        const core::DependencyGraph& graph
    ) {
        if (!graph.has_node(changed_file)) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::INVALID_ARGUMENT,
                "File not found in dependency graph: " + changed_file
            );
        }

        auto affected = graph::get_transitive_dependents(graph, changed_file);

        return core::Result<std::vector<std::string>>::success(std::move(affected));
    }

    core::Result<double> ImpactAnalyzer::estimate_rebuild_time(
        const std::vector<std::string>& affected_files,
        const core::BuildTrace& trace
    ) {
        double total_time = 0.0;

        for (const auto& file : affected_files) {
            total_time += get_compile_time(file, trace);
        }

        return core::Result<double>::success(total_time);
    }

    core::Result<std::vector<std::string>> ImpactAnalyzer::find_fragile_headers(
        const core::DependencyGraph& graph,
        const int threshold
    ) {
        std::vector<std::string> fragile;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            if (!is_header_file(node)) {
                continue;
            }

            if (auto dependents = graph.get_reverse_dependencies(node); static_cast<int>(dependents.size()) >= threshold) {
                fragile.push_back(node);
            }
        }

        std::ranges::sort(fragile,
                          [&graph](const std::string& a, const std::string& b) {
                              return graph.get_reverse_dependencies(a).size() >
                                  graph.get_reverse_dependencies(b).size();
                          });

        return core::Result<std::vector<std::string>>::success(std::move(fragile));
    }

    core::Result<std::unordered_map<std::string, core::ImpactReport>>
    ImpactAnalyzer::analyze_all_files(
        const core::DependencyGraph& graph,
        const core::BuildTrace& trace
    ) {
        std::unordered_map<std::string, core::ImpactReport> reports;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            if (auto result = analyze_change_impact(node, graph, trace); result.is_success()) {
                reports[node] = result.value();
            }
        }

        return core::Result<std::unordered_map<std::string, core::ImpactReport>>::success(
            std::move(reports)
        );
    }

    double ImpactAnalyzer::calculate_fragility_score(
        const std::string& file,
        const core::DependencyGraph& graph,
        const core::BuildTrace& trace
    ) {
        const auto dependents = graph.get_reverse_dependencies(file);
        const int num_dependents = static_cast<int>(dependents.size());

        double total_dependent_time = 0.0;
        for (const auto& dep : dependents) {
            total_dependent_time += get_compile_time(dep, trace);
        }

        const double file_time = get_compile_time(file, trace);

        return (file_time + total_dependent_time) * num_dependents;
    }

    core::Result<std::unordered_map<std::string, double>>
    ImpactAnalyzer::calculate_all_fragility_scores(
        const core::DependencyGraph& graph,
        const core::BuildTrace& trace
    ) {
        std::unordered_map<std::string, double> scores;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            scores[node] = calculate_fragility_score(node, graph, trace);
        }

        return core::Result<std::unordered_map<std::string, double>>::success(
            std::move(scores)
        );
    }

    core::Result<std::vector<std::string>> ImpactAnalyzer::simulate_header_removal(
        const std::string& header,
        const core::DependencyGraph& graph
    ) {
        if (!graph.has_node(header)) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::INVALID_ARGUMENT,
                "Header not found in dependency graph: " + header
            );
        }

        auto affected = graph.get_reverse_dependencies(header);

        return core::Result<std::vector<std::string>>::success(std::move(affected));
    }

    int ImpactAnalyzer::count_cascading_rebuilds(
        const std::string& file,
        const core::DependencyGraph& graph
    ) {
        const auto transitive = graph::get_transitive_dependents(graph, file);
        return static_cast<int>(transitive.size());
    }

    double ImpactAnalyzer::get_compile_time(
        const std::string& file,
        const core::BuildTrace& trace
    ) {
        for (const auto& unit : trace.compilation_units) {
            if (unit.file_path == file) {
                return unit.total_time_ms;
            }
        }
        return 0.0;
    }

    bool ImpactAnalyzer::is_header_file(const std::string& file) {
        return utils::has_extension(file, ".h") ||
               utils::has_extension(file, ".hpp") ||
               utils::has_extension(file, ".hxx") ||
               utils::has_extension(file, ".hh");
    }

} // namespace bha::analysis