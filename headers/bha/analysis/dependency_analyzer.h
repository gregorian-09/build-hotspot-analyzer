//
// Created by gregorian on 20/10/2025.
//

#ifndef DEPENDENCY_ANALYZER_H
#define DEPENDENCY_ANALYZER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace bha::analysis {

    /**
     * @struct DependencyIssue
     * Represents an issue or inefficiency found in a dependency graph.
     *
     * This structure describes a detected dependency-related problem such as
     * circular includes, redundant dependencies, or deep nesting. It includes
     * contextual information and potential suggestions for resolving the issue.
     */
    struct DependencyIssue {
        /**
         * @enum Type
         * Types of dependency issues that can be detected.
         */
        enum class Type {
            CIRCULAR_DEPENDENCY,  ///< Circular include or dependency loop.
            REDUNDANT_INCLUDE,    ///< Header included unnecessarily multiple times.
            HIGH_FANOUT,          ///< A file is included by many dependents.
            DEEP_NESTING,         ///< Excessive include depth in dependency chain.
            MISSING_FORWARD_DECL  ///< Missing forward declaration that could reduce includes.
        };

        Type type; ///< The category of the dependency issue.
        std::vector<std::string> files; ///< Files involved in the issue.
        std::string description; ///< Human-readable description of the issue.
        std::string suggestion; ///< Suggested resolution or optimization.
        int severity; ///< Severity level (e.g., 1 = minor, 5 = critical).
    };

    /**
     * @class DependencyAnalyzer
     * Analyzes build dependency graphs to detect inefficiencies and potential issues.
     *
     * The DependencyAnalyzer examines the dependency structure of a build to identify
     * problematic patterns such as circular dependencies, redundant includes, and
     * files with excessive fanout or depth. It provides detailed reports and recommendations.
     */
    class DependencyAnalyzer {
    public:
        /**
         * Default constructor.
         */
        DependencyAnalyzer() = default;

        /**
         * Detects circular dependencies in a dependency graph.
         *
         * @param graph The dependency graph to analyze.
         * @return A Result containing a list of cycles, where each cycle is a vector of file paths.
         */
        static core::Result<std::vector<std::vector<std::string>>> detect_cycles(
            const core::DependencyGraph& graph
        );

        /**
         * Finds redundant includes in a specific file.
         *
         * @param file Path to the file to analyze.
         * @param graph The dependency graph.
         * @return A Result containing a list of redundant include file paths.
         */
        static core::Result<std::vector<std::string>> find_redundant_includes(
            const std::string& file,
            const core::DependencyGraph& graph
        );

        /**
         * Finds headers that are included by many other files (high fanout).
         *
         * @param graph The dependency graph.
         * @param min_dependents Minimum number of dependents to be considered high fanout.
         * @return A Result containing a list of headers with high fanout.
         */
        static core::Result<std::vector<std::string>> find_fanout_headers(
            const core::DependencyGraph& graph,
            int min_dependents = 10
        );

        /**
         * Calculates the include depth for all files in the graph.
         *
         * @param graph The dependency graph.
         * @return A Result mapping each file to its include depth.
         */
        static core::Result<std::unordered_map<std::string, int>> calculate_include_depths(
            const core::DependencyGraph& graph
        );

        /**
         * Performs a full dependency analysis to identify all types of issues.
         *
         * @param graph The dependency graph to analyze.
         * @return A Result containing a list of detected dependency issues.
         */
        static core::Result<std::vector<DependencyIssue>> analyze_all_issues(
            const core::DependencyGraph& graph
        );

        /**
         * Calculates the transitive include depth for a given file.
         *
         * @param file Path to the file to analyze.
         * @param graph The dependency graph.
         * @return The calculated transitive include depth.
         */
        static int calculate_transitive_depth(
            const std::string& file,
            const core::DependencyGraph& graph
        );

        /**
         * Retrieves the include tree for a specific file.
         *
         * @param file The file for which to generate the include tree.
         * @param graph The dependency graph.
         * @param max_depth Optional limit for include depth (-1 means no limit).
         * @return A list of included files in the dependency order.
         */
        static std::vector<std::string> get_include_tree(
            const std::string& file,
            const core::DependencyGraph& graph,
            int max_depth = -1
        );

        /**
         * Finds dependencies that are shared among multiple files.
         *
         * @param graph The dependency graph.
         * @return A Result mapping shared dependencies to the files that depend on them.
         */
        static core::Result<std::unordered_map<std::string, std::vector<std::string>>>
        find_common_dependencies(
            const core::DependencyGraph& graph
        );

    private:
        /**
         * Checks whether a file is a system header.
         *
         * @param file The file path to check.
         * @return True if the file is a system header; otherwise, false.
         */
        [[nodiscard]] static bool is_system_header(const std::string& file) ;

        /**
         * Estimates the severity of a detected dependency issue.
         *
         * @param type The type of issue.
         * @param magnitude A numeric indicator of the issue's impact (e.g., number of files affected).
         * @return A severity score, typically ranging from 1 (minor) to 5 (critical).
         */
        [[nodiscard]] static int estimate_severity(DependencyIssue::Type type, int magnitude) ;
    };

} // namespace bha::analysis

#endif //DEPENDENCY_ANALYZER_H
