//
// Created by gregorian on 23/10/2025.
//

#ifndef MARKDOWN_EXPORTER_H
#define MARKDOWN_EXPORTER_H

#include "bha/export/exporter.h"

namespace bha::export_module {

    /**
     * Exports build analysis reports in Markdown format.
     *
     * The MarkdownExporter generates human-readable `.md` reports that can be
     * viewed in text editors or rendered as rich documents on platforms such
     * as GitHub. The exporter supports optional tables of contents, code block
     * formatting, and configurable heading styles.
     */
    class MarkdownExporter final : public Exporter {
    public:
        /**
         * Configuration options controlling Markdown output.
         */
        struct Options {
            bool include_toc = true;              ///< Whether to include a table of contents.
            bool include_code_blocks = true;      ///< Whether to format sections as fenced code blocks.
            std::string heading_style = "atx";    ///< Heading style ("atx" for #, "setext" for underlined).
        };

        /**
         * Constructs a Markdown exporter with specified options.
         * @param options Options defining formatting behavior.
         */
        explicit MarkdownExporter(Options options);

        MarkdownExporter() : MarkdownExporter(Options{}) {};

        /**
         * Exports the given build report as a Markdown file.
         * @param metrics Aggregated build metrics summary.
         * @param suggestions List of optimization suggestions.
         * @param trace Build trace data for detailed insights.
         * @param output_path Destination file path for the Markdown report.
         * @return A Result indicating whether export succeeded or failed.
         */
        core::Result<void> export_report(
            const core::MetricsSummary& metrics,
            const std::vector<core::Suggestion>& suggestions,
            const core::BuildTrace& trace,
            const std::string& output_path
        ) override;

        /**
         * Returns the default file extension used by this exporter.
         * @return The string ".md".
         */
        [[nodiscard]] std::string get_default_extension() const override { return ".md"; }

        /**
         * Identifies this exporter’s output format.
         * @return ExportFormat::MARKDOWN.
         */
        [[nodiscard]] ExportFormat get_format() const override { return ExportFormat::MARKDOWN; }

    private:
        Options options_;  ///< User-defined configuration options.

        /**
         * Generates a Markdown table of contents if enabled.
         * @return The table of contents as a Markdown string.
         */
        static std::string generate_table_of_contents();

        /**
         * Generates a section summarizing build metrics.
         * @param metrics Metrics data structure to render.
         * @return Markdown-formatted text for the metrics section.
         */
        static std::string generate_metrics_section(const core::MetricsSummary& metrics);

        /**
         * Produces a Markdown table listing detected build hotspots.
         * @param metrics Metrics summary containing hotspot details.
         * @return A Markdown table representation of hotspots.
         */
        [[nodiscard]] std::string generate_hotspots_table(const core::MetricsSummary& metrics) const;

        /**
         * Generates a section containing improvement suggestions.
         * @param suggestions List of suggestions to include.
         * @return Markdown-formatted suggestion section.
         */
        static std::string generate_suggestions_section(const std::vector<core::Suggestion>& suggestions);

        /**
         * Escapes Markdown-sensitive characters in plain text.
         * @param text Input string.
         * @return Escaped string safe for Markdown rendering.
         */
        static std::string escape_markdown(const std::string& text);
    };

} // namespace bha::export_module

#endif //MARKDOWN_EXPORTER_H
