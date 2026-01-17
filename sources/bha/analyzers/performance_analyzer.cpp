//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/performance_analyzer.hpp"

#include "bha/graph/graph.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace bha::analyzers
{
    namespace {

        Duration calculate_percentile(std::vector<Duration>& times, const double percentile) {
            if (times.empty()) {
                return Duration::zero();
            }

            std::ranges::sort(times);

            const auto index = static_cast<std::size_t>(
                static_cast<double>(times.size() - 1) * percentile / 100.0
            );
            return times[index];
        }

        /**
         * Builds a dependency graph from the build trace.
         *
         * Nodes are source files and headers.
         * Edges represent include dependencies (header -> includer).
         * Node weights are compile/parse times.
         *
         * The critical path in this graph represents the longest chain of
         * dependencies that must be processed sequentially.
         */
        graph::DirectedGraph build_dependency_graph(const BuildTrace& trace) {
            graph::DirectedGraph g;

            // Map from file path to its compile time
            std::unordered_map<std::string, Duration> file_times;

            // First pass: add all source files as nodes with their compile times
            for (const auto& unit : trace.units) {
                std::string source = unit.source_file.string();
                file_times[source] = unit.metrics.total_time;
                g.add_node(source, unit.metrics.total_time);
            }

            // Second pass: add headers and include edges
            for (const auto& unit : trace.units) {
                std::string source = unit.source_file.string();

                for (const auto& inc : unit.includes) {
                    std::string header = inc.header.string();

                    // Add header node if not already added
                    if (!g.has_node(header)) {
                        g.add_node(header, inc.parse_time);
                        file_times[header] = inc.parse_time;
                    }

                    // Edge from header to source (header must be parsed before source)
                    // Weight is the parse time of the header
                    graph::EdgeWeight weight;
                    weight.time = inc.parse_time;
                    weight.count = 1;
                    g.add_edge(header, source, weight);
                }
            }

            return g;
        }

        /**
         * Identifies bottleneck files that limit parallelism.
         *
         * A bottleneck is a file that:
         * 1. Takes a long time to compile
         * 2. Has many dependents waiting on it
         * 3. Is on the critical path
         *
         * Uses a scoring system based on ClangBuildAnalyzer's approach:
         * bottleneck_score = compile_time * (1 + log(dependent_count))
         */
        struct BottleneckInfo {
            fs::path file;
            Duration compile_time{};
            std::size_t dependent_count{};
            double bottleneck_score{};
            bool on_critical_path{};
        };

        std::vector<BottleneckInfo> identify_bottlenecks(
            const graph::DirectedGraph& g,
            const std::vector<std::string>& critical_path_nodes,
            std::size_t max_results = 10
        ) {
            const std::unordered_set cp_set(
                critical_path_nodes.begin(),
                critical_path_nodes.end()
            );

            std::vector<BottleneckInfo> bottlenecks;

            for (const auto& node : g.nodes()) {
                auto successors = g.successors(node);
                Duration node_time = g.node_time(node);
                const std::size_t dep_count = successors.size();

                // Calculate bottleneck score
                // Files with long compile times and many dependents are bigger bottlenecks
                const double time_ms = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(node_time).count()
                );

                // Log scaling for dependent count to avoid extreme scores
                double dep_factor = 1.0;
                if (dep_count > 0) {
                    dep_factor = 1.0 + std::log(static_cast<double>(dep_count + 1));
                }

                double score = time_ms * dep_factor;

                // Bonus for being on critical path
                const bool on_cp = cp_set.contains(node);
                if (on_cp) {
                    score *= 1.5;
                }

                if (score > 0) {
                    BottleneckInfo info;
                    info.file = node;
                    info.compile_time = node_time;
                    info.dependent_count = dep_count;
                    info.bottleneck_score = score;
                    info.on_critical_path = on_cp;
                    bottlenecks.push_back(std::move(info));
                }
            }

            std::ranges::sort(bottlenecks,
                              [](const auto& a, const auto& b) {
                                  return a.bottleneck_score > b.bottleneck_score;
                              });

            // Return top N
            if (bottlenecks.size() > max_results) {
                bottlenecks.resize(max_results);
            }

            return bottlenecks;
        }

    }  // namespace

    Result<AnalysisResult, Error> PerformanceAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& options
    ) const {
        AnalysisResult result;

        if (trace.units.empty()) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        result.performance.total_build_time = trace.total_time;
        result.performance.total_files = trace.units.size();

        std::vector<Duration> compile_times;
        compile_times.reserve(trace.units.size());

        Duration sequential_total = Duration::zero();

        for (const auto& unit : trace.units) {
            Duration compile_time = unit.metrics.total_time;
            compile_times.push_back(compile_time);
            sequential_total += compile_time;

            FileAnalysisResult file_result;
            file_result.file = unit.source_file;
            file_result.compile_time = compile_time;
            file_result.frontend_time = unit.metrics.frontend_time;
            file_result.backend_time = unit.metrics.backend_time;
            file_result.breakdown = unit.metrics.breakdown;
            file_result.include_count = unit.includes.size();
            file_result.template_count = unit.templates.size();

            result.files.push_back(std::move(file_result));
        }

        result.performance.sequential_time = sequential_total;
        result.performance.parallel_time = trace.total_time;
        auto dep_graph = build_dependency_graph(trace);

        std::vector<std::string> critical_path_nodes;
        if (auto cp_result = graph::find_critical_path(dep_graph); cp_result.is_ok()) {
            for (const auto& cp = cp_result.value(); const auto& node : cp.critical_path.nodes) {
                result.performance.critical_path.emplace_back(node);
                critical_path_nodes.push_back(node);
            }
        } else {
            if (!result.files.empty()) {
                auto max_it = std::ranges::max_element(result.files,
                                                       [](const auto& a, const auto& b) {
                                                           return a.compile_time < b.compile_time;
                                                       });
                result.performance.critical_path.push_back(max_it->file);
                critical_path_nodes.push_back(max_it->file.string());
            }
        }

        // Calculate parallelism efficiency (speedup factor)
        // This is the ratio of sequential time to parallel time
        // A value > 1.0 indicates parallel speedup
        // A value of N means the build achieved N-way parallelism on average
        if (trace.total_time.count() > 0) {
            result.performance.parallelism_efficiency =
                static_cast<double>(sequential_total.count()) /
                static_cast<double>(trace.total_time.count());
        } else {
            result.performance.parallelism_efficiency = 1.0;
        }

        if (!compile_times.empty()) {
            auto total = std::accumulate(compile_times.begin(), compile_times.end(),
                                         Duration::zero());
            result.performance.avg_file_time = total / compile_times.size();
            result.performance.median_file_time = calculate_percentile(compile_times, 50.0);
            result.performance.p90_file_time = calculate_percentile(compile_times, 90.0);
            result.performance.p99_file_time = calculate_percentile(compile_times, 99.0);
        }

        std::size_t files_with_memory = 0;
        for (const auto& file : result.files) {
            if (file.memory.has_data()) {
                result.performance.total_memory.peak_memory_bytes += file.memory.peak_memory_bytes;
                result.performance.total_memory.frontend_peak_bytes += file.memory.frontend_peak_bytes;
                result.performance.total_memory.backend_peak_bytes += file.memory.backend_peak_bytes;
                result.performance.total_memory.max_stack_bytes += file.memory.max_stack_bytes;

                if (file.memory.peak_memory_bytes > result.performance.peak_memory.peak_memory_bytes) {
                    result.performance.peak_memory = file.memory;
                }

                ++files_with_memory;
            }
        }

        if (files_with_memory > 0) {
            result.performance.average_memory.peak_memory_bytes =
                result.performance.total_memory.peak_memory_bytes / files_with_memory;
            result.performance.average_memory.frontend_peak_bytes =
                result.performance.total_memory.frontend_peak_bytes / files_with_memory;
            result.performance.average_memory.backend_peak_bytes =
                result.performance.total_memory.backend_peak_bytes / files_with_memory;
            result.performance.average_memory.max_stack_bytes =
                result.performance.total_memory.max_stack_bytes / files_with_memory;
        }

        std::ranges::sort(result.files,
                          [](const auto& a, const auto& b) {
                              return a.compile_time > b.compile_time;
                          });

        Duration slow_threshold = options.min_duration_threshold;
        std::size_t slowest_count = 0;
        auto bottlenecks = identify_bottlenecks(dep_graph, critical_path_nodes, 20);

        for (const auto& file : result.files) {
            if (file.compile_time >= slow_threshold) {
                ++slowest_count;
                if (result.performance.slowest_files.size() < 20) {
                    result.performance.slowest_files.push_back(file);
                }
            }
        }

        result.performance.slowest_file_count = slowest_count;

        if (trace.total_time.count() > 0) {
            for (auto& file : result.files) {
                file.time_percent = 100.0 *
                    static_cast<double>(file.compile_time.count()) /
                    static_cast<double>(trace.total_time.count());
            }
        }

        for (std::size_t i = 0; i < result.files.size(); ++i) {
            result.files[i].rank = i + 1;
        }

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_performance_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<PerformanceAnalyzer>()
        );
    }
}  // namespace bha::analyzers