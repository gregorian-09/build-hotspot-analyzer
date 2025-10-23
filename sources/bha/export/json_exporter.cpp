//
// Created by grego on 23/10/2025.
//

#include "bha/export/json_exporter.h"
#include "bha/utils/file_utils.h"
#include <chrono>

namespace bha::export_module {

    JSONExporter::JSONExporter(const Options& options)
        : options_(options) {}

    core::Result<void> JSONExporter::export_report(
        const core::MetricsSummary& metrics,
        const std::vector<core::Suggestion>& suggestions,
        const core::BuildTrace& trace,
        const std::string& output_path
    ) {
        nlohmann::json report;

        report["metadata"] = {
            {"generated_at", std::chrono::system_clock::now().time_since_epoch().count()},
            {"tool_version", "1.0.0"},
            {"format_version", "1.0"}
        };

        report["build_info"] = {
            {"trace_id", trace.trace_id},
            {"build_system", trace.build_system},
            {"configuration", trace.configuration},
            {"platform", trace.platform},
            {"total_build_time_ms", trace.total_build_time_ms}
        };

        report["metrics"] = metrics_to_json(metrics);

        if (options_.include_suggestions) {
            report["suggestions"] = suggestions_to_json(suggestions);
        }

        if (options_.include_full_trace) {
            report["trace"] = trace_to_json(trace);
        }

        std::string json_str;
        if (options_.pretty_print) {
            json_str = report.dump(options_.indent_size);
        } else {
            json_str = report.dump();
        }

        if (!utils::write_file(output_path, json_str)) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::FILE_WRITE_ERROR,
                "Failed to write JSON to: " + output_path
            });
        }

        return core::Result<void>::success();
    }

    nlohmann::json JSONExporter::metrics_to_json(const core::MetricsSummary& metrics) {
        nlohmann::json j;

        j["summary"] = {
            {"total_files_compiled", metrics.total_files_compiled},
            {"total_headers_parsed", metrics.total_headers_parsed},
            {"average_file_time_ms", metrics.average_file_time_ms},
            {"median_file_time_ms", metrics.median_file_time_ms},
            {"p95_file_time_ms", metrics.p95_file_time_ms},
            {"p99_file_time_ms", metrics.p99_file_time_ms}
        };

        j["dependencies"] = {
            {"total_dependencies", metrics.total_dependencies},
            {"average_include_depth", metrics.average_include_depth},
            {"max_include_depth", metrics.max_include_depth},
            {"circular_dependency_count", metrics.circular_dependency_count}
        };

        nlohmann::json hotspots_array = nlohmann::json::array();
        for (const auto& hotspot : metrics.top_slow_files) {
            hotspots_array.push_back(hotspot_to_json(hotspot));
        }
        j["hotspots"] = hotspots_array;

        return j;
    }

    nlohmann::json JSONExporter::suggestions_to_json(
        const std::vector<core::Suggestion>& suggestions
    ) {
        nlohmann::json arr = nlohmann::json::array();

        for (const auto& suggestion : suggestions) {
            nlohmann::json s;
            s["id"] = suggestion.id;
            s["type"] = static_cast<int>(suggestion.type);
            s["priority"] = static_cast<int>(suggestion.priority);
            s["confidence"] = suggestion.confidence;
            s["title"] = suggestion.title;
            s["description"] = suggestion.description;
            s["file_path"] = suggestion.file_path;
            s["estimated_savings_ms"] = suggestion.estimated_time_savings_ms;
            s["estimated_savings_percent"] = suggestion.estimated_time_savings_percent;
            s["is_safe"] = suggestion.is_safe;
            s["related_files"] = suggestion.related_files;

            arr.push_back(s);
        }

        return arr;
    }

    nlohmann::json JSONExporter::trace_to_json(const core::BuildTrace& trace) {
        nlohmann::json j;
        j["trace_id"] = trace.trace_id;
        j["compilation_units_count"] = trace.compilation_units.size();
        return j;
    }

    nlohmann::json JSONExporter::hotspot_to_json(const core::Hotspot& hotspot) {
        return {
            {"file_path", hotspot.file_path},
            {"time_ms", hotspot.time_ms},
            {"impact_score", hotspot.impact_score},
            {"num_dependent_files", hotspot.num_dependent_files},
            {"category", hotspot.category}
        };
    }

}