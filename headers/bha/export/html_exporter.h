//
// Created by gregorian on 23/10/2025.
//

#ifndef HTML_EXPORTER_H
#define HTML_EXPORTER_H

#include "bha/export/exporter.h"

namespace bha::export_module {

    /**
     * Exports build analysis reports as interactive HTML documents.
     *
     * The HTMLExporter generates visually rich HTML reports summarizing build metrics,
     * hotspots, and performance suggestions. Supports embedded CSS/JavaScript for
     * enhanced readability and interactivity.
     */
    class HTMLExporter final : public Exporter {
    public:
        /**
         * Configuration options for the HTML exporter.
         */
        struct Options {
            bool include_visualizations = true;   ///< Include charts and graphs in output.
            bool embed_css = true;                ///< Embed CSS styles directly in the HTML file.
            bool embed_javascript = true;         ///< Embed JavaScript for interactivity.
            std::string title = "Build Hotspot Analysis Report"; ///< HTML report title.
            std::string theme = "default";        ///< Visual theme for the report.
        };

        /**
         * Constructs an HTML exporter with the given options.
         * @param options Export customization options.
         */
        explicit HTMLExporter(Options options);

        HTMLExporter() : HTMLExporter(Options{}) {};

        /**
         * Exports the provided data as an HTML report.
         * @param metrics Summary of build performance metrics.
         * @param suggestions List of improvement or optimization suggestions.
         * @param trace Detailed build trace data.
         * @param output_path Destination file path for the HTML report.
         * @return A Result indicating success or failure of the export.
         */
        core::Result<void> export_report(
            const core::MetricsSummary& metrics,
            const std::vector<core::Suggestion>& suggestions,
            const core::BuildTrace& trace,
            const std::string& output_path
        ) override;

        /**
         * Returns the default file extension for HTML exports.
         * @return The string ".html".
         */
        [[nodiscard]] std::string get_default_extension() const override { return ".html"; }

        /**
         * Returns the export format type.
         * @return ExportFormat::HTML.
         */
        [[nodiscard]] ExportFormat get_format() const override { return ExportFormat::HTML; }

    private:
        Options options_; ///< Configuration settings for this exporter.

        /**
         * Generates the HTML document header including title and styles.
         * @param title Title of the HTML document.
         * @return HTML header string.
         */
        static std::string generate_html_header(const std::string& title);

        /**
         * Generates CSS styles for the HTML report.
         * @return A string containing embedded CSS.
         */
        static std::string generate_css();

        /**
         * Generates JavaScript for interactivity or charts.
         * @return A string containing embedded JavaScript code.
         */
        static std::string generate_javascript();

        /**
         * Generates the metrics summary section in HTML format.
         * @param metrics The metrics summary data.
         * @return HTML markup for the metrics section.
         */
        static std::string generate_metrics_section(const core::MetricsSummary& metrics);

        /**
         * Generates an HTML table of build hotspots.
         * @param metrics The metrics summary data containing hotspots.
         * @return HTML markup for the hotspots table.
         */
        static std::string generate_hotspots_table(const core::MetricsSummary& metrics);

        /**
         * Generates the suggestions section in the HTML report.
         * @param suggestions List of suggestions to display.
         * @return HTML markup for the suggestions section.
         */
        static std::string generate_suggestions_section(const std::vector<core::Suggestion>& suggestions);

        /**
         * Generates the footer for the HTML document.
         * @return HTML markup for the footer.
         */
        static std::string generate_footer();

        /**
         * Escapes HTML special characters in a string.
         * @param text Input text.
         * @return Escaped HTML-safe string.
         */
        static std::string escape_html(const std::string& text);

        /**
         * Formats a duration value in milliseconds into a human-readable string.
         * @param milliseconds Duration in milliseconds.
         * @return Formatted time string.
         */
        static std::string format_duration(double milliseconds);
    };

} // namespace bha::export_module


#endif //HTML_EXPORTER_H
