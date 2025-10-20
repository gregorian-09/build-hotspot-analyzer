//
// Created by gregorian on 20/10/2025.
//

#ifndef TEMPLATE_ANALYZER_H
#define TEMPLATE_ANALYZER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace bha::analysis {

    /**
     * @struct TemplateAnalysisResult
     * Represents the outcome of a template performance analysis.
     *
     * This structure aggregates metrics about template instantiations,
     * including counts, timing information, and overall impact on build performance.
     */
    struct TemplateAnalysisResult {
        /** List of the most expensive template instantiations. */
        std::vector<core::TemplateHotspot> expensive_templates;

        /** Mapping of template names to their instantiation counts. */
        std::unordered_map<std::string, int> instantiation_counts;

        /** Mapping of template names to their cumulative instantiation times (in milliseconds). */
        std::unordered_map<std::string, double> total_times_by_template;

        /** Total compilation time spent on template instantiations (in milliseconds). */
        double total_template_time_ms;

        /** Percentage of total build time attributed to template instantiations. */
        double template_time_percentage;
    };

    /**
     * @class TemplateAnalyzer
     * Performs analysis of C++ template usage and compilation overhead.
     *
     * The TemplateAnalyzer inspects build traces to identify expensive templates,
     * high instantiation counts, and files that contribute heavily to template-related
     * compile times. It can also suggest optimizations such as explicit instantiations.
     */
    class TemplateAnalyzer {
    public:
        /**
         * Default constructor.
         */
        TemplateAnalyzer() = default;

        /**
         * Performs a comprehensive analysis of template usage across the build.
         *
         * Identifies costly templates, computes instantiation counts,
         * and aggregates time spent in template compilation.
         *
         * @param trace The build trace containing compilation unit data.
         * @param top_n The number of top templates to include in the analysis.
         * @return A Result containing a @ref TemplateAnalysisResult with analysis data.
         */
        static core::Result<TemplateAnalysisResult> analyze_templates(
            const core::BuildTrace& trace,
            int top_n = 20
        );

        /**
         * Identifies the most time-consuming template instantiations.
         *
         * Filters templates based on compilation time and returns the most expensive ones.
         *
         * @param trace The build trace data.
         * @param top_n Number of top templates to return.
         * @param threshold_ms Minimum time threshold (in milliseconds) to consider a template expensive.
         * @return A Result containing a list of @ref core::TemplateHotspot entries.
         */
        static core::Result<std::vector<core::TemplateHotspot>> find_expensive_templates(
            const core::BuildTrace& trace,
            int top_n = 20,
            double threshold_ms = 100.0
        );

        /**
         * Counts how many times each template was instantiated across all units.
         *
         * @param trace The build trace containing template instantiation data.
         * @return A Result mapping template names to their instantiation counts.
         */
        static core::Result<std::unordered_map<std::string, int>> count_instantiations(
            const core::BuildTrace& trace
        );

        /**
         * Calculates total compilation time spent per template.
         *
         * Aggregates time across all instances of each template.
         *
         * @param trace The build trace containing template timing data.
         * @return A Result mapping template names to their cumulative compile times (in milliseconds).
         */
        static core::Result<std::unordered_map<std::string, double>> calculate_template_times(
            const core::BuildTrace& trace
        );

        /**
         * Suggests templates that could benefit from explicit instantiation.
         *
         * Templates with a high number of instantiations may be optimized by
         * explicitly instantiating them in a single translation unit.
         *
         * @param trace The build trace.
         * @param min_instantiation_count Minimum number of instantiations to suggest explicit instantiation.
         * @return A Result containing a list of suggested template names.
         */
        static core::Result<std::vector<std::string>> suggest_explicit_instantiations(
            const core::BuildTrace& trace,
            int min_instantiation_count = 3
        );

        /**
         * Identifies source files dominated by template compilation time.
         *
         * Files exceeding the given percentage threshold of total compile time
         * spent in templates are considered template-heavy.
         *
         * @param trace The build trace.
         * @param threshold_percent Minimum percentage of compile time in templates.
         * @return A Result containing a list of file paths considered template-heavy.
         */
        static core::Result<std::vector<std::string>> find_template_heavy_files(
            const core::BuildTrace& trace,
            double threshold_percent = 50.0
        );

        /**
         * Calculates the proportion of compile time spent on templates within a compilation unit.
         *
         * @param unit The compilation unit to evaluate.
         * @return The percentage of compile time attributed to templates (0–100).
         */
        static double calculate_template_overhead(
            const core::CompilationUnit& unit
        );

    private:
        /**
         * Normalizes template names for consistent comparison.
         *
         * Removes noise such as parameter formatting or compiler-specific suffixes.
         *
         * @param name The original template name.
         * @return The normalized template name.
         */
        [[nodiscard]] static std::string normalize_template_name(const std::string& name) ;

        /**
         * Checks if a template belongs to the C++ standard library.
         *
         * Used to exclude standard templates (e.g., `std::vector`, `std::map`) from custom analysis.
         *
         * @param template_name The name of the template.
         * @return True if the template is from the standard library, false otherwise.
         */
        [[nodiscard]] static bool is_std_template(const std::string& template_name) ;
    };

} // namespace bha::analysis


#endif //TEMPLATE_ANALYZER_H
