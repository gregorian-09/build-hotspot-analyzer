//
// Created by gregorian-rayne on 09/01/2026.
//

#ifndef CONFIG_HPP
#define CONFIG_HPP

/**
 * @file config.hpp
 * @brief Heuristics configuration for build optimization analysis.
 *
 * Thresholds and parameters are based on industry best practices:
 * - ClangBuildAnalyzer: https://github.com/aras-p/ClangBuildAnalyzer
 * - Microsoft C++ Build Insights: https://github.com/microsoft/cpp-build-insights-samples
 * - Chromium Jumbo Builds: https://chromium.googlesource.com/chromium/src.git/+/65.0.3283.0/docs/jumbo.md
 */

#include <chrono>
#include <cstddef>

namespace bha::heuristics
{
    /**
     * @brief Analysis thresholds based on ClangBuildAnalyzer defaults.
     *
     * Reference: ClangBuildAnalyzer Config struct
     * - fileParseCount/fileCodegenCount: 10
     * - templateCount/functionCount: 30
     * - headerCount: 10, headerChainCount: 5
     * - minFileTime: 10ms
     * - maxName: 70 characters
     */
    struct AnalysisConfig {
        /// Maximum files to report for parsing/codegen (ClangBuildAnalyzer: 10)
        std::size_t max_files_to_report = 10;

        /// Maximum templates/functions to report (ClangBuildAnalyzer: 30)
        std::size_t max_templates_to_report = 30;

        /// Maximum headers to report (ClangBuildAnalyzer: 10)
        std::size_t max_headers_to_report = 10;

        /// Maximum header chain depth to display (ClangBuildAnalyzer: 5)
        std::size_t max_header_chain_depth = 5;

        /// Minimum file time to include in analysis (ClangBuildAnalyzer: 10ms)
        std::chrono::milliseconds min_file_time{10};

        /// Maximum name length before truncation (ClangBuildAnalyzer: 70)
        std::size_t max_name_length = 70;
    };

    /**
     * @brief PCH suggestion thresholds.
     *
     * References:
     * - Microsoft TopHeaders sample: identifies headers for precompilation
     * - Build Insights: headers parsed repeatedly across translation units
     */
    struct PCHConfig {
        /// Minimum inclusion count to consider for PCH
        /// Headers included in fewer files don't benefit much from PCH
        std::size_t min_include_count = 10;

        /// Minimum aggregate parse time to justify PCH overhead (ms)
        /// Based on typical PCH loading overhead vs parse time savings
        std::chrono::milliseconds min_aggregate_time{500};

        /// Priority thresholds based on inclusion count and time ratio
        struct PriorityThresholds {
            std::size_t critical_includes = 50;  // >= 50 includes + >5% build time
            std::size_t high_includes = 20;      // >= 20 includes + >2% build time
            double critical_time_ratio = 0.05;   // 5% of total build time
            double high_time_ratio = 0.02;       // 2% of total build time
        } priority;
    };

    /**
     * @brief Template optimization thresholds.
     *
     * References:
     * - ClangBuildAnalyzer: template instantiation cost aggregation
     * - Microsoft RecursiveTemplateInspector: identifies costly recursive instantiations
     */
    struct TemplateConfig {
        /// Minimum instantiation count to report
        std::size_t min_instantiation_count = 5;

        /// Minimum total time to consider optimization (ms)
        std::chrono::milliseconds min_total_time{100};

        /// Time percentage threshold for high priority (10% of template time)
        double high_priority_percent = 10.0;

        /// Recursive depth threshold for warning
        std::size_t recursive_depth_warning = 10;
    };

    /**
     * @brief Function code generation thresholds.
     *
     * Reference: Microsoft LongCodeGenFinder
     * - Lists functions taking more than 500ms to generate
     */
    struct CodeGenConfig {
        /// Threshold for long code generation (Microsoft Build Insights: 500ms)
        std::chrono::milliseconds long_codegen_threshold{500};

        /// Threshold for warning about inlining issues
        std::chrono::milliseconds inline_warning_threshold{100};
    };

    /**
     * @brief Header analysis thresholds.
     */
    struct HeaderConfig {
        /// Minimum parse time to consider for optimization (ms)
        std::chrono::milliseconds min_parse_time{100};

        /// Minimum includers to suggest header splitting
        std::size_t min_includers_for_split = 5;

        /// Time thresholds for priority classification
        struct TimeThresholds {
            std::chrono::milliseconds critical{2000};  // >2s = critical
            std::chrono::milliseconds high{1000};      // >1s = high
            std::chrono::milliseconds medium{500};     // >500ms = medium
            std::chrono::milliseconds low{100};        // >100ms = low
        } time;
    };

    /**
     * @brief Unity build configuration.
     *
     * Reference: Chromium Jumbo Builds
     * - Uses 50 files per jumbo/unity unit
     * - Header parsing typically 40-50% of compile time
     */
    struct UnityBuildConfig {
        /// Files to group per unity file (Chromium: 50)
        std::size_t files_per_unit = 50;

        /// Minimum files to consider unity build worthwhile
        std::size_t min_files_threshold = 10;

        /// Estimated ratio of compile time spent on header parsing (40-50%)
        double header_parsing_ratio = 0.45;
    };

    /**
     * @brief Forward declaration suggestion thresholds.
     */
    struct ForwardDeclConfig {
        /// Minimum header parse time to suggest forward declaration (ms)
        std::chrono::milliseconds min_parse_time{50};

        /// Minimum usage sites to make suggestion worthwhile
        std::size_t min_usage_sites = 3;
    };

    /**
     * @brief Global heuristics configuration.
     *
     * All values are based on ClangBuildAnalyzer and Microsoft Build Insights
     * research and best practices.
     */
    struct HeuristicsConfig {
        AnalysisConfig analysis;
        PCHConfig pch;
        TemplateConfig templates;
        CodeGenConfig codegen;
        HeaderConfig headers;
        UnityBuildConfig unity_build;
        ForwardDeclConfig forward_decl;

        /// Get default configuration with research-backed values.
        static HeuristicsConfig defaults() {
            return HeuristicsConfig{};
        }
    };
}      // namespace bha::heuristics

#endif //CONFIG_HPP
