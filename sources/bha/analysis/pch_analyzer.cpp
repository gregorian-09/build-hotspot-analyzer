//
// Created by gregorian on 20/10/2025.
//

#include "bha/analysis/pch_analyzer.h"
#include "bha/utils/string_utils.h"
#include <algorithm>

namespace bha::analysis {

    core::Result<std::vector<PCHCandidate>> PCHAnalyzer::identify_pch_candidates(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const int top_n,
        const double min_inclusion_ratio
    ) {
        auto inclusion_counts = count_header_inclusions(graph);
        const auto compile_times = estimate_header_compile_times(trace, graph);

        const int total_files = static_cast<int>(trace.compilation_units.size());
        const int min_inclusions = static_cast<int>(total_files * min_inclusion_ratio);

        std::vector<PCHCandidate> candidates;

        for (const auto& [header, count] : inclusion_counts) {
            if (count < min_inclusions) {
                continue;
            }

            if (is_system_header(header)) {
                continue;
            }

            const double compile_time = compile_times.contains(header) ? compile_times.at(header) : 0.0;
            const double potential_savings = compile_time * count * 0.8;
            const double benefit_score = calculate_pch_benefit_score(count, compile_time, total_files);

            PCHCandidate candidate;
            candidate.header = header;
            candidate.inclusion_count = count;
            candidate.average_compile_time_ms = compile_time;
            candidate.potential_savings_ms = potential_savings;
            candidate.benefit_score = benefit_score;

            candidates.push_back(candidate);
        }

        std::ranges::sort(candidates,
                          [](const PCHCandidate& a, const PCHCandidate& b) {
                              return a.benefit_score > b.benefit_score;
                          });

        if (candidates.size() > static_cast<size_t>(top_n)) {
            candidates.resize(top_n);
        }

        return core::Result<std::vector<PCHCandidate>>::success(std::move(candidates));
    }

    core::Result<core::PCHMetrics> PCHAnalyzer::analyze_pch_effectiveness(
        const core::BuildTrace& trace,
        const std::string& pch_file
    ) {
        core::PCHMetrics metrics;

        metrics.pch_file = pch_file;
        metrics.pch_build_time_ms = 0.0;

        for (const auto& unit : trace.compilation_units) {
            if (unit.file_path == pch_file) {
                metrics.pch_build_time_ms = unit.total_time_ms;
                break;
            }
        }

        metrics.files_using_pch = 0;
        metrics.total_time_saved_ms = 0.0;

        for (const auto& unit : trace.compilation_units) {
            bool uses_pch = false;
            for (const auto& include : unit.direct_includes) {
                if (include == pch_file) {
                    uses_pch = true;
                    break;
                }
            }

            if (uses_pch) {
                metrics.files_using_pch++;
                metrics.total_time_saved_ms += metrics.pch_build_time_ms * 0.8;
            }
        }

        if (metrics.files_using_pch > 0) {
            metrics.average_time_saved_per_file_ms =
                metrics.total_time_saved_ms / metrics.files_using_pch;
        }

        if (!trace.compilation_units.empty()) {
            metrics.pch_hit_rate =
                (static_cast<double>(metrics.files_using_pch) / static_cast<double>(trace.compilation_units.size())) * 100.0;
        }

        return core::Result<core::PCHMetrics>::success(std::move(metrics));
    }

    core::Result<std::vector<std::string>> PCHAnalyzer::suggest_pch_additions(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const std::string& current_pch_file
    ) {
        auto candidates_result = identify_pch_candidates(trace, graph, 20, 0.5);
        if (!candidates_result.is_success()) {
            return core::Result<std::vector<std::string>>::failure(candidates_result.error());
        }

        std::vector<std::string> suggestions;
        for (const auto& candidate : candidates_result.value()) {
            if (candidate.header != current_pch_file) {
                suggestions.push_back(candidate.header);
            }
        }

        return core::Result<std::vector<std::string>>::success(std::move(suggestions));
    }

    core::Result<std::vector<std::string>> PCHAnalyzer::suggest_pch_removals(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const std::string& current_pch_file
    ) {
        std::vector<std::string> removals;

        const auto deps = graph.get_dependencies(current_pch_file);
        const auto inclusion_counts = count_header_inclusions(graph);

        const int total_files = static_cast<int>(trace.compilation_units.size());

        for (const auto& dep : deps) {
            const int count = inclusion_counts.contains(dep) ? inclusion_counts.at(dep) : 0;

            if (const double usage_ratio = static_cast<double>(count) / total_files; usage_ratio < 0.1) {
                removals.push_back(dep);
            }
        }

        return core::Result<std::vector<std::string>>::success(std::move(removals));
    }

    double PCHAnalyzer::calculate_pch_benefit_score(
        const int inclusion_count,
        const double compile_time_ms,
        const int total_files
    ) {
        const double usage_ratio = static_cast<double>(inclusion_count) / total_files;
        const double time_weight = compile_time_ms / 1000.0;

        return usage_ratio * time_weight * inclusion_count;
    }

    core::Result<double> PCHAnalyzer::estimate_pch_savings(
        const std::vector<std::string>& pch_headers,
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph
    ) {
        const auto inclusion_counts = count_header_inclusions(graph);
        const auto compile_times = estimate_header_compile_times(trace, graph);

        double total_savings = 0.0;

        for (const auto& header : pch_headers) {
            const int count = inclusion_counts.contains(header) ? inclusion_counts.at(header) : 0;
            const double time = compile_times.contains(header) ? compile_times.at(header) : 0.0;

            total_savings += time * count * 0.8;
        }

        return core::Result<double>::success(total_savings);
    }

    std::unordered_map<std::string, int> PCHAnalyzer::count_header_inclusions(
        const core::DependencyGraph& graph
    ) {
        std::unordered_map<std::string, int> counts;

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            for (auto deps = graph.get_dependencies(node); const auto& dep : deps) {
                counts[dep]++;
            }
        }

        return counts;
    }

    std::unordered_map<std::string, double> PCHAnalyzer::estimate_header_compile_times(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph
    ) {
        std::unordered_map<std::string, double> times;

        for (const auto& unit : trace.compilation_units) {
            if (unit.preprocessing_time_ms > 0) {
                if (auto includes = graph.get_dependencies(unit.file_path); !includes.empty()) {
                    const double avg_time = unit.preprocessing_time_ms / static_cast<double>(includes.size());
                    for (const auto& include : includes) {
                        times[include] += avg_time;
                    }
                }
            }
        }

        for (auto& [header, time] : times) {
            if (auto dependents = graph.get_reverse_dependencies(header); !dependents.empty()) {
                time /= static_cast<double>(dependents.size());
            }
        }

        return times;
    }

    bool PCHAnalyzer::is_system_header(const std::string& header) {
        return utils::starts_with(header, "/usr/") ||
               utils::starts_with(header, "/opt/") ||
               utils::starts_with(header, "C:\\Program Files") ||
               utils::contains(header, "/include/c++/") ||
               utils::contains(header, "/mingw/");
    }

} // namespace bha::analysis