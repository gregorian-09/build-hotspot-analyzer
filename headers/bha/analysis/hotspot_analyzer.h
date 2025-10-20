//
// Created by gregorian on 20/10/2025.
//

#ifndef HOTSPOT_ANALYZER_H
#define HOTSPOT_ANALYZER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <unordered_map>

namespace bha::analysis {

    /**
     * @class HotspotAnalyzer
     * Identifies and ranks performance-critical files and headers in a build.
     *
     * The HotspotAnalyzer detects slow or high-impact source files and headers
     * based on compilation times, dependency relationships, and impact scores.
     * It helps pinpoint the most time-consuming or influential components of large builds.
     */
    class HotspotAnalyzer {
    public:
        /**
         * @struct Options
         * Configuration options for hotspot analysis.
         *
         * Defines parameters such as the number of top hotspots to report,
         * time thresholds, metrics to consider, and whether to include headers.
         */
        struct Options {
            int top_n = 20; ///< Number of top hotspots to report.
            double threshold_ms = 1000.0; ///< Minimum time threshold (in milliseconds) to consider a file a hotspot.
            std::vector<std::string> metrics = {"absolute_time", "impact_score"}; ///< Metrics used for ranking hotspots.
            bool include_headers = true; ///< Whether to include header files in the analysis.
        };

        /**
         * Default constructor.
         */
        HotspotAnalyzer() = default;

        /**
         * Identifies build hotspots using configurable options.
         *
         * @param trace The build trace data to analyze.
         * @param options The analysis configuration options.
         * @return A Result containing a vector of detected hotspots.
         */
        static core::Result<std::vector<core::Hotspot>> identify_hotspots(
            const core::BuildTrace& trace,
            const Options& options
        );

        /**
         * Finds the slowest files in the build trace.
         *
         * @param trace The build trace data.
         * @param top_n Number of top slow files to return.
         * @param threshold_ms Minimum compile time (in milliseconds) to include a file.
         * @return A Result containing a list of slow file hotspots.
         */
        static core::Result<std::vector<core::Hotspot>> find_slow_files(
            const core::BuildTrace& trace,
            int top_n,
            double threshold_ms
        );

        /**
         * Finds headers that significantly impact build performance.
         *
         * @param trace The build trace data.
         * @param graph The dependency graph to determine header inclusion impact.
         * @param top_n Number of top headers to return.
         * @return A Result containing header hotspots.
         */
        static core::Result<std::vector<core::Hotspot>> find_hot_headers(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            int top_n
        );

        /**
         * Identifies hotspots along the critical build dependency path.
         *
         * @param trace The build trace data.
         * @param graph The dependency graph representing file relationships.
         * @return A Result containing hotspots contributing to the critical path.
         */
        static core::Result<std::vector<core::Hotspot>> find_critical_path(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph
        );

        /**
         * Calculates the impact score of a specific file.
         *
         * @param file Path of the file to evaluate.
         * @param graph The dependency graph.
         * @param trace The build trace data.
         * @return The computed impact score (higher means greater build influence).
         */
        static double calculate_impact_score(
            const std::string& file,
            const core::DependencyGraph& graph,
            const core::BuildTrace& trace
        );

        /**
         * Calculates impact scores for all files in the trace.
         *
         * @param trace The build trace data.
         * @param graph The dependency graph.
         * @return A map of file paths to their respective impact scores.
         */
        static std::unordered_map<std::string, double> calculate_all_impact_scores(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph
        );

        /**
         * Ranks hotspots based on a selected metric.
         *
         * @param hotspots A list of identified hotspots.
         * @param metric The metric to rank by (e.g., "impact_score", "absolute_time").
         * @return A Result containing the sorted list of hotspots.
         */
        static core::Result<std::vector<core::Hotspot>> rank_by_metric(
            const std::vector<core::Hotspot>& hotspots,
            const std::string& metric
        );

    private:
        /**
         * Retrieves the compile time for a specific file.
         *
         * @param file Path of the file.
         * @param trace The build trace data.
         * @return The compile time in milliseconds.
         */
        [[nodiscard]] static double get_compile_time(
            const std::string& file,
            const core::BuildTrace& trace
        ) ;

        /**
         * Counts how many files depend on a given file.
         *
         * @param file Path of the file.
         * @param graph The dependency graph.
         * @return The number of dependents.
         */
        [[nodiscard]] static int count_dependents(
            const std::string& file,
            const core::DependencyGraph& graph
        ) ;

        /**
         * Calculates a depth-based weight for a file in the dependency graph.
         *
         * @param file Path of the file.
         * @param graph The dependency graph.
         * @return The computed depth weight factor.
         */
        [[nodiscard]] static double calculate_depth_weight(
            const std::string& file,
            const core::DependencyGraph& graph
        ) ;

        /**
         * Checks if a given file is a header file.
         *
         * @param file Path of the file.
         * @return True if the file is a header; otherwise, false.
         */
        [[nodiscard]] static bool is_header_file(const std::string& file) ;
    };

} // namespace bha::analysis

#endif //HOTSPOT_ANALYZER_H
