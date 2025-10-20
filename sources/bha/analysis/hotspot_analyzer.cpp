//
// Created by gregorian on 20/10/2025.
//

#include "bha/analysis/hotspot_analyzer.h"
#include "bha/graph/graph_algorithms.h"
#include "bha/utils/path_utils.h"
#include <algorithm>
#include <cmath>

namespace bha::analysis {

    core::Result<std::vector<core::Hotspot>> HotspotAnalyzer::identify_hotspots(
        const core::BuildTrace& trace,
        const Options& options
    ) {
        std::vector<core::Hotspot> all_hotspots;

        for (const auto& metric : options.metrics) {
            if (metric == "absolute_time") {
                if (auto result = find_slow_files(trace, options.top_n, options.threshold_ms); result.is_success()) {
                    all_hotspots.insert(all_hotspots.end(),
                                       result.value().begin(),
                                       result.value().end());
                }
            } else if (metric == "impact_score") {
                if (auto result = find_hot_headers(trace, trace.dependency_graph, options.top_n); result.is_success()) {
                    all_hotspots.insert(all_hotspots.end(),
                                       result.value().begin(),
                                       result.value().end());
                }
            } else if (metric == "critical_path") {
                if (auto result = find_critical_path(trace, trace.dependency_graph); result.is_success()) {
                    all_hotspots.insert(all_hotspots.end(),
                                       result.value().begin(),
                                       result.value().end());
                }
            }
        }

        std::ranges::sort(all_hotspots,
                          [](const core::Hotspot& a, const core::Hotspot& b) {
                              return a.impact_score > b.impact_score;
                          });

        std::vector<core::Hotspot> unique_hotspots;
        std::unordered_set<std::string> seen;
        for (const auto& hotspot : all_hotspots) {
            if (!seen.contains(hotspot.file_path)) {
                unique_hotspots.push_back(hotspot);
                seen.insert(hotspot.file_path);
            }
        }

        if (unique_hotspots.size() > static_cast<size_t>(options.top_n)) {
            unique_hotspots.resize(options.top_n);
        }

        return core::Result<std::vector<core::Hotspot>>::success(std::move(unique_hotspots));
    }

    core::Result<std::vector<core::Hotspot>> HotspotAnalyzer::find_slow_files(
        const core::BuildTrace& trace,
        const int top_n,
        const double threshold_ms
    ) {
        std::vector<core::Hotspot> hotspots;

        for (const auto& unit : trace.compilation_units) {
            if (unit.total_time_ms >= threshold_ms) {
                core::Hotspot hotspot;
                hotspot.file_path = unit.file_path;
                hotspot.time_ms = unit.total_time_ms;
                hotspot.impact_score = unit.total_time_ms;
                hotspot.num_dependent_files = 0;
                hotspot.category = "slow_compile";

                hotspots.push_back(hotspot);
            }
        }

        std::ranges::sort(hotspots.begin(), hotspots.end(),
                  [](const core::Hotspot& a, const core::Hotspot& b) {
                      return a.time_ms > b.time_ms;
                  });

        if (hotspots.size() > static_cast<size_t>(top_n)) {
            hotspots.resize(top_n);
        }

        return core::Result<std::vector<core::Hotspot>>::success(std::move(hotspots));
    }

    core::Result<std::vector<core::Hotspot>> HotspotAnalyzer::find_hot_headers(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const int top_n
    ) {
        auto impact_scores = calculate_all_impact_scores(trace, graph);

        std::vector<core::Hotspot> hotspots;
        for (const auto& [file, score] : impact_scores) {
            if (!is_header_file(file)) {
                continue;
            }

            core::Hotspot hotspot;
            hotspot.file_path = file;
            hotspot.time_ms = get_compile_time(file, trace);
            hotspot.impact_score = score;
            hotspot.num_dependent_files = count_dependents(file, graph);
            hotspot.category = "high_fanout";

            hotspots.push_back(hotspot);
        }

        std::ranges::sort(hotspots,
                          [](const core::Hotspot& a, const core::Hotspot& b) {
                              return a.impact_score > b.impact_score;
                          });

        if (hotspots.size() > static_cast<size_t>(top_n)) {
            hotspots.resize(top_n);
        }

        return core::Result<std::vector<core::Hotspot>>::success(std::move(hotspots));
    }

    core::Result<std::vector<core::Hotspot>> HotspotAnalyzer::find_critical_path(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph
    ) {
        std::unordered_map<std::string, double> compile_times;
        for (const auto& unit : trace.compilation_units) {
            compile_times[unit.file_path] = unit.total_time_ms;
        }

        const auto path = graph::find_critical_path(graph, compile_times);

        std::vector<core::Hotspot> hotspots;
        for (const auto& file : path) {
            core::Hotspot hotspot;
            hotspot.file_path = file;
            hotspot.time_ms = get_compile_time(file, trace);
            hotspot.impact_score = hotspot.time_ms;
            hotspot.num_dependent_files = count_dependents(file, graph);
            hotspot.category = "critical_path";

            hotspots.push_back(hotspot);
        }

        return core::Result<std::vector<core::Hotspot>>::success(std::move(hotspots));
    }

    double HotspotAnalyzer::calculate_impact_score(
        const std::string& file,
        const core::DependencyGraph& graph,
        const core::BuildTrace& trace
    ) {
        const double compile_time = get_compile_time(file, trace);
        const int num_dependents = count_dependents(file, graph);
        const double depth_weight = calculate_depth_weight(file, graph);

        return compile_time * num_dependents * depth_weight;
    }

    std::unordered_map<std::string, double> HotspotAnalyzer::calculate_all_impact_scores(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph
    ) {
        std::unordered_map<std::string, double> scores;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            scores[node] = calculate_impact_score(node, graph, trace);
        }

        return scores;
    }

    core::Result<std::vector<core::Hotspot>> HotspotAnalyzer::rank_by_metric(
        const std::vector<core::Hotspot>& hotspots,
        const std::string& metric
    ) {
        std::vector<core::Hotspot> ranked = hotspots;

        if (metric == "time") {
            std::ranges::sort(ranked,
                              [](const core::Hotspot& a, const core::Hotspot& b) {
                                  return a.time_ms > b.time_ms;
                              });
        } else if (metric == "impact") {
            std::ranges::sort(ranked,
                              [](const core::Hotspot& a, const core::Hotspot& b) {
                                  return a.impact_score > b.impact_score;
                              });
        } else if (metric == "dependents") {
            std::ranges::sort(ranked,
                              [](const core::Hotspot& a, const core::Hotspot& b) {
                                  return a.num_dependent_files > b.num_dependent_files;
                              });
        }

        return core::Result<std::vector<core::Hotspot>>::success(std::move(ranked));
    }

    double HotspotAnalyzer::get_compile_time(
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

    int HotspotAnalyzer::count_dependents(
        const std::string& file,
        const core::DependencyGraph& graph
    ) {
        return static_cast<int>(graph.get_reverse_dependencies(file).size());
    }

    double HotspotAnalyzer::calculate_depth_weight(
        const std::string& file,
        const core::DependencyGraph& graph
    ) {
        const int depth = graph::calculate_depth(graph, file);
        return 1.0 / (1.0 + depth);
    }

    bool HotspotAnalyzer::is_header_file(const std::string& file) {
        return utils::has_extension(file, ".h") ||
               utils::has_extension(file, ".hpp") ||
               utils::has_extension(file, ".hxx") ||
               utils::has_extension(file, ".hh");
    }

} // namespace bha::analysis