//
// Created by gregorian on 23/10/2025.
//

#ifndef REPORT_GENERATOR_H
#define REPORT_GENERATOR_H

#include "bha/export/exporter.h"
#include "bha/core/types.h"
#include "bha/core/result.h"
#include <string>
#include <vector>
#include "csv_exporter.h"
#include "html_exporter.h"
#include "json_exporter.h"
#include "markdown_exporter.h"
#include "text_exporter.h"

namespace bha::export_module {

    /**
     * Centralized class responsible for generating build analysis reports in multiple formats.
     *
     * The ReportGenerator serves as a high-level interface that coordinates report creation using the appropriate
     * exporter (e.g., JSON, HTML, CSV, Markdown, or Text) based on user-specified options. It supports both
     * single-format and multi-format output generation.
     */
    class ReportGenerator {
    public:
        /**
         * Configuration options controlling report generation and output behavior.
         */
        struct Options {
            ExportFormat format = ExportFormat::HTML;   ///< Default export format.
            std::string output_path;                    ///< Path where the generated report should be saved.
            bool auto_open = false;                     ///< Whether to automatically open the generated report in a browser or viewer.

            JSONExporter::Options json_options;         ///< Customization options for JSON export.
            HTMLExporter::Options html_options;         ///< Customization options for HTML export.
            CSVExporter::Options csv_options;           ///< Customization options for CSV export.
            MarkdownExporter::Options markdown_options; ///< Customization options for Markdown export.
            TextExporter::Options text_options;         ///< Customization options for plain-text export.
        };

        /**
         * Constructs a report generator with specified options.
         * @param options The configuration options determining output format and behavior.
         */
        explicit ReportGenerator(Options options);

        /**
         * Generates a single-format build report using configured exporter options.
         *
         * @param metrics Aggregated build performance metrics.
         * @param suggestions List of optimization suggestions derived from analysis.
         * @param trace Build trace data used for analysis.
         * @return A core::Result indicating success or containing an error message.
         *
         * The format is determined by the `options_.format` value. The appropriate exporter is automatically
         * instantiated based on that setting.
         */
        core::Result<void> generate(
            const core::MetricsSummary& metrics,
            const std::vector<core::Suggestion>& suggestions,
            const core::BuildTrace& trace
        ) const;

        /**
         * Generates multiple reports in various formats from the same analysis data.
         *
         * @param metrics Aggregated build performance metrics.
         * @param suggestions List of optimization suggestions derived from analysis.
         * @param trace Build trace data used for analysis.
         * @param formats List of formats to generate (e.g., {ExportFormat::HTML, ExportFormat::JSON}).
         * @param base_output_path Base file path used to construct per-format output filenames.
         * @return A core::Result indicating success or containing an error message.
         *
         * This allows producing multiple report formats (e.g., both `.html` and `.json`)
         * from a single analysis run for flexibility in integration and presentation.
         */
        static core::Result<void> generate_multi_format(
            const core::MetricsSummary& metrics,
            const std::vector<core::Suggestion>& suggestions,
            const core::BuildTrace& trace,
            const std::vector<ExportFormat>& formats,
            const std::string& base_output_path
        );

    private:
        Options options_; ///< Configuration settings determining output and exporters.

        /**
         * Constructs an output file path for a given export format.
         * @param base_path The base output path (without extension).
         * @param format The export format for which to construct the output path.
         * @return The complete output path including the appropriate extension.
         */
        static std::string get_output_path_for_format(
            const std::string& base_path,
            ExportFormat format
        );

        /**
         * Attempts to open the generated file in the user's default browser or viewer.
         * @param path The file path to open.
         * @return True if successfully opened, false otherwise.
         */
        static bool open_file_in_browser(const std::string& path);
    };

} // namespace bha::export_module


#endif //REPORT_GENERATOR_H
