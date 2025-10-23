//
// Created by gregorian on 23/10/2025.
//

#include "bha/export/html_exporter.h"
#include "bha/utils/file_utils.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <utility>

namespace bha::export_module {

    HTMLExporter::HTMLExporter(Options options)
        : options_(std::move(options)) {}

    core::Result<void> HTMLExporter::export_report(
        const core::MetricsSummary& metrics,
        const std::vector<core::Suggestion>& suggestions,
        const core::BuildTrace& trace,
        const std::string& output_path
    ) {
        std::ostringstream html;

        html << generate_html_header(options_.title);

        if (options_.embed_css) {
            html << "<style>\n" << generate_css() << "\n</style>\n";
        }

        html << "<body>\n";
        html << "<div class='container'>\n";
        html << "<div class='header'>\n";
        html << "<h1>" << escape_html(options_.title) << "</h1>\n";
        html << "<p>Build System: " << escape_html(trace.build_system) << "</p>\n";
        html << "<p>Configuration: " << escape_html(trace.configuration) << "</p>\n";
        html << "</div>\n";

        html << generate_metrics_section(metrics);
        html << generate_hotspots_table(metrics);
        html << generate_suggestions_section(suggestions);

        if (options_.embed_javascript) {
            html << "<script>\n" << generate_javascript() << "\n</script>\n";
        }

        html << "</div>\n";
        html << generate_footer();
        html << "</body>\n</html>\n";

        if (!utils::write_file(output_path, html.str())) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::FILE_WRITE_ERROR,
                "Failed to write HTML to: " + output_path
            });
        }

        return core::Result<void>::success();
    }

    std::string HTMLExporter::generate_html_header(const std::string& title) {
        return "<!DOCTYPE html>\n"
               "<html lang='en'>\n"
               "<head>\n"
               "<meta charset='UTF-8'>\n"
               "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
               "<title>" + escape_html(title) + "</title>\n";
    }

    std::string HTMLExporter::generate_css() {
        return R"(
    body {
        font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
        margin: 0;
        padding: 20px;
        background: #f5f5f5;
    }
    .container {
        max-width: 1400px;
        margin: 0 auto;
        background: white;
        padding: 30px;
        border-radius: 8px;
        box-shadow: 0 2px 8px rgba(0,0,0,0.1);
    }
    .header {
        border-bottom: 2px solid #e0e0e0;
        padding-bottom: 20px;
        margin-bottom: 30px;
    }
    .metrics-grid {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
        gap: 20px;
        margin-bottom: 30px;
    }
    .metric-card {
        padding: 20px;
        background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
        color: white;
        border-radius: 8px;
    }
    .metric-value {
        font-size: 36px;
        font-weight: bold;
        margin: 10px 0;
    }
    .metric-label {
        font-size: 14px;
        opacity: 0.9;
    }
    .section {
        margin-bottom: 40px;
    }
    .section-title {
        font-size: 24px;
        font-weight: bold;
        margin-bottom: 20px;
        color: #333;
    }
    table {
        width: 100%;
        border-collapse: collapse;
    }
    th, td {
        padding: 12px;
        text-align: left;
        border-bottom: 1px solid #e0e0e0;
    }
    th {
        background: #f8f8f8;
        font-weight: 600;
    }
    .hotspot-bar {
        background: linear-gradient(90deg, #f44336 0%, #ff9800 100%);
        height: 20px;
        border-radius: 4px;
    }
    .suggestion {
        padding: 15px;
        margin-bottom: 15px;
        border-left: 4px solid #667eea;
        background: #f9f9f9;
        border-radius: 4px;
    }
    .suggestion-title {
        font-weight: bold;
        margin-bottom: 5px;
    }
    .suggestion-priority-high { border-left-color: #f44336; }
    .suggestion-priority-medium { border-left-color: #ff9800; }
    .suggestion-priority-low { border-left-color: #4caf50; }
    )";
    }

    std::string HTMLExporter::generate_javascript() {
        return R"(
    document.addEventListener('DOMContentLoaded', function() {
        const rows = document.querySelectorAll('table tr[data-file]');
        rows.forEach(row => {
            row.addEventListener('click', function() {
                const file = this.dataset.file;
                console.log('Clicked:', file);
            });
        });
    });
    )";
    }

    std::string HTMLExporter::generate_metrics_section(const core::MetricsSummary& metrics) {
        std::ostringstream html;

        html << "<div class='metrics-grid'>\n";

        html << "<div class='metric-card'>\n";
        html << "<div class='metric-label'>Total Files</div>\n";
        html << "<div class='metric-value'>" << metrics.total_files_compiled << "</div>\n";
        html << "</div>\n";

        html << "<div class='metric-card'>\n";
        html << "<div class='metric-label'>Average Time</div>\n";
        html << "<div class='metric-value'>"
             << format_duration(metrics.average_file_time_ms) << "</div>\n";
        html << "</div>\n";

        html << "<div class='metric-card'>\n";
        html << "<div class='metric-label'>P95 Time</div>\n";
        html << "<div class='metric-value'>"
             << format_duration(metrics.p95_file_time_ms) << "</div>\n";
        html << "</div>\n";

        html << "<div class='metric-card'>\n";
        html << "<div class='metric-label'>Max Depth</div>\n";
        html << "<div class='metric-value'>" << metrics.max_include_depth << "</div>\n";
        html << "</div>\n";

        html << "</div>\n";

        return html.str();
    }

    std::string HTMLExporter::generate_hotspots_table(const core::MetricsSummary& metrics) {
        std::ostringstream html;

        html << "<div class='section'>\n";
        html << "<div class='section-title'>Top Hotspots</div>\n";
        html << "<table>\n";
        html << "<thead>\n<tr>\n";
        html << "<th>File</th><th>Time</th><th>Impact Score</th><th>Dependents</th><th>Visual</th>\n";
        html << "</tr>\n</thead>\n<tbody>\n";

        double max_time = 0;
        for (const auto& hotspot : metrics.top_slow_files) {
            max_time = std::max(max_time, hotspot.time_ms);
        }

        for (const auto& hotspot : metrics.top_slow_files) {
            html << "<tr data-file='" << escape_html(hotspot.file_path) << "'>\n";
            html << "<td>" << escape_html(hotspot.file_path) << "</td>\n";
            html << "<td>" << format_duration(hotspot.time_ms) << "</td>\n";
            html << "<td>" << std::fixed << std::setprecision(2)
                 << hotspot.impact_score << "</td>\n";
            html << "<td>" << hotspot.num_dependent_files << "</td>\n";

            const double bar_width = max_time > 0 ? (hotspot.time_ms / max_time * 100) : 0;
            html << "<td><div class='hotspot-bar' style='width: "
                 << bar_width << "%'></div></td>\n";
            html << "</tr>\n";
        }

        html << "</tbody>\n</table>\n";
        html << "</div>\n";

        return html.str();
    }

    std::string HTMLExporter::generate_suggestions_section(
        const std::vector<core::Suggestion>& suggestions
    ) {
        std::ostringstream html;

        html << "<div class='section'>\n";
        html << "<div class='section-title'>Optimization Suggestions</div>\n";

        for (const auto& suggestion : suggestions) {
            std::string priority_class;
            if (suggestion.priority == core::Priority::HIGH) {
                priority_class = "suggestion-priority-high";
            } else if (suggestion.priority == core::Priority::MEDIUM) {
                priority_class = "suggestion-priority-medium";
            } else {
                priority_class = "suggestion-priority-low";
            }

            html << "<div class='suggestion " << priority_class << "'>\n";
            html << "<div class='suggestion-title'>"
                 << escape_html(suggestion.title) << "</div>\n";
            html << "<p>" << escape_html(suggestion.description) << "</p>\n";
            html << "<p><small>Estimated savings: "
                 << format_duration(suggestion.estimated_time_savings_ms)
                 << " (Confidence: " << std::fixed << std::setprecision(0)
                 << (suggestion.confidence * 100) << "%)</small></p>\n";
            html << "</div>\n";
        }

        html << "</div>\n";

        return html.str();
    }

    std::string HTMLExporter::generate_footer() {
        return "<div style='margin-top: 40px; padding-top: 20px; border-top: 1px solid #e0e0e0; "
               "text-align: center; color: #666;'>\n"
               "<p>Generated by Build Hotspot Analyzer v1.0.0</p>\n"
               "</div>\n";
    }

    std::string HTMLExporter::escape_html(const std::string& text) {
        std::string escaped;
        escaped.reserve(text.size());

        for (const char c : text) {
            switch (c) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                case '"': escaped += "&quot;"; break;
                case '\'': escaped += "&#39;"; break;
                default: escaped += c;
            }
        }

        return escaped;
    }

    std::string HTMLExporter::format_duration(double milliseconds) {
        if (milliseconds < 1000) {
            return std::to_string(static_cast<int>(milliseconds)) + "ms";
        }
        return std::to_string(milliseconds / 1000.0).substr(0, 4) + "s";
    }

}
