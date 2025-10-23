//
// Created by gregorian on 23/10/2025.
//

#ifndef EXPORTER_H
#define EXPORTER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <string>
#include <memory>

namespace bha::export_module {

    /**
     * Supported export output formats.
     */
    enum class ExportFormat {
        JSON,       ///< Export in JSON format.
        HTML,       ///< Export in HTML format.
        CSV,        ///< Export in CSV format.
        MARKDOWN,   ///< Export in Markdown format.
        TEXT        ///< Export in plain text format.
    };

    /**
     * Abstract base class for report exporters.
     *
     * Defines the interface for exporting build metrics, suggestions, and traces
     * to various output formats such as JSON, HTML, or CSV.
     */
    class Exporter {
    public:
        virtual ~Exporter() = default;

        /**
         * Exports the provided build report to a specified output file.
         * @param metrics Summary of build performance metrics.
         * @param suggestions List of optimization or improvement suggestions.
         * @param trace Build trace data collected during compilation.
         * @param output_path Destination file path for the exported report.
         * @return A core::Result indicating success or failure of the export.
         */
        virtual core::Result<void> export_report(
            const core::MetricsSummary& metrics,
            const std::vector<core::Suggestion>& suggestions,
            const core::BuildTrace& trace,
            const std::string& output_path
        ) = 0;

        /**
         * Returns the default file extension for the export format.
         * @return Default extension (e.g., ".json", ".html").
         */
        [[nodiscard]] virtual std::string get_default_extension() const = 0;

        /**
         * Returns the export format type.
         * @return The ExportFormat enumeration value.
         */
        [[nodiscard]] virtual ExportFormat get_format() const = 0;
    };

    /**
     * Factory for creating exporters based on format type.
     *
     * Provides static utility functions to instantiate exporters for specific formats
     * and to convert between string and enum representations of export formats.
     */
    class ExporterFactory {
    public:
        /**
         * Creates an exporter for the specified format.
         * @param format The desired output format.
         * @return A unique pointer to an Exporter instance.
         */
        static std::unique_ptr<Exporter> create_exporter(ExportFormat format);

        /**
         * Converts a string to an ExportFormat enum value.
         * @param format_str String representation of the format (e.g., "json").
         * @return The corresponding ExportFormat value.
         */
        static ExportFormat format_from_string(const std::string& format_str);

        /**
         * Converts an ExportFormat enum to a string representation.
         * @param format ExportFormat value.
         * @return String name of the format.
         */
        static std::string format_to_string(ExportFormat format);
    };

} // namespace bha::export_module

#endif //EXPORTER_H
