//
// Created by gregorian on 25/10/2025.
//

#ifndef ANALYSIS_ENGINE_H
#define ANALYSIS_ENGINE_H

#include "bha/analysis/dependency_analyzer.h"
#include "bha/analysis/hotspot_analyzer.h"
#include "bha/analysis/pch_analyzer.h"
#include "bha/analysis/template_analyzer.h"
#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>

namespace bha::analysis {

    /**
     * Aggregated report containing results from all build analyses.
     *
     * The report consolidates findings from dependency analysis, hotspot detection,
     * PCH evaluation, and template performance investigations. It represents a
     * single execution of a full build analysis pass.
     */
    struct AnalysisReport {
        /// List of detected dependency-related issues (cycles, redundancy, etc.)
        std::vector<DependencyIssue> dependency_issues;

        /// Cycles detected within the dependency graph.
        std::vector<std::vector<std::string>> dependency_cycles;

        /// Include depth per file, measuring the transitive inclusion chain length.
        std::unordered_map<std::string, int> include_depths;

        /// General list of build hotspots across all files.
        std::vector<core::Hotspot> hotspots;

        /// Files with the highest compile time.
        std::vector<core::Hotspot> slow_files;

        /// Header files contributing significantly to compile cost.
        std::vector<core::Hotspot> hot_headers;

        /// Files representing the critical build path.
        std::vector<core::Hotspot> critical_path;

        /// Mapping of each file to its change impact report.
        std::unordered_map<std::string, core::ImpactReport> impact_by_file;

        /// Headers with high fragility scores.
        std::vector<std::string> fragile_headers;

        /// Suggested precompiled header (PCH) candidates.
        std::vector<PCHCandidate> pch_candidates;

        /// Quantitative assessment of an existing PCH’s effectiveness.
        core::PCHMetrics pch_metrics;

        /// Results of analyzing template instantiations and compile costs.
        TemplateAnalysisResult template_analysis;

        /// Total measured build duration in milliseconds.
        double total_build_time_ms = 0.0;

        /// Total number of files processed in the analysis.
        int total_files_analyzed = 0;
    };

    /**
     * Main engine for orchestrating build analysis tasks.
     *
     * The BuildAnalysisEngine coordinates different analysis subsystems:
     * - Dependency analysis (cycles, redundancy, fanout)
     * - Hotspot analysis (slow files, headers, critical path)
     * - Impact analysis (change propagation)
     * - PCH analysis (candidates and effectiveness)
     * - Template analysis (compile-time cost)
     *
     * Each of these can be toggled independently via the @ref Options struct.
     */
    class BuildAnalysisEngine {
    public:
        /**
         * Configuration controlling which analyses are performed.
         */
        struct Options {
            bool enable_dependency_analysis = true; ///< Enable dependency-related checks.
            bool enable_hotspot_analysis = true;    ///< Enable hotspot and performance profiling.
            bool enable_impact_analysis = true;     ///< Enable change impact propagation analysis.
            bool enable_pch_analysis = true;        ///< Enable PCH candidate and effectiveness evaluation.
            bool enable_template_analysis = true;   ///< Enable template instantiation cost analysis.

            HotspotAnalyzer::Options hotspot_options; ///< Parameters for hotspot detection.

            int pch_candidates_count = 10;           ///< Maximum number of PCH suggestions.
            double pch_min_inclusion_ratio = 0.5;    ///< Minimum inclusion ratio for PCH candidacy.
            int template_top_n = 20;                 ///< Maximum number of template hotspots.
            int fragile_header_threshold = 10;       ///< Inclusion count threshold for fragile headers.
        };

        /// Default constructor.
        BuildAnalysisEngine() = default;

        /**
         * Execute a comprehensive analysis using all enabled modules.
         *
         * @param trace The build trace data containing compile durations and artifacts.
         * @param graph The dependency graph describing file relationships.
         * @param options The configuration options controlling analysis scope.
         * @return A @ref core::Result containing an @ref AnalysisReport or an error.
         */
        static core::Result<AnalysisReport> analyze(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            const Options& options
        );

    private:
        /**
         * Perform dependency graph analysis.
         *
         * Detects circular dependencies, redundant includes, and computes include depths.
         *
         * @param graph The dependency graph.
         * @param report Output report to populate.
         * @return A void result on success, or an error.
         */
        static core::Result<void> run_dependency_analysis(
            const core::DependencyGraph& graph,
            AnalysisReport& report
        );

        /**
         * Perform hotspot analysis on build trace and dependency data.
         *
         * Identifies slow files, hot headers, and critical build paths.
         *
         * @param trace The build trace data.
         * @param graph The dependency graph.
         * @param options Analysis configuration.
         * @param report Output report to populate.
         * @return A void result on success, or an error.
         */
        static core::Result<void> run_hotspot_analysis(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            const Options& options,
            AnalysisReport& report
        );

        /**
         * Analyze file-level change impacts across the project.
         *
         * Evaluates how modifications in source files affect rebuild scope and time.
         *
         * @param graph The dependency graph.
         * @param trace The build trace data.
         * @param options Analysis configuration.
         * @param report Output report to populate.
         * @return A void result on success, or an error.
         */
        static core::Result<void> run_impact_analysis(
            const core::DependencyGraph& graph,
            const core::BuildTrace& trace,
            const Options& options,
            AnalysisReport& report
        );

        /**
         * Perform PCH analysis.
         *
         * Suggests precompiled header candidates and evaluates potential time savings.
         *
         * @param trace The build trace data.
         * @param graph The dependency graph.
         * @param options Analysis configuration.
         * @param report Output report to populate.
         * @return A void result on success, or an error.
         */
        static core::Result<void> run_pch_analysis(
            const core::BuildTrace& trace,
            const core::DependencyGraph& graph,
            const Options& options,
            AnalysisReport& report
        );

        /**
         * Perform template instantiation cost analysis.
         *
         * Identifies templates responsible for significant compile-time costs.
         *
         * @param trace The build trace data.
         * @param options Analysis configuration.
         * @param report Output report to populate.
         * @return A void result on success, or an error.
         */
        static core::Result<void> run_template_analysis(
            const core::BuildTrace& trace,
            const Options& options,
            AnalysisReport& report
        );
    };

} // namespace bha::analysis

#endif //ANALYSIS_ENGINE_H
