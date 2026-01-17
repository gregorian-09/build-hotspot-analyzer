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
     * Result of file-level analysis.
     */
    struct FileAnalysisResult {
        fs::path file;
        Duration compile_time = Duration::zero();
        Duration frontend_time = Duration::zero();
        Duration backend_time = Duration::zero();
        TimeBreakdown breakdown;
        MemoryMetrics memory;

        double time_percent = 0.0;
        std::size_t rank = 0;

        std::size_t include_count = 0;
        std::size_t template_count = 0;
    };

    /**
     * Result of dependency analysis.
     */
    struct DependencyAnalysisResult {
        struct HeaderInfo {
            fs::path path;
            Duration total_parse_time = Duration::zero();
            std::size_t inclusion_count = 0;
            std::size_t including_files = 0;
            std::vector<fs::path> included_by;
            double impact_score = 0.0;
        };

        std::vector<HeaderInfo> headers;
        std::size_t total_includes = 0;
        std::size_t unique_headers = 0;
        std::size_t max_include_depth = 0;
        Duration total_include_time = Duration::zero();

        std::vector<std::pair<fs::path, fs::path>> circular_dependencies;
    };

    /**
     * Result of template analysis.
     */
    struct TemplateAnalysisResult {
        struct TemplateInfo {
            std::string name;
            std::string full_signature;
            Duration total_time = Duration::zero();
            std::size_t instantiation_count = 0;
            std::vector<SourceLocation> locations;
            std::vector<std::string> files_using;
            double time_percent = 0.0;
        };

        using TemplateStats = TemplateInfo;

        std::vector<TemplateInfo> templates;
        Duration total_template_time = Duration::zero();
        double template_time_percent = 0.0;
        std::size_t total_instantiations = 0;
    };

    /**
     * Result of symbol analysis.
     */
    struct SymbolAnalysisResult {
        struct SymbolInfo {
            std::string name;
            std::string type;
            fs::path defined_in;
            std::vector<fs::path> used_in;
            std::size_t usage_count = 0;
        };

        std::vector<SymbolInfo> symbols;
        std::size_t total_symbols = 0;
        std::size_t unused_symbols = 0;
    };

    /**
     * Overall performance analysis result.
     */
    struct PerformanceAnalysisResult {
        Duration total_build_time = Duration::zero();
        Duration sequential_time = Duration::zero();
        Duration parallel_time = Duration::zero();
        double parallelism_efficiency = 0.0;

        std::size_t total_files = 0;
        std::size_t slowest_file_count = 0;

        Duration avg_file_time = Duration::zero();
        Duration median_file_time = Duration::zero();
        Duration p90_file_time = Duration::zero();
        Duration p99_file_time = Duration::zero();

        MemoryMetrics total_memory;
        MemoryMetrics peak_memory;
        MemoryMetrics average_memory;

        std::vector<FileAnalysisResult> slowest_files;
        std::vector<fs::path> critical_path;
    };

    /**
     * Combined analysis result containing all analysis types.
     */
    struct AnalysisResult {
        PerformanceAnalysisResult performance;
        std::vector<FileAnalysisResult> files;
        DependencyAnalysisResult dependencies;
        TemplateAnalysisResult templates;
        SymbolAnalysisResult symbols;

        Timestamp analysis_time;
        Duration analysis_duration = Duration::zero();
    };

    /**
     * Base interface for all analyzers.
     */
    class IAnalyzer {
    public:
        virtual ~IAnalyzer() = default;

        /**
         * Returns the analyzer name.
         */
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;

        /**
         * Returns a description of what this analyzer does.
         */
        [[nodiscard]] virtual std::string_view description() const noexcept = 0;

        /**
         * Analyzes a build trace.
         *
         * @param trace The build trace to analyze.
         * @param options Analysis options.
         * @return Analysis result or an error.
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
        static AnalyzerRegistry& instance();

        void register_analyzer(std::unique_ptr<IAnalyzer> analyzer);

        [[nodiscard]] IAnalyzer* get_analyzer(std::string_view name) const;
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
     * @return Combined analysis result.
     */
    [[nodiscard]] Result<AnalysisResult, Error> run_full_analysis(
        const BuildTrace& trace,
        const AnalysisOptions& options = {}
    );

}  // namespace bha::analyzers

#endif //BHA_ANALYZER_HPP