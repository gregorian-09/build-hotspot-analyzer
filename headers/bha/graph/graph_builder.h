//
// Created by gregorian on 16/10/2025.
//

#ifndef GRAPH_BUILDER_H
#define GRAPH_BUILDER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace bha::graph {

    /**
     * @class GraphBuilder
     * Responsible for constructing and enriching dependency graphs from compilation units or build traces.
     *
     * The GraphBuilder class provides methods to construct a dependency graph based on parsed compilation data,
     * such as source files, includes, and timing information. It supports configurable options for merging transitive
     * dependencies, including system headers, and weighting edges by compile time.
     */
    class GraphBuilder {
    public:
        /**
         * Default constructor.
         */
        GraphBuilder() = default;

        /**
         * Builds a dependency graph from a collection of compilation units.
         *
         * Each compilation unit contributes nodes (source files) and edges (include relationships) to the resulting graph.
         *
         * @param units The list of compilation units to use for graph construction.
         * @return A Result containing the constructed dependency graph, or an error if graph construction fails.
         */
        core::Result<core::DependencyGraph> build_from_compilation_units(
            const std::vector<core::CompilationUnit>& units
        ) const;

        /**
         * Builds a dependency graph from a complete build trace.
         *
         * This variant extracts compilation data from a BuildTrace and produces a graph representing dependencies between files.
         *
         * @param trace The build trace to use for constructing the dependency graph.
         * @return A Result containing the constructed dependency graph, or an error on failure.
         */
        core::Result<core::DependencyGraph> build_from_trace(
            const core::BuildTrace& trace
        ) const;

        /**
         * Enables or disables merging of transitive dependencies.
         *
         * When enabled, indirect dependencies will be merged into direct edges for simplified graph visualization.
         *
         * @param merge True to merge transitive dependencies, false to keep them separate.
         */
        void set_merge_transitive(bool merge);

        /**
         * Controls whether system headers are included in the dependency graph.
         *
         * @param include True to include system headers, false to exclude them.
         */
        void set_include_system_headers(bool include);

        /**
         * Enables or disables weighting edges based on compile time.
         *
         * @param weight True to assign edge weights based on compile time, false for unweighted edges.
         */
        void set_weight_by_compile_time(bool weight);

        /**
         * Adds additional dependencies from a `.d` file (Makefile-style dependency file) into an existing graph.
         *
         * @param graph The dependency graph to update.
         * @param dependency_file_path Path to the dependency file.
         * @return A Result indicating success or failure.
         */
        core::Result<void> add_dependency_files(
            core::DependencyGraph& graph,
            const std::string& dependency_file_path
        ) const;

        /**
         * Adds dependencies inferred from a `compile_commands.json` file into the graph.
         *
         * @param graph The dependency graph to update.
         * @param compile_commands_path Path to the compile commands JSON file.
         * @return A Result indicating success or failure.
         */
        static core::Result<void> add_compile_commands(
            core::DependencyGraph& graph,
            const std::string& compile_commands_path
        );

    private:
        bool merge_transitive_ = false;        ///< Whether to merge transitive dependencies.
        bool include_system_headers_ = true;   ///< Whether to include system headers in the graph.
        bool weight_by_compile_time_ = true;   ///< Whether to weight edges by compile time.

        /**
         * Adds include relationships from a compilation unit into the graph.
         *
         * @param graph The graph to modify.
         * @param unit The compilation unit containing include data.
         */
        void add_includes_to_graph(
            core::DependencyGraph& graph,
            const core::CompilationUnit& unit
        ) const;

        /**
         * Computes and merges transitive dependencies into the graph if enabled.
         *
         * @param graph The dependency graph to process.
         */
        static void compute_transitive_closure(core::DependencyGraph& graph);

        /**
         * Assigns edge weights based on compile times of files.
         *
         * @param graph The dependency graph to update.
         * @param compile_times Map of file paths to their compile times.
         */
        static void assign_weights(
            const core::DependencyGraph& graph,
            const std::unordered_map<std::string, double>& compile_times
        );

        /**
         * Determines whether a given path corresponds to a system header.
         *
         * @param path Path to check.
         * @return True if the path represents a system header, false otherwise.
         */
        [[nodiscard]] static bool is_system_header(const std::string& path) ;

        /**
         * Parses a dependency file and extracts listed file paths.
         *
         * @param dep_file_path Path to the dependency file.
         * @return A Result containing the list of dependencies or an error on failure.
         */
        static core::Result<std::vector<std::string>> parse_dependency_file(
            const std::string& dep_file_path
        ) ;

        /**
         * Normalizes include paths for consistent graph representation.
         *
         * @param path The input include path.
         * @return A normalized version of the include path.
         */
        [[nodiscard]] static std::string normalize_include_path(const std::string& path) ;
    };

    /**
     * Builds a dependency graph from a list of compilation units.
     *
     * @param units The compilation units to process.
     * @return A Result containing the constructed dependency graph or an error on failure.
     */
    core::Result<core::DependencyGraph> build_dependency_graph(
        const std::vector<core::CompilationUnit>& units
    );

    /**
     * Builds a dependency graph from a full build trace.
     *
     * @param trace The build trace to analyze.
     * @return A Result containing the dependency graph or an error.
     */
    core::Result<core::DependencyGraph> build_dependency_graph(
        const core::BuildTrace& trace
    );

    /**
     * Extracts compile times from compilation units into a mapping.
     *
     * @param units The compilation units to analyze.
     * @return A map from file path to its compile time in milliseconds.
     */
    std::unordered_map<std::string, double> extract_compile_times(
        const std::vector<core::CompilationUnit>& units
    );

    /**
     * Merges the contents of one dependency graph into another.
     *
     * @param target The graph to merge into.
     * @param source The graph to merge from.
     */
    void merge_graphs(
        core::DependencyGraph& target,
        const core::DependencyGraph& source
    );

} // namespace bha::graph

#endif //GRAPH_BUILDER_H
