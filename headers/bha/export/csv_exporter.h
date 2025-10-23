//
// Created by gregorian on 23/10/2025.
//

#ifndef CSV_EXPORTER_H
#define CSV_EXPORTER_H

#include "bha/export/exporter.h"

namespace bha::export_module {

    /**
     * Exports build reports in CSV (Comma Separated Values) format.
     *
     * The CSVExporter converts the metrics, suggestions, and optional trace data
     * into a tabular CSV representation. It supports configurable delimiters,
     * quoting rules, and optional inclusion of header rows.
     */
    class CSVExporter final : public Exporter {
    public:
        /**
         * Configuration options for CSV export behavior.
         */
        struct Options {
            char delimiter = ',';         ///< Field delimiter character (e.g. comma, semicolon).
            bool include_header = true;   ///< Whether to emit a header row with column names.
            bool quote_strings = true;    ///< Whether to wrap string fields in quotes.
        };

        /**
         * Constructs a CSV exporter with the given options.
         * @param options Settings controlling delimiter, header, quoting, etc.
         */
        explicit CSVExporter(const Options& options);

        CSVExporter() : CSVExporter(Options{}) {};

        /**
         * Exports the build report as a CSV file.
         * @param metrics Summary of build performance metrics.
         * @param suggestions Optimization suggestions.
         * @param trace Build trace data (may or may not be used depending on exporter).
         * @param output_path Path to the output CSV file.
         * @return A Result indicating success or failure of the export.
         */
        core::Result<void> export_report(
            const core::MetricsSummary& metrics,
            const std::vector<core::Suggestion>& suggestions,
            const core::BuildTrace& trace,
            const std::string& output_path
        ) override;

        /**
         * Returns the default file extension for CSV exports.
         * @return The string ".csv".
         */
        [[nodiscard]] std::string get_default_extension() const override { return ".csv"; }

        /**
         * Returns the export format enumeration for CSV.
         * @return ExportFormat::CSV.
         */
        [[nodiscard]] ExportFormat get_format() const override { return ExportFormat::CSV; }

    private:
        Options options_;  ///< Configuration settings for this CSV exporter instance.

        /**
         * Escapes a single CSV field for safe output.
         *
         * Handles quoting, embedded delimiters, and escape sequences as needed.
         *
         * @param field The unescaped field string.
         * @return The properly escaped CSV field.
         */
        std::string escape_csv_field(const std::string& field) const;

        /**
         * Serializes metric data (especially hotspots) into CSV rows.
         * @param metrics The metrics summary including hotspot info.
         * @return A CSV-formatted string for hotspots section.
         */
        std::string hotspots_to_csv(const core::MetricsSummary& metrics) const;

        /**
         * Serializes suggestion entries into CSV rows.
         * @param suggestions The vector of suggestions to output.
         * @return A CSV-formatted string for suggestions section.
         */
        std::string suggestions_to_csv(const std::vector<core::Suggestion>& suggestions) const;
    };

} // namespace bha::export_module


#endif //CSV_EXPORTER_H
