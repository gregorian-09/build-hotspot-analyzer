//
// Created by gregorian on 23/10/2025.
//

#ifndef JSON_EXPORTER_H
#define JSON_EXPORTER_H

#include "bha/export/exporter.h"
#include <nlohmann/json.hpp>

namespace bha::export_module {

    /**
     * JSON exporter implementation for build reports.
     *
     * Serializes metrics, suggestions, and trace data into structured JSON format.
     * Supports pretty-printing, selective inclusion of sections, and formatting options.
     */
    class JSONExporter final : public Exporter {
    public:
        /**
         * Configuration options for JSON export behavior.
         */
        struct Options {
            bool pretty_print = true;          ///< Enable pretty-printed JSON output.
            bool include_full_trace = false;   ///< Include full trace data if true.
            bool include_suggestions = true;   ///< Include suggestions in the output.
            int indent_size = 2;               ///< Indentation level for pretty printing.
        };

        /**
         * Constructs a JSON exporter with the specified options.
         * @param options Export customization options.
         */
        explicit JSONExporter(const Options& options);

        JSONExporter() : JSONExporter(Options{}) {};

        /**
         * Exports the report to a JSON file.
         * @param metrics Build metrics summary.
         * @param suggestions Optimization suggestions.
         * @param trace Build trace data.
         * @param output_path Destination file path for JSON output.
         * @return A core::Result indicating success or failure.
         */
        core::Result<void> export_report(
            const core::MetricsSummary& metrics,
            const std::vector<core::Suggestion>& suggestions,
            const core::BuildTrace& trace,
            const std::string& output_path
        ) override;

        /**
         * Returns the default file extension for JSON exports.
         * @return The string ".json".
         */
        [[nodiscard]] std::string get_default_extension() const override { return ".json"; }

        /**
         * Returns the export format type.
         * @return ExportFormat::JSON.
         */
        [[nodiscard]] ExportFormat get_format() const override { return ExportFormat::JSON; }

    private:
        Options options_; ///< Configuration options for this exporter instance.

        /**
         * Converts metrics data to a JSON representation.
         * @param metrics Metrics summary object.
         * @return JSON object representing the metrics.
         */
        static nlohmann::json metrics_to_json(const core::MetricsSummary& metrics);

        /**
         * Converts suggestion data to JSON.
         * @param suggestions List of suggestions.
         * @return JSON array of suggestion entries.
         */
        static nlohmann::json suggestions_to_json(const std::vector<core::Suggestion>& suggestions);

        /**
         * Converts build trace data to JSON.
         * @param trace Build trace object.
         * @return JSON representation of the trace.
         */
        static nlohmann::json trace_to_json(const core::BuildTrace& trace);

        /**
         * Converts a hotspot object to JSON format.
         * @param hotspot Hotspot data element.
         * @return JSON representation of the hotspot.
         */
        static nlohmann::json hotspot_to_json(const core::Hotspot& hotspot);
    };

} // namespace bha::export_module


#endif //JSON_EXPORTER_H
