//
// Created by gregorian on 20/10/2025.
//

#ifndef IMPACT_ANALYZER_H
#define IMPACT_ANALYZER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace bha::analysis {

    /**
     * @class ImpactAnalyzer
     * Analyzes the propagation and rebuild impact of file changes in a build dependency graph.
     *
     * The ImpactAnalyzer evaluates how modifications to specific source or header files
     * influence downstream dependencies, rebuild times, and overall build fragility.
     * It provides methods for estimating rebuild costs, identifying fragile headers,
     * and quantifying dependency sensitivity.
     */
    class ImpactAnalyzer {
    public:
        /**
         * Default constructor.
         */
        ImpactAnalyzer() = default;

        /**
         * Performs a full impact analysis for a given file change.
         *
         * Determines affected files, estimates rebuild time, and constructs
         * a comprehensive @ref core::ImpactReport.
         *
         * @param changed_file The file that has been modified.
         * @param graph The dependency graph representing relationships between files.
         * @param trace The build trace containing compile-time metrics.
         * @return A Result containing a @ref core::ImpactReport detailing the impact.
         */
        static core::Result<core::ImpactReport> analyze_change_impact(
            const std::string& changed_file,
            const core::DependencyGraph& graph,
            const core::BuildTrace& trace
        );

        /**
         * Retrieves all files affected by changes in a given file.
         *
         * Traverses the dependency graph to find direct and transitive dependents.
         *
         * @param changed_file The modified file path.
         * @param graph The dependency graph to analyze.
         * @return A Result containing the list of affected file paths.
         */
        static core::Result<std::vector<std::string>> get_affected_files(
            const std::string& changed_file,
            const core::DependencyGraph& graph
        );

        /**
         * Estimates total rebuild time for a set of affected files.
         *
         * Uses compile-time data from the build trace to calculate cumulative
         * rebuild cost after a change.
         *
         * @param affected_files The list of files to be rebuilt.
         * @param trace The build trace containing timing information.
         * @return A Result containing the total estimated rebuild time in milliseconds.
         */
        static core::Result<double> estimate_rebuild_time(
            const std::vector<std::string>& affected_files,
            const core::BuildTrace& trace
        );

        /**
         * Identifies header files that cause widespread rebuilds when modified.
         *
         * Headers exceeding the dependent threshold are considered fragile,
         * as they induce many recompilations when changed.
         *
         * @param graph The dependency graph.
         * @param threshold Minimum number of dependents to classify a header as fragile.
         * @return A Result containing a list of fragile header file paths.
         */
        static core::Result<std::vector<std::string>> find_fragile_headers(
            const core::DependencyGraph& graph,
            int threshold = 10
        );

        /**
         * Performs impact analysis for all files in the dependency graph.
         *
         * Generates a map associating each file with a detailed @ref core::ImpactReport.
         *
         * @param graph The dependency graph.
         * @param trace The build trace.
         * @return A Result mapping each file path to its corresponding impact report.
         */
        static core::Result<std::unordered_map<std::string, core::ImpactReport>>
        analyze_all_files(
            const core::DependencyGraph& graph,
            const core::BuildTrace& trace
        );

        /**
         * Calculates a fragility score for a specific file.
         *
         * The score reflects how a change to the file propagates through the graph,
         * influencing rebuild complexity and cost.
         *
         * @param file The file path to evaluate.
         * @param graph The dependency graph.
         * @param trace The build trace with compile times.
         * @return The calculated fragility score (higher means more fragile).
         */
        static double calculate_fragility_score(
            const std::string& file,
            const core::DependencyGraph& graph,
            const core::BuildTrace& trace
        );

        /**
         * Calculates fragility scores for all files in the project.
         *
         * @param graph The dependency graph.
         * @param trace The build trace.
         * @return A Result mapping file paths to their fragility scores.
         */
        static core::Result<std::unordered_map<std::string, double>>
        calculate_all_fragility_scores(
            const core::DependencyGraph& graph,
            const core::BuildTrace& trace
        );

        /**
         * Simulates removal of a header file and lists affected files.
         *
         * Evaluates dependency impact by removing the given header node from the graph.
         *
         * @param header The header file to simulate removal for.
         * @param graph The dependency graph.
         * @return A Result containing the list of impacted files.
         */
        static core::Result<std::vector<std::string>> simulate_header_removal(
            const std::string& header,
            const core::DependencyGraph& graph
        );

        /**
         * Counts how many files would rebuild if a file changes.
         *
         * Traverses the dependency graph to compute cascading rebuilds.
         *
         * @param file The file being evaluated.
         * @param graph The dependency graph.
         * @return The number of dependent files requiring rebuild.
         */
        static int count_cascading_rebuilds(
            const std::string& file,
            const core::DependencyGraph& graph
        );

    private:
        /**
         * Retrieves compile time for a specific file from the build trace.
         *
         * @param file The file name to query.
         * @param trace The build trace containing timing data.
         * @return The compile time in milliseconds.
         */
        [[nodiscard]] static double get_compile_time(
            const std::string& file,
            const core::BuildTrace& trace
        ) ;

        /**
         * Checks whether a given file path refers to a header file.
         *
         * @param file The file path.
         * @return True if the file is a header, false otherwise.
         */
        [[nodiscard]] static bool is_header_file(const std::string& file) ;
    };

} // namespace bha::analysis

#endif //IMPACT_ANALYZER_H
