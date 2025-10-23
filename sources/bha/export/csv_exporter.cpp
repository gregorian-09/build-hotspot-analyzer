//
// Created by gregorian on 23/10/2025.
//

#include "bha/export/csv_exporter.h"
#include "bha/utils/file_utils.h"
#include <sstream>

namespace bha::export_module {

    CSVExporter::CSVExporter(const Options& options)
        : options_(options) {}

    core::Result<void> CSVExporter::export_report(
        const core::MetricsSummary& metrics,
        const std::vector<core::Suggestion>& suggestions,
        const core::BuildTrace& trace,
        const std::string& output_path
    ) {
        std::ostringstream csv;

        csv << hotspots_to_csv(metrics);
        csv << "\n\n";
        csv << suggestions_to_csv(suggestions);

        if (!utils::write_file(output_path, csv.str())) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::FILE_WRITE_ERROR,
                "Failed to write CSV to: " + output_path
            });
        }

        return core::Result<void>::success();
    }

    std::string CSVExporter::escape_csv_field(const std::string& field) const
    {
        if (!options_.quote_strings) {
            return field;
        }

        const bool needs_quoting = field.find(options_.delimiter) != std::string::npos ||
                             field.find('"') != std::string::npos ||
                             field.find('\n') != std::string::npos;

        if (!needs_quoting) {
            return field;
        }

        std::string escaped = "\"";
        for (const char c : field) {
            if (c == '"') {
                escaped += "\"\"";
            } else {
                escaped += c;
            }
        }
        escaped += "\"";

        return escaped;
    }

    std::string CSVExporter::hotspots_to_csv(const core::MetricsSummary& metrics) const
    {
        std::ostringstream csv;

        if (options_.include_header) {
            csv << "File" << options_.delimiter
                << "Time (ms)" << options_.delimiter
                << "Impact Score" << options_.delimiter
                << "Dependents" << options_.delimiter
                << "Category\n";
        }

        for (const auto& hotspot : metrics.top_slow_files) {
            csv << escape_csv_field(hotspot.file_path) << options_.delimiter
                << hotspot.time_ms << options_.delimiter
                << hotspot.impact_score << options_.delimiter
                << hotspot.num_dependent_files << options_.delimiter
                << escape_csv_field(hotspot.category) << "\n";
        }

        return csv.str();
    }

    std::string CSVExporter::suggestions_to_csv(const std::vector<core::Suggestion>& suggestions) const
    {
        std::ostringstream csv;

        if (options_.include_header) {
            csv << "Type" << options_.delimiter
                << "Priority" << options_.delimiter
                << "Title" << options_.delimiter
                << "File" << options_.delimiter
                << "Savings (ms)" << options_.delimiter
                << "Confidence\n";
        }

        for (const auto& suggestion : suggestions) {
            csv << static_cast<int>(suggestion.type) << options_.delimiter
                << static_cast<int>(suggestion.priority) << options_.delimiter
                << escape_csv_field(suggestion.title) << options_.delimiter
                << escape_csv_field(suggestion.file_path) << options_.delimiter
                << suggestion.estimated_time_savings_ms << options_.delimiter
                << suggestion.confidence << "\n";
        }

        return csv.str();
    }
}