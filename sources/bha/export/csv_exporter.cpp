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
        csv << "\n\n";
        csv << trace_to_csv(trace);

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

        for (const auto& [file_path, time_ms, impact_score, num_dependent_files, category] : metrics.top_slow_files) {
            csv << escape_csv_field(file_path) << options_.delimiter
                << time_ms << options_.delimiter
                << impact_score << options_.delimiter
                << num_dependent_files << options_.delimiter
                << escape_csv_field(category) << "\n";
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

    std::string CSVExporter::trace_to_csv(const core::BuildTrace& trace)
    {
        std::ostringstream csv;

        csv << "Build Trace Summary\n";
        csv << "Trace ID,Build System,Configuration,Platform,Total Time (ms),Total Files,Commit SHA,Branch,Clean Build\n";
        csv << trace.trace_id << ","
            << trace.build_system << ","
            << trace.configuration << ","
            << trace.platform << ","
            << trace.total_build_time_ms << ","
            << trace.compilation_units.size() << ","
            << trace.commit_sha << ","
            << trace.branch << ","
            << (trace.is_clean_build ? "Yes" : "No") << "\n\n";

        csv << "Compilation Units\n";
        csv << "File Path,Total Time (ms),Preprocessing (ms),Parsing (ms),Codegen (ms),Optimization (ms),Compiler,File Size (bytes),Direct Includes,All Includes\n";
        for (const auto& unit : trace.compilation_units) {
            csv << unit.file_path << ","
                << unit.total_time_ms << ","
                << unit.preprocessing_time_ms << ","
                << unit.parsing_time_ms << ","
                << unit.codegen_time_ms << ","
                << unit.optimization_time_ms << ","
                << unit.compiler_type << ","
                << unit.file_size_bytes << ","
                << unit.direct_includes.size() << ","
                << unit.all_includes.size() << "\n";
        }
        csv << "\n";

        csv << "Dependency Graph\n";
        csv << "Source,Target,Edge Type,Line Number,System Header\n";
        for (const auto& adjacency = trace.dependency_graph.get_adjacency_list(); const auto& [source, edges] : adjacency) {
            for (const auto& edge : edges) {
                csv << source << ","
                    << edge.target << ","
                    << core::to_string(edge.type) << ","
                    << edge.line_number << ","
                    << (edge.is_system_header ? "Yes" : "No") << "\n";
            }
        }
        csv << "\n";

        csv << "Build Targets\n";
        csv << "Target,Dependencies Count\n";
        for (const auto& [target, deps] : trace.targets) {
            csv << target << "," << deps.size() << "\n";
        }
        csv << "\n";

        csv << "Build Order\n";
        csv << "Order,Target\n";
        for (size_t i = 0; i < trace.build_order.size(); ++i) {
            csv << i + 1 << "," << trace.build_order[i] << "\n";
        }

        return csv.str();
    }
}