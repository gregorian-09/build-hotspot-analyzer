//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_ANALYZER_HPP
#define BHA_ANALYZER_HPP

/**
 * @file analyzer.hpp
 * @brief Analysis interface and result types.
 *
 * Defines the interface for build trace analyzers. Analyzers process
 * parsed build traces to extract insights about compilation performance,
 * dependencies, and optimization opportunities.
 *
 * Analyzer types:
 * - FileAnalyzer: Per-file compilation metrics
 * - DependencyAnalyzer: Include graph and dependency analysis
 * - TemplateAnalyzer: Template instantiation hotspots
 * - SymbolAnalyzer: Symbol definition and usage patterns
 * - PerformanceAnalyzer: Overall build performance metrics
 */

#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/types.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>

namespace bha::analyzers {

    /**
     * @brief File-level compilation metrics for one translation unit.
     */
    struct FileAnalysisResult {
        /// Path to the translation unit source file.
        fs::path file;
        /// End-to-end compile duration for the translation unit.
        Duration compile_time = Duration::zero();
        /// Frontend phase duration (parsing, semantic analysis).
        Duration frontend_time = Duration::zero();
        /// Backend phase duration (optimization, code generation).
        Duration backend_time = Duration::zero();
        /// Fine-grained compiler-reported time categories.
        TimeBreakdown breakdown{};
        /// Memory telemetry captured for the compile.
        MemoryMetrics memory{};

        /// Percentage contribution to total build time.
        double time_percent = 0.0;
        /// Rank among files sorted by descending compile time.
        std::size_t rank = 0;

        /// Number of includes encountered while compiling this file.
        std::size_t include_count = 0;
        /// Number of template instantiations attributed to this file.
        std::size_t template_count = 0;
    };

    /**
     * @brief Include/dependency graph analysis results.
     */
    struct DependencyAnalysisResult {
        /**
         * @brief Aggregate include metrics for one header file.
         */
        struct HeaderInfo {
            /// Header path.
            fs::path path;
            /// Total parse time accumulated across all inclusions.
            Duration total_parse_time = Duration::zero();
            /// Number of include events observed for this header.
            std::size_t inclusion_count = 0;
            /// Number of unique source files that include this header.
            std::size_t including_files = 0;
            /// Reverse include edges (`who includes this header`).
            std::vector<fs::path> included_by;
            /// Composite impact score used for optimization prioritization.
            double impact_score = 0.0;

            /// Source-control modification count where available.
            std::size_t modification_count = 0;
            /// Last modification timestamp where available.
            Timestamp last_modified;
            /// Elapsed duration since last modification.
            Duration time_since_modification = Duration::zero();
            /// Heuristic stability indicator (rarely modified).
            bool is_stable = false;
            /// True when the header is outside project-owned source roots.
            bool is_external = false;
        };

        /// All analyzed headers.
        std::vector<HeaderInfo> headers;
        /// Total include directives/events seen in traces.
        std::size_t total_includes = 0;
        /// Count of unique header files.
        std::size_t unique_headers = 0;
        /// Maximum include nesting depth observed.
        std::size_t max_include_depth = 0;
        /// Cumulative time spent parsing included headers.
        Duration total_include_time = Duration::zero();

        /// Detected circular include relations.
        std::vector<std::pair<fs::path, fs::path>> circular_dependencies;
    };

    /**
     * @brief Template instantiation cost profile.
     */
    struct TemplateAnalysisResult {
        /**
         * @brief Aggregated timing and usage metadata for one template signature.
         */
        struct TemplateInfo {
            /// Friendly template identifier.
            std::string name;
            /// Fully qualified signature used for grouping.
            std::string full_signature;
            /// Sum of time attributed to this template.
            Duration total_time = Duration::zero();
            /// Number of instantiation events observed.
            std::size_t instantiation_count = 0;
            /// Source locations where instantiation occurred.
            std::vector<SourceLocation> locations;
            /// Files that instantiate or use this template.
            std::vector<std::string> files_using;
            /// Percentage contribution within template-related time.
            double time_percent = 0.0;
        };

        using TemplateStats = TemplateInfo;

        /// All aggregated template entries.
        std::vector<TemplateInfo> templates;
        /// Total time spent in template instantiation.
        Duration total_template_time = Duration::zero();
        /// Template time as a percentage of total build time.
        double template_time_percent = 0.0;
        /// Total count of instantiation events.
        std::size_t total_instantiations = 0;
    };

    /**
     * @brief Symbol definition and usage analysis.
     */
    struct SymbolAnalysisResult {
        /**
         * @brief One symbol with definition and usage locations.
         */
        struct SymbolInfo {
            /// Symbol name (possibly qualified).
            std::string name;
            /// Symbol category/classification (function, class, variable, ...).
            std::string type;
            /// Definition site.
            fs::path defined_in;
            /// Files referencing this symbol.
            std::vector<fs::path> used_in;
            /// Number of references across all files.
            std::size_t usage_count = 0;
        };

        /// All tracked symbols.
        std::vector<SymbolInfo> symbols;
        /// Total symbol count.
        std::size_t total_symbols = 0;
        /// Count of symbols with zero observed usages.
        std::size_t unused_symbols = 0;
    };

    /**
     * @brief Build-level performance summary statistics.
     */
    struct PerformanceAnalysisResult {
        /// Wall-clock total build duration from trace.
        Duration total_build_time = Duration::zero();
        /// Sum of compile durations as if executed serially.
        Duration sequential_time = Duration::zero();
        /// Effective overlap duration from parallel execution.
        Duration parallel_time = Duration::zero();
        /// Parallelism efficiency in [0, 1] under current heuristic.
        double parallelism_efficiency = 0.0;

        /// Number of compiled translation units.
        std::size_t total_files = 0;
        /// Number of entries included in the `slowest_files` list.
        std::size_t slowest_file_count = 0;

        /// Mean compile duration.
        Duration avg_file_time = Duration::zero();
        /// Median compile duration.
        Duration median_file_time = Duration::zero();
        /// P90 compile duration.
        Duration p90_file_time = Duration::zero();
        /// P99 compile duration.
        Duration p99_file_time = Duration::zero();

        /// Aggregated memory metrics across the build.
        MemoryMetrics total_memory{};
        /// Peak memory footprint seen in the build.
        MemoryMetrics peak_memory{};
        /// Average memory footprint seen in the build.
        MemoryMetrics average_memory{};

        /// Slowest translation units, sorted descending by compile time.
        std::vector<FileAnalysisResult> slowest_files;
        /// Approximate critical path through the build graph.
        std::vector<fs::path> critical_path;
    };

    /**
     * @brief Cacheability/distributed-build suitability summary.
     */
    struct CacheDistributionAnalysisResult {
        /// Total compilation commands analyzed.
        std::size_t total_compilations = 0;
        /// Commands considered cache-friendly.
        std::size_t cache_friendly_compilations = 0;
        /// Commands flagged as cache-risky.
        std::size_t cache_risk_compilations = 0;
        /// Estimated cache hit opportunity percentage.
        double cache_hit_opportunity_percent = 0.0;

        /// Whether sccache wrapper usage was detected.
        bool sccache_detected = false;
        /// Whether FASTBuild markers were detected.
        bool fastbuild_detected = false;
        /// Whether any cache wrapper pattern was detected.
        bool cache_wrapper_detected = false;

        /// Number of commands using dynamic-macro patterns harming cache hits.
        std::size_t dynamic_macro_risk_count = 0;
        /// Number of commands using profile/coverage flags reducing cacheability.
        std::size_t profile_or_coverage_risk_count = 0;
        /// Number of commands tied to PCH generation constraints.
        std::size_t pch_generation_risk_count = 0;
        /// Number of commands with volatile absolute-path characteristics.
        std::size_t volatile_path_risk_count = 0;

        /// Composite score for distributed build suitability.
        double distributed_suitability_score = 0.0;
        /// Number of heavy TUs likely to dominate distributed scheduling.
        std::size_t heavy_translation_units = 0;
        /// Number of commands with homogeneous argument shape.
        std::size_t homogeneous_command_units = 0;
    };

    /**
     * @brief Unified analysis output combining all analyzer domains.
     */
    struct AnalysisResult {
        /// Global build performance summary.
        PerformanceAnalysisResult performance;
        /// Per-file metrics.
        std::vector<FileAnalysisResult> files;
        /// Include/dependency metrics.
        DependencyAnalysisResult dependencies;
        /// Template-instantiation metrics.
        TemplateAnalysisResult templates;
        /// Symbol graph metrics.
        SymbolAnalysisResult symbols;
        /// Cache/distribution suitability metrics.
        CacheDistributionAnalysisResult cache_distribution;

        /// Timestamp when analysis completed.
        Timestamp analysis_time;
        /// Total analysis wall-clock duration.
        Duration analysis_duration = Duration::zero();
    };

    /**
     * Base interface for all analyzers.
     */
    class IAnalyzer {
    public:
        virtual ~IAnalyzer() = default;

        /**
         * @brief Return the analyzer identifier.
         *
         * Identifier values are used in registry lookup, telemetry, and
         * diagnostics. Implementations should return stable ASCII tokens.
         */
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;

        /**
         * @brief Return a human-readable analyzer description.
         */
        [[nodiscard]] virtual std::string_view description() const noexcept = 0;

        /**
         * @brief Run this analyzer over a parsed build trace.
         *
         * Implementations may populate only the sections they own in
         * `AnalysisResult`; orchestration merges outputs into the final report.
         *
         * @param trace Build trace to analyze.
         * @param options Analyzer-specific option bundle.
         * @return Analysis payload on success, otherwise structured error.
         */
        [[nodiscard]] virtual Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const = 0;
    };

    /**
     * Registry for managing analyzers.
     */
    class AnalyzerRegistry {
    public:
        /**
         * @brief Access process-wide analyzer registry singleton.
         */
        static AnalyzerRegistry& instance();

        /**
         * @brief Register one analyzer implementation.
         *
         * Ownership is transferred to the registry.
         *
         * @param analyzer Analyzer instance to store.
         */
        void register_analyzer(std::unique_ptr<IAnalyzer> analyzer);

        /**
         * @brief Find an analyzer by identifier.
         *
         * @param name Analyzer identifier (`IAnalyzer::name()`).
         * @return Raw pointer owned by the registry, or `nullptr`.
         */
        [[nodiscard]] IAnalyzer* get_analyzer(std::string_view name) const;
        /**
         * @brief List all registered analyzers.
         *
         * @return Stable snapshot of registry entries as raw pointers.
         */
        [[nodiscard]] std::vector<IAnalyzer*> list_analyzers() const;

    private:
        AnalyzerRegistry() = default;
        std::vector<std::unique_ptr<IAnalyzer>> analyzers_;
    };

    /**
     * Runs all registered analyzers on a build trace.
     *
     * @param trace The build trace to analyze.
     * @param options Analysis options.
     * @return Combined analysis result or the first fatal error encountered.
     */
    [[nodiscard]] Result<AnalysisResult, Error> run_full_analysis(
        const BuildTrace& trace,
        const AnalysisOptions& options = {}
    );

}  // namespace bha::analyzers

#endif //BHA_ANALYZER_HPP
