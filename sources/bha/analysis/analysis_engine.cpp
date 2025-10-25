//
// Created by gregorian on 25/10/2025.
//

#include "bha/analysis/analysis_engine.h"
#include "bha/analysis/impact_analyzer.h"

namespace bha::analysis {

    core::Result<AnalysisReport> BuildAnalysisEngine::analyze(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const Options& options
    ) {
        AnalysisReport report;
        report.total_build_time_ms = trace.total_build_time_ms;
        report.total_files_analyzed = static_cast<int>(trace.compilation_units.size());

        if (options.enable_dependency_analysis) {
            if (auto result = run_dependency_analysis(graph, report); !result) {
                return core::Result<AnalysisReport>::failure(result.error());
            }
        }

        if (options.enable_hotspot_analysis) {
            if (auto result = run_hotspot_analysis(trace, graph, options, report); !result) {
                return core::Result<AnalysisReport>::failure(result.error());
            }
        }

        if (options.enable_impact_analysis) {
            if (auto result = run_impact_analysis(graph, trace, options, report); !result) {
                return core::Result<AnalysisReport>::failure(result.error());
            }
        }

        if (options.enable_pch_analysis) {
            if (auto result = run_pch_analysis(trace, graph, options, report); !result) {
                return core::Result<AnalysisReport>::failure(result.error());
            }
        }

        if (options.enable_template_analysis) {
            if (auto result = run_template_analysis(trace, options, report); !result) {
                return core::Result<AnalysisReport>::failure(result.error());
            }
        }

        return core::Result<AnalysisReport>::success(std::move(report));
    }

    core::Result<void> BuildAnalysisEngine::run_dependency_analysis(
        const core::DependencyGraph& graph,
        AnalysisReport& report
    ) {
        auto cycles = DependencyAnalyzer::detect_cycles(graph);
        if (!cycles) {
            return core::Result<void>::failure(cycles.error());
        }
        report.dependency_cycles = cycles.value();

        auto issues = DependencyAnalyzer::analyze_all_issues(graph);
        if (!issues) {
            return core::Result<void>::failure(issues.error());
        }
        report.dependency_issues = issues.value();

        auto depths = DependencyAnalyzer::calculate_include_depths(graph);
        if (!depths) {
            return core::Result<void>::failure(depths.error());
        }
        report.include_depths = depths.value();

        return core::Result<void>::success();
    }

    core::Result<void> BuildAnalysisEngine::run_hotspot_analysis(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const Options& options,
        AnalysisReport& report
    ) {
        auto hotspots = HotspotAnalyzer::identify_hotspots(trace, options.hotspot_options);
        if (!hotspots) {
            return core::Result<void>::failure(hotspots.error());
        }
        report.hotspots = hotspots.value();

        auto slow_files = HotspotAnalyzer::find_slow_files(
            trace,
            options.hotspot_options.top_n,
            options.hotspot_options.threshold_ms
        );
        if (!slow_files) {
            return core::Result<void>::failure(slow_files.error());
        }
        report.slow_files = slow_files.value();

        auto hot_headers = HotspotAnalyzer::find_hot_headers(
            trace,
            graph,
            options.hotspot_options.top_n
        );
        if (!hot_headers) {
            return core::Result<void>::failure(hot_headers.error());
        }
        report.hot_headers = hot_headers.value();

        auto critical = HotspotAnalyzer::find_critical_path(trace, graph);
        if (!critical) {
            return core::Result<void>::failure(critical.error());
        }
        report.critical_path = critical.value();

        return core::Result<void>::success();
    }

    core::Result<void> BuildAnalysisEngine::run_impact_analysis(
        const core::DependencyGraph& graph,
        const core::BuildTrace& trace,
        const Options& options,
        AnalysisReport& report
    ) {
        for (const auto& node : graph.get_all_nodes()) {
            if (auto impact = ImpactAnalyzer::analyze_change_impact(node, graph, trace)) {
                report.impact_by_file[node] = impact.value();
            }
        }

        auto fragile = ImpactAnalyzer::find_fragile_headers(graph, options.fragile_header_threshold);
        if (!fragile) {
            return core::Result<void>::failure(fragile.error());
        }
        report.fragile_headers = fragile.value();

        return core::Result<void>::success();
    }

    core::Result<void> BuildAnalysisEngine::run_pch_analysis(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const Options& options,
        AnalysisReport& report
    ) {
        auto candidates = PCHAnalyzer::identify_pch_candidates(
            trace,
            graph,
            options.pch_candidates_count,
            options.pch_min_inclusion_ratio
        );
        if (!candidates) {
            return core::Result<void>::failure(candidates.error());
        }
        report.pch_candidates = candidates.value();

        return core::Result<void>::success();
    }

    core::Result<void> BuildAnalysisEngine::run_template_analysis(
        const core::BuildTrace& trace,
        const Options& options,
        AnalysisReport& report
    ) {
        auto analysis = TemplateAnalyzer::analyze_templates(trace, options.template_top_n);
        if (!analysis) {
            return core::Result<void>::failure(analysis.error());
        }
        report.template_analysis = analysis.value();

        return core::Result<void>::success();
    }

} // namespace bha::analysis