//
// Created by gregorian on 16/10/2025.
//

#include "bha/graph/graph_builder.h"
#include "bha/graph/graph_algorithms.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/string_utils.h"
#include "bha/utils/path_utils.h"

namespace bha::graph {

    core::Result<core::DependencyGraph> GraphBuilder::build_from_compilation_units(
        const std::vector<core::CompilationUnit>& units
    ) const
    {
        core::DependencyGraph graph;
        std::unordered_map<std::string, double> compile_times;

        for (const auto& unit : units) {
            graph.add_node(unit.file_path);
            compile_times[unit.file_path] = unit.total_time_ms;

            add_includes_to_graph(graph, unit);
        }

        if (merge_transitive_) {
            compute_transitive_closure(graph);
        }

        if (weight_by_compile_time_) {
            assign_weights(graph, compile_times);
        }

        return core::Result<core::DependencyGraph>::success(std::move(graph));
    }

    core::Result<core::DependencyGraph> GraphBuilder::build_from_trace(
        const core::BuildTrace& trace
    ) const {
        return build_from_compilation_units(trace.compilation_units);
    }

    void GraphBuilder::set_merge_transitive(const bool merge) {
        merge_transitive_ = merge;
    }

    void GraphBuilder::set_include_system_headers(const bool include) {
        include_system_headers_ = include;
    }

    void GraphBuilder::set_weight_by_compile_time(const bool weight) {
        weight_by_compile_time_ = weight;
    }

    core::Result<void> GraphBuilder::add_dependency_files(
        core::DependencyGraph& graph,
        const std::string& dependency_file_path
    ) const
    {
        auto deps_result = parse_dependency_file(dependency_file_path);
        if (!deps_result.is_success()) {
            return core::Result<void>::failure(deps_result.error());
        }

        const auto& deps = deps_result.value();

        if (deps.empty()) {
            return core::Result<void>::success();
        }

        const std::string source = deps[0];
        graph.add_node(source);

        for (size_t i = 1; i < deps.size(); ++i) {
            if (!include_system_headers_ && is_system_header(deps[i])) {
                continue;
            }

            graph.add_edge(source, deps[i], core::EdgeType::DIRECT_INCLUDE);
        }

        return core::Result<void>::success();
    }

    core::Result<void> GraphBuilder::add_compile_commands(
        core::DependencyGraph& graph,
        const std::string& compile_commands_path
    ) {
        if (const auto content = utils::read_file(compile_commands_path); !content) {
            return core::Result<void>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Failed to read compile_commands.json: " + compile_commands_path
            );
        }

        graph.add_node(compile_commands_path);
        return core::Result<void>::success();
    }

    void GraphBuilder::add_includes_to_graph(
        core::DependencyGraph& graph,
        const core::CompilationUnit& unit
    ) const
    {
        for (const auto& include : unit.direct_includes) {
            if (!include_system_headers_ && is_system_header(include)) {
                continue;
            }

            std::string normalized = normalize_include_path(include);
            graph.add_edge(unit.file_path, normalized, core::EdgeType::DIRECT_INCLUDE);
        }
    }

    void GraphBuilder::compute_transitive_closure(core::DependencyGraph& graph) {
        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            for (auto transitive = get_transitive_dependencies(graph, node); const auto& dep : transitive) {
                if (!graph.has_edge(node, dep)) {
                    graph.add_edge(node, dep, core::EdgeType::TRANSITIVE);
                }
            }
        }
    }

    void GraphBuilder::assign_weights(
        const core::DependencyGraph& graph,
        const std::unordered_map<std::string, double>& compile_times
    ) {
        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            for (auto edges = graph.get_edges(node); auto& edge : edges) {
                if (compile_times.contains(edge.target)) {
                    edge.weight = compile_times.at(edge.target);
                }
            }
        }
    }

    bool GraphBuilder::is_system_header(const std::string& path) {
        return utils::starts_with(path, "/usr/") ||
               utils::starts_with(path, "/opt/") ||
               utils::starts_with(path, "C:\\Program Files") ||
               utils::starts_with(path, "C:\\Windows") ||
               utils::contains(path, "/include/c++/") ||
               utils::contains(path, "/mingw/") ||
               utils::contains(path, "/msys/");
    }

    core::Result<std::vector<std::string>> GraphBuilder::parse_dependency_file(
        const std::string& dep_file_path
    )
    {
        const auto content = utils::read_file(dep_file_path);
        if (!content) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Failed to read dependency file: " + dep_file_path
            );
        }

        std::string cleaned = utils::replace_all(*content, "\\\n", " ");
        cleaned = utils::replace_all(cleaned, "\\", "");

        const size_t colon_pos = cleaned.find(':');
        if (colon_pos == std::string::npos) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::PARSE_ERROR,
                "Invalid dependency file format: " + dep_file_path
            );
        }

        const std::string target = utils::trim(cleaned.substr(0, colon_pos));
        const std::string deps_str = utils::trim(cleaned.substr(colon_pos + 1));

        const auto deps = utils::split(deps_str, ' ');

        std::vector<std::string> result;
        result.push_back(target);

        for (const auto& dep : deps) {
            if (std::string trimmed = utils::trim(dep); !trimmed.empty()) {
                result.push_back(normalize_include_path(trimmed));
            }
        }

        return core::Result<std::vector<std::string>>::success(std::move(result));
    }

    std::string GraphBuilder::normalize_include_path(const std::string& path) {
        return utils::normalize_path(path);
    }

    core::Result<core::DependencyGraph> build_dependency_graph(
        const std::vector<core::CompilationUnit>& units
    ) {
        constexpr GraphBuilder builder;
        return builder.build_from_compilation_units(units);
    }

    core::Result<core::DependencyGraph> build_dependency_graph(
        const core::BuildTrace& trace
    ) {
        constexpr GraphBuilder builder;
        return builder.build_from_trace(trace);
    }

    std::unordered_map<std::string, double> extract_compile_times(
        const std::vector<core::CompilationUnit>& units
    ) {
        std::unordered_map<std::string, double> times;

        for (const auto& unit : units) {
            times[unit.file_path] = unit.total_time_ms;
        }

        return times;
    }

    void merge_graphs(
        core::DependencyGraph& target,
        const core::DependencyGraph& source
    ) {
        const auto nodes = source.get_all_nodes();

        for (const auto& node : nodes) {
            target.add_node(node);
        }

        for (const auto& node : nodes) {
            for (auto edges = source.get_edges(node); const auto& edge : edges) {
                target.add_edge(node, edge);
            }
        }
    }

} // namespace bha::graph