//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/exporters/exporter.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace bha::exporters
{
    namespace {

        /**
         * Formats a timestamp to ISO 8601.
         */
        std::string format_timestamp(const Timestamp ts) {
            const auto time_t_val = std::chrono::system_clock::to_time_t(ts);
            std::ostringstream ss;

#ifdef _WIN32
            std::tm time_info;
            gmtime_s(&time_info, &time_t_val);
            ss << std::put_time(&time_info, "%Y-%m-%dT%H:%M:%SZ");
#else
            ss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");
#endif

            return ss.str();
        }

        /**
         * Formats a duration to milliseconds.
         */
        double duration_to_ms(Duration d) {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(d).count()) / 1000.0;
        }

        /**
         * Escapes HTML special characters.
         */
        std::string escape_html(const std::string& text) {
            std::string result;
            result.reserve(static_cast<std::size_t>(static_cast<double>(text.size()) * 1.1));
            for (const char c : text) {
                switch (c) {
                case '&': result += "&amp;"; break;
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '"': result += "&quot;"; break;
                case '\'': result += "&#39;"; break;
                default: result += c; break;
                }
            }
            return result;
        }

        /**
         * Escapes CSV special characters.
         */
        std::string escape_csv(const std::string& text) {
            if (text.find_first_of(",\"\n\r") == std::string::npos) {
                return text;
            }
            std::string result = "\"";
            for (const char c : text) {
                if (c == '"') {
                    result += "\"\"";
                } else {
                    result += c;
                }
            }
            result += "\"";
            return result;
        }

    }  // namespace

    // =============================================================================
    // Format Conversion
    // =============================================================================

    std::string_view format_to_string(const ExportFormat format) noexcept {
        switch (format) {
        case ExportFormat::JSON: return "json";
        case ExportFormat::HTML: return "html";
        case ExportFormat::CSV: return "csv";
        case ExportFormat::SARIF: return "sarif";
        case ExportFormat::Markdown: return "markdown";
        }
        return "unknown";
    }

    std::optional<ExportFormat> string_to_format(const std::string_view str) noexcept {
        if (str == "json" || str == "JSON") return ExportFormat::JSON;
        if (str == "html" || str == "HTML") return ExportFormat::HTML;
        if (str == "csv" || str == "CSV") return ExportFormat::CSV;
        if (str == "sarif" || str == "SARIF") return ExportFormat::SARIF;
        if (str == "markdown" || str == "md" || str == "Markdown") return ExportFormat::Markdown;
        return std::nullopt;
    }

    // =============================================================================
    // Exporter Factory
    // =============================================================================

    Result<std::unique_ptr<IExporter>, Error> ExporterFactory::create(const ExportFormat format) {
        switch (format) {
        case ExportFormat::JSON:
            return Result<std::unique_ptr<IExporter>, Error>::success(
                std::make_unique<JsonExporter>()
            );
        case ExportFormat::HTML:
            return Result<std::unique_ptr<IExporter>, Error>::success(
                std::make_unique<HtmlExporter>()
            );
        case ExportFormat::CSV:
            return Result<std::unique_ptr<IExporter>, Error>::success(
                std::make_unique<CsvExporter>()
            );
        case ExportFormat::Markdown:
            return Result<std::unique_ptr<IExporter>, Error>::success(
                std::make_unique<MarkdownExporter>()
            );
        case ExportFormat::SARIF:
            return Result<std::unique_ptr<IExporter>, Error>::failure(
                Error(ErrorCode::NotFound, "SARIF exporter not yet implemented")
            );
        }
        return Result<std::unique_ptr<IExporter>, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Unknown export format")
        );
    }

    Result<std::unique_ptr<IExporter>, Error> ExporterFactory::create_for_file(const fs::path& path) {
        std::string ext = path.extension().string();
        std::ranges::transform(ext, ext.begin(),
                               [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (ext == ".json") return create(ExportFormat::JSON);
        if (ext == ".html" || ext == ".htm") return create(ExportFormat::HTML);
        if (ext == ".csv") return create(ExportFormat::CSV);
        if (ext == ".md" || ext == ".markdown") return create(ExportFormat::Markdown);
        if (ext == ".sarif") return create(ExportFormat::SARIF);

        return Result<std::unique_ptr<IExporter>, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Cannot determine format from extension: " + ext)
        );
    }

    std::vector<ExportFormat> ExporterFactory::available_formats() {
        return {
            ExportFormat::JSON,
            ExportFormat::HTML,
            ExportFormat::CSV,
            ExportFormat::Markdown
        };
    }

    // =============================================================================
    // JSON Exporter
    // =============================================================================

    Result<void, Error> JsonExporter::export_to_file(
        const fs::path& path,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options,
        const ExportProgressCallback progress
    ) const {
        std::ofstream file(path);
        if (!file.is_open()) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, "Failed to open file for writing: " + path.string())
            );
        }
        return export_to_stream(file, analysis, suggestions, options, progress);
    }

    Result<void, Error> JsonExporter::export_to_stream(
        std::ostream& stream,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options,
        ExportProgressCallback progress
    ) const {
        using json = nlohmann::json;

        json output;

        // Metadata
        if (options.include_metadata) {
            output["$schema"] = "https://bha.dev/schemas/analysis-v" + options.json_schema_version + ".json";
            output["schema_version"] = options.json_schema_version;
            output["bha_version"] = "1.0.0";
            output["generated_at"] = format_timestamp(std::chrono::system_clock::now());
        }

        json summary;
        summary["total_files"] = analysis.files.size();
        summary["total_compile_time_ms"] = duration_to_ms(analysis.performance.total_build_time);
        summary["analysis_duration_ms"] = duration_to_ms(analysis.analysis_duration);
        summary["suggestions_count"] = suggestions.size();
        output["summary"] = summary;

        if (options.include_file_details) {
            json files = json::array();
            std::size_t file_count = 0;
            std::size_t total_files = analysis.files.size();

            for (const auto& file : analysis.files) {
                if (options.min_compile_time > Duration::zero() &&
                    file.compile_time < options.min_compile_time) {
                    continue;
                    }

                if (options.max_files > 0 && file_count >= options.max_files) {
                    break;
                }

                json file_entry;
                file_entry["path"] = file.file.string();
                file_entry["total_time_ms"] = duration_to_ms(file.compile_time);
                file_entry["frontend_time_ms"] = duration_to_ms(file.frontend_time);
                file_entry["backend_time_ms"] = duration_to_ms(file.backend_time);
                file_entry["lines_of_code"] = file.lines_of_code;
                file_entry["include_count"] = file.include_count;

                files.push_back(file_entry);
                file_count++;

                if (progress) {
                    progress(file_count, total_files, "Exporting files");
                }
            }
            output["files"] = files;
        }

        if (options.include_dependencies && !analysis.dependencies.headers.empty()) {
            json deps;
            deps["total_includes"] = analysis.dependencies.total_includes;
            deps["unique_headers"] = analysis.dependencies.unique_headers;
            deps["max_depth"] = analysis.dependencies.max_include_depth;
            deps["circular_dependencies_count"] = analysis.dependencies.circular_dependencies.size();

            json headers_array = json::array();
            for (const auto& header : analysis.dependencies.headers) {
                json h;
                h["path"] = header.path.string();
                h["inclusion_count"] = header.inclusion_count;
                h["including_files"] = header.including_files;
                h["parse_time_ms"] = duration_to_ms(header.total_parse_time);
                h["impact_score"] = header.impact_score;
                headers_array.push_back(h);
            }
            deps["headers"] = headers_array;

            output["dependencies"] = deps;
        }

        if (options.include_templates && !analysis.templates.templates.empty()) {
            json templates;
            templates["total_instantiations"] = analysis.templates.total_instantiations;
            templates["total_time_ms"] = duration_to_ms(analysis.templates.total_template_time);

            json tmpl_array = json::array();
            for (const auto& tmpl : analysis.templates.templates) {
                json t;
                t["name"] = !tmpl.full_signature.empty() ? tmpl.full_signature : tmpl.name;
                t["type"] = tmpl.name;  // Keep original event type (InstantiateClass, etc.)
                t["count"] = tmpl.instantiation_count;
                t["time_ms"] = duration_to_ms(tmpl.total_time);
                t["time_percent"] = tmpl.time_percent;
                tmpl_array.push_back(t);
            }
            templates["templates"] = tmpl_array;

            output["templates"] = templates;
        }

        if (options.include_symbols && !analysis.symbols.symbols.empty()) {
            json symbols;
            symbols["total_symbols"] = analysis.symbols.total_symbols;
            symbols["unused_symbols"] = analysis.symbols.unused_symbols;

            json sym_array = json::array();
            for (const auto& sym : analysis.symbols.symbols) {
                json s;
                s["name"] = sym.name;
                s["type"] = sym.type;
                s["defined_in"] = sym.defined_in.string();
                s["usage_count"] = sym.usage_count;
                sym_array.push_back(s);
            }
            symbols["symbols"] = sym_array;

            output["symbols"] = symbols;
        }

        if (options.include_suggestions && !suggestions.empty()) {
            json sugg_array = json::array();
            std::size_t sugg_count = 0;

            for (const auto& sugg : suggestions) {
                if (sugg.confidence < options.min_confidence) {
                    continue;
                }
                if (options.max_suggestions > 0 && sugg_count >= options.max_suggestions) {
                    break;
                }

                json sugg_entry;
                sugg_entry["type"] = sugg.type;
                sugg_entry["title"] = sugg.title;
                sugg_entry["description"] = sugg.description;
                sugg_entry["target_file"] = sugg.target_file.path.string();
                sugg_entry["target_line"] = sugg.target_file.line_start;
                sugg_entry["confidence"] = sugg.confidence;
                sugg_entry["priority"] = sugg.priority;
                sugg_entry["estimated_savings_ms"] = duration_to_ms(sugg.estimated_savings);
                sugg_entry["auto_applicable"] = sugg.is_safe;

                if (!sugg.before_code.code.empty()) {
                    sugg_entry["before_code"] = sugg.before_code.code;
                }
                if (!sugg.after_code.code.empty()) {
                    sugg_entry["after_code"] = sugg.after_code.code;
                }

                sugg_array.push_back(sugg_entry);
                sugg_count++;
            }
            output["suggestions"] = sugg_array;
        }

        if (options.pretty_print) {
            stream << std::setw(2) << output << std::endl;
        } else {
            stream << output << std::endl;
        }

        return Result<void, Error>::success();
    }

    Result<std::string, Error> JsonExporter::export_to_string(
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options
    ) const {
        std::ostringstream ss;
        if (auto result = export_to_stream(ss, analysis, suggestions, options, nullptr); result.is_err()) {
            return Result<std::string, Error>::failure(result.error());
        }
        return Result<std::string, Error>::success(ss.str());
    }

    // =============================================================================
    // HTML Exporter
    // =============================================================================

    Result<void, Error> HtmlExporter::export_to_file(
        const fs::path& path,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options,
        const ExportProgressCallback progress
    ) const {
        std::ofstream file(path);
        if (!file.is_open()) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, "Failed to open file for writing: " + path.string())
            );
        }
        return export_to_stream(file, analysis, suggestions, options, progress);
    }

    Result<void, Error> HtmlExporter::export_to_stream(
        std::ostream& stream,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options,
        ExportProgressCallback /* progress */
    ) const {
        ExportOptions json_opts = options;
        json_opts.pretty_print = false;

        JsonExporter json_exporter;
        auto json_result = json_exporter.export_to_string(analysis, suggestions, json_opts);
        if (json_result.is_err()) {
            return Result<void, Error>::failure(json_result.error());
        }

        std::string theme_class = options.html_dark_mode ? "dark-theme" : "light-theme";

        // HTML template with embedded D3.js visualization
        stream << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)HTML" << escape_html(options.html_title) << R"HTML(</title>
    <style>
        :root {
            --bg-primary: #ffffff;
            --bg-secondary: #f8f9fa;
            --text-primary: #212529;
            --text-secondary: #6c757d;
            --border-color: #dee2e6;
            --accent-color: #0d6efd;
            --success-color: #198754;
            --warning-color: #ffc107;
            --danger-color: #dc3545;
        }
        .dark-theme {
            --bg-primary: #1a1a2e;
            --bg-secondary: #16213e;
            --text-primary: #eaeaea;
            --text-secondary: #a0a0a0;
            --border-color: #3a3a5a;
            --accent-color: #4dabf7;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg-primary);
            color: var(--text-primary);
            line-height: 1.6;
        }
        .container { max-width: 1400px; margin: 0 auto; padding: 20px; }
        header {
            background: var(--bg-secondary);
            border-bottom: 1px solid var(--border-color);
            padding: 20px 0;
            margin-bottom: 30px;
        }
        h1 { font-size: 2rem; font-weight: 600; }
        h2 { font-size: 1.5rem; margin-bottom: 15px; color: var(--text-primary); }
        .summary-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .summary-card {
            background: var(--bg-secondary);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 20px;
        }
        .summary-card h3 { font-size: 0.875rem; color: var(--text-secondary); margin-bottom: 5px; }
        .summary-card .value { font-size: 2rem; font-weight: 700; color: var(--accent-color); }
        .summary-card .unit { font-size: 0.875rem; color: var(--text-secondary); }
        .section { margin-bottom: 40px; }
        table {
            width: 100%;
            border-collapse: collapse;
            background: var(--bg-secondary);
            border-radius: 8px;
            overflow: hidden;
        }
        th, td {
            padding: 12px 16px;
            text-align: left;
            border-bottom: 1px solid var(--border-color);
        }
        th { background: var(--bg-primary); font-weight: 600; }
        tr:hover { background: rgba(13, 110, 253, 0.05); }
        .time-bar {
            height: 8px;
            background: var(--accent-color);
            border-radius: 4px;
            min-width: 4px;
        }
        .suggestion-card {
            background: var(--bg-secondary);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 20px;
            margin-bottom: 15px;
        }
        .suggestion-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
        }
        .suggestion-title { font-weight: 600; font-size: 1.1rem; }
        .suggestion-badge {
            display: inline-block;
            padding: 4px 8px;
            border-radius: 4px;
            font-size: 0.75rem;
            font-weight: 600;
        }
        .badge-high { background: var(--danger-color); color: white; }
        .badge-medium { background: var(--warning-color); color: black; }
        .badge-low { background: var(--success-color); color: white; }
        .suggestion-meta { font-size: 0.875rem; color: var(--text-secondary); margin-bottom: 10px; }
        .code-block {
            background: #1e1e1e;
            color: #d4d4d4;
            padding: 15px;
            border-radius: 6px;
            font-family: 'Fira Code', 'Monaco', monospace;
            font-size: 0.875rem;
            overflow-x: auto;
            margin-top: 10px;
        }
        #graph-container {
            width: 100%;
            height: 500px;
            background: var(--bg-secondary);
            border: 1px solid var(--border-color);
            border-radius: 8px;
        }
        .controls { margin-bottom: 20px; }
        input[type="text"] {
            padding: 10px 15px;
            border: 1px solid var(--border-color);
            border-radius: 6px;
            background: var(--bg-secondary);
            color: var(--text-primary);
            width: 300px;
        }
        .tabs { display: flex; border-bottom: 1px solid var(--border-color); margin-bottom: 20px; }
        .tab {
            padding: 10px 20px;
            cursor: pointer;
            border-bottom: 2px solid transparent;
            color: var(--text-secondary);
        }
        .tab.active { border-bottom-color: var(--accent-color); color: var(--accent-color); }
        .tab-content { display: none; }
        .tab-content.active { display: block; }
    </style>
</head>
<body class=")HTML" << theme_class << R"HTML(">
    <header>
        <div class="container">
            <h1>)HTML" << escape_html(options.html_title) << R"HTML(</h1>
            <p style="color: var(--text-secondary); margin-top: 5px;">
                Generated by BHA v2.0 on )HTML" << format_timestamp(std::chrono::system_clock::now()) << R"HTML(
            </p>
        </div>
    </header>

    <div class="container">
        <!-- Summary Cards -->
        <div class="summary-grid">
            <div class="summary-card">
                <h3>Total Files</h3>
                <div class="value">)HTML" << analysis.files.size() << R"HTML(</div>
            </div>
            <div class="summary-card">
                <h3>Total Build Time</h3>
                <div class="value">)HTML" << std::fixed << std::setprecision(1)
                         << duration_to_ms(analysis.performance.total_build_time) / 1000.0 << R"HTML(</div>
                <div class="unit">seconds</div>
            </div>
            <div class="summary-card">
                <h3>Avg File Time</h3>
                <div class="value">)HTML" << std::fixed << std::setprecision(1)
                         << duration_to_ms(analysis.performance.avg_file_time) << R"HTML(</div>
                <div class="unit">seconds</div>
            </div>
            <div class="summary-card">
                <h3>Suggestions</h3>
                <div class="value">)HTML" << suggestions.size() << R"HTML(</div>
            </div>
        </div>

        <!-- Tabs -->
        <div class="tabs">
            <div class="tab active" onclick="showTab('files')">Files</div>
            <div class="tab" onclick="showTab('suggestions')">Suggestions</div>
            <div class="tab" onclick="showTab('dependencies')">Dependencies</div>
        </div>

        <!-- Files Tab -->
        <div id="files" class="tab-content active">
            <div class="section">
                <div class="controls">
                    <input type="text" id="file-search" placeholder="Search files..." onkeyup="filterFiles()">
                </div>
                <table id="files-table">
                    <thead>
                        <tr>
                            <th>File</th>
                            <th>Total Time</th>
                            <th>Frontend</th>
                            <th>Backend</th>
                            <th>Lines</th>
                            <th>Time Distribution</th>
                        </tr>
                    </thead>
                    <tbody>)HTML";

        auto sorted_files = analysis.files;
        std::ranges::sort(sorted_files,
                          [](const auto& a, const auto& b) { return a.compile_time > b.compile_time; });

        Duration max_time = sorted_files.empty() ? Duration::zero() : sorted_files[0].compile_time;

        for (const auto& file : sorted_files) {
            double time_ms = duration_to_ms(file.compile_time);
            double fe_ms = duration_to_ms(file.frontend_time);
            double be_ms = duration_to_ms(file.backend_time);
            double bar_width = max_time.count() > 0
                ? 100.0 * static_cast<double>(file.compile_time.count()) / static_cast<double>(max_time.count())
                : 0.0;

            stream << R"HTML(
                        <tr>
                            <td>)HTML" << escape_html(file.file.string()) << R"HTML(</td>
                            <td>)HTML" << std::fixed << std::setprecision(1) << time_ms << R"HTML( ms</td>
                            <td>)HTML" << std::fixed << std::setprecision(1) << fe_ms << R"HTML( ms</td>
                            <td>)HTML" << std::fixed << std::setprecision(1) << be_ms << R"HTML( ms</td>
                            <td>)HTML" << file.lines_of_code << R"HTML(</td>
                            <td><div class="time-bar" style="width: )HTML" << bar_width << R"HTML(%"></div></td>
                        </tr>)HTML";
        }

        stream << R"HTML(
                    </tbody>
                </table>
            </div>
        </div>

        <!-- Suggestions Tab -->
        <div id="suggestions" class="tab-content">
            <div class="section">)HTML";

        for (const auto& sugg : suggestions) {
            std::string badge_class = "badge-low";
            std::string priority_text = "Low";
            if (sugg.priority == Priority::High || sugg.priority == Priority::Critical) {
                badge_class = "badge-high";
                priority_text = "High";
            } else if (sugg.priority == Priority::Medium) {
                badge_class = "badge-medium";
                priority_text = "Medium";
            }

            stream << R"HTML(
                <div class="suggestion-card">
                    <div class="suggestion-header">
                        <span class="suggestion-title">)HTML" << escape_html(sugg.title) << R"HTML(</span>
                        <span class="suggestion-badge )HTML" << badge_class << R"HTML(">)HTML" << priority_text << R"HTML(</span>
                    </div>
                    <div class="suggestion-meta">
                        )HTML" << escape_html(sugg.target_file.path.string()) << R"HTML(:)HTML" << sugg.target_file.line_start << R"HTML( |
                        Confidence: )HTML" << std::fixed << std::setprecision(0) << (sugg.confidence * 100) << R"HTML(% |
                        Est. savings: )HTML" << std::fixed << std::setprecision(1) << duration_to_ms(sugg.estimated_savings) << R"HTML( ms
                    </div>
                    <p>)HTML" << escape_html(sugg.description) << R"HTML(</p>)HTML";

            if (!sugg.before_code.code.empty() || !sugg.after_code.code.empty()) {
                stream << R"HTML(
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 10px;">)HTML";
                if (!sugg.before_code.code.empty()) {
                    stream << R"HTML(
                        <div>
                            <div style="font-size: 0.75rem; color: var(--text-secondary); margin-bottom: 5px;">Before:</div>
                            <pre class="code-block">)HTML" << escape_html(sugg.before_code.code) << R"HTML(</pre>
                        </div>)HTML";
                }
                if (!sugg.after_code.code.empty()) {
                    stream << R"HTML(
                        <div>
                            <div style="font-size: 0.75rem; color: var(--text-secondary); margin-bottom: 5px;">After:</div>
                            <pre class="code-block">)HTML" << escape_html(sugg.after_code.code) << R"HTML(</pre>
                        </div>)HTML";
                }
                stream << R"HTML(
                    </div>)HTML";
            }

            stream << R"HTML(
                </div>)HTML";
        }

        stream << R"HTML(
            </div>
        </div>

        <!-- Dependencies Tab -->
        <div id="dependencies" class="tab-content">
            <div class="section">
                <h2>Dependency Graph</h2>
                <div id="graph-container"></div>
                <p style="margin-top: 10px; color: var(--text-secondary); font-size: 0.875rem;">
                    Circular dependencies: )HTML" << analysis.dependencies.circular_dependencies.size() << R"HTML( |
                    Unique headers: )HTML" << analysis.dependencies.unique_headers << R"HTML( |
                    Max depth: )HTML" << analysis.dependencies.max_include_depth << R"HTML(
                </p>
            </div>
        </div>
    </div>

    <script>
        // Tab switching
        function showTab(tabId) {
            document.querySelectorAll('.tab').forEach(function(t) { t.classList.remove('active'); });
            document.querySelectorAll('.tab-content').forEach(function(c) { c.classList.remove('active'); });
            var selector = '.tab[onclick="showTab(\'' + tabId + '\')"]';
            document.querySelector(selector).classList.add('active');
            document.getElementById(tabId).classList.add('active');
        }

        // File search
        function filterFiles() {
            var query = document.getElementById('file-search').value.toLowerCase();
            var rows = document.querySelectorAll('#files-table tbody tr');
            rows.forEach(function(row) {
                var text = row.textContent.toLowerCase();
                row.style.display = text.includes(query) ? '' : 'none';
            });
        }

        // Embedded analysis data
        var analysisData = )HTML" << json_result.value() << R"HTML(;

        // Simple dependency visualization (without D3.js for offline support)
        function renderDependencyGraph() {
            var container = document.getElementById('graph-container');
            if (!analysisData.dependencies || !analysisData.dependencies.graph) {
                container.innerHTML = '<p style="padding: 20px; text-align: center;">No dependency data available</p>';
                return;
            }

            var deps = analysisData.dependencies.graph.slice(0, 50);
            var html = '<div style="padding: 20px; overflow: auto; height: 100%;">';
            html += '<p style="margin-bottom: 15px; color: var(--text-secondary);">Showing top 50 files by dependencies</p>';

            deps.forEach(function(entry) {
                var filename = entry.file.split('/').pop();
                var count = entry.includes.length;
                html += '<div style="margin-bottom: 10px; padding: 10px; background: var(--bg-primary); border-radius: 4px;">';
                html += '<strong>' + filename + '</strong>';
                html += '<span style="color: var(--text-secondary);"> includes ' + count + ' file(s)</span>';
                html += '</div>';
            });

            html += '</div>';
            container.innerHTML = html;
        }

        // Initialize
        renderDependencyGraph();
    </script>
</body>
</html>)HTML";

        return Result<void, Error>::success();
    }

    Result<std::string, Error> HtmlExporter::export_to_string(
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options
    ) const {
        std::ostringstream ss;
        if (auto result = export_to_stream(ss, analysis, suggestions, options, nullptr); result.is_err()) {
            return Result<std::string, Error>::failure(result.error());
        }
        return Result<std::string, Error>::success(ss.str());
    }

    // =============================================================================
    // CSV Exporter
    // =============================================================================

    Result<void, Error> CsvExporter::export_to_file(
        const fs::path& path,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options,
        const ExportProgressCallback progress
    ) const {
        std::ofstream file(path);
        if (!file.is_open()) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, "Failed to open file for writing: " + path.string())
            );
        }
        return export_to_stream(file, analysis, suggestions, options, progress);
    }

    Result<void, Error> CsvExporter::export_to_stream(
        std::ostream& stream,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options,
        ExportProgressCallback
    ) const {
        stream << "# Files\n";
        stream << "Path,Total Time (ms),Frontend Time (ms),Backend Time (ms),Lines of Code,Include Count\n";

        for (const auto& file : analysis.files) {
            if (options.min_compile_time > Duration::zero() &&
                file.compile_time < options.min_compile_time) {
                continue;
                }

            stream << escape_csv(file.file.string()) << ","
                   << std::fixed << std::setprecision(3) << duration_to_ms(file.compile_time) << ","
                   << duration_to_ms(file.frontend_time) << ","
                   << duration_to_ms(file.backend_time) << ","
                   << file.lines_of_code << ","
                   << file.include_count << "\n";
        }

        if (options.include_suggestions && !suggestions.empty()) {
            stream << "\n# Suggestions\n";
            stream << "Type,Title,Target File,Line,Confidence,Priority,Estimated Savings (ms)\n";

            for (const auto& sugg : suggestions) {
                if (sugg.confidence < options.min_confidence) {
                    continue;
                }

                stream << static_cast<int>(sugg.type) << ","
                       << escape_csv(sugg.title) << ","
                       << escape_csv(sugg.target_file.path.string()) << ","
                       << sugg.target_file.line_start << ","
                       << std::fixed << std::setprecision(2) << sugg.confidence << ","
                       << static_cast<int>(sugg.priority) << ","
                       << duration_to_ms(sugg.estimated_savings) << "\n";
            }
        }

        return Result<void, Error>::success();
    }

    Result<std::string, Error> CsvExporter::export_to_string(
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options
    ) const {
        std::ostringstream ss;
        if (auto result = export_to_stream(ss, analysis, suggestions, options, nullptr); result.is_err()) {
            return Result<std::string, Error>::failure(result.error());
        }
        return Result<std::string, Error>::success(ss.str());
    }

    // =============================================================================
    // Markdown Exporter
    // =============================================================================

    Result<void, Error> MarkdownExporter::export_to_file(
        const fs::path& path,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options,
        const ExportProgressCallback progress
    ) const {
        std::ofstream file(path);
        if (!file.is_open()) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, "Failed to open file for writing: " + path.string())
            );
        }
        return export_to_stream(file, analysis, suggestions, options, progress);
    }

    Result<void, Error> MarkdownExporter::export_to_stream(
        std::ostream& stream,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options,
        ExportProgressCallback
    ) const {
        stream << "# Build Hotspot Analysis Report\n\n";
        stream << "_Generated by BHA v2.0 on " << format_timestamp(std::chrono::system_clock::now()) << "_\n\n";

        stream << "## Summary\n\n";
        stream << "| Metric | Value |\n";
        stream << "|--------|-------|\n";
        stream << "| Total Files | " << analysis.files.size() << " |\n";
        stream << "| Total Build Time | " << std::fixed << std::setprecision(2)
               << duration_to_ms(analysis.performance.total_build_time) / 1000.0 << " s |\n";
        stream << "| Avg File Time | " << duration_to_ms(analysis.performance.avg_file_time) << " ms |\n";
        stream << "| Parallelism Efficiency | " << std::setprecision(1) << (analysis.performance.parallelism_efficiency * 100.0) << "% |\n";
        stream << "| Suggestions | " << suggestions.size() << " |\n\n";

        if (options.include_file_details) {
            stream << "## Top Files by Compile Time\n\n";
            stream << "| File | Time (ms) | Frontend | Backend | LOC |\n";
            stream << "|------|-----------|----------|---------|-----|\n";

            auto sorted_files = analysis.files;
            std::ranges::sort(sorted_files,
                              [](const auto& a, const auto& b) { return a.compile_time > b.compile_time; });

            std::size_t count = 0;
            for (const auto& file : sorted_files) {
                if (options.max_files > 0 && count >= options.max_files) break;
                if (count >= 20) break;  // Default limit for Markdown

                stream << "| " << file.file.filename().string()
                       << " | " << std::fixed << std::setprecision(1) << duration_to_ms(file.compile_time)
                       << " | " << duration_to_ms(file.frontend_time)
                       << " | " << duration_to_ms(file.backend_time)
                       << " | " << file.lines_of_code << " |\n";
                count++;
            }
            stream << "\n";
        }

        if (options.include_suggestions && !suggestions.empty()) {
            stream << "## Optimization Suggestions\n\n";

            for (const auto& sugg : suggestions) {
                if (sugg.confidence < options.min_confidence) continue;

                std::string priority;
                switch (sugg.priority) {
                case Priority::Critical: priority = "CRITICAL"; break;
                case Priority::High: priority = "HIGH"; break;
                case Priority::Medium: priority = "MEDIUM"; break;
                case Priority::Low: priority = "LOW"; break;
                }

                stream << "### " << sugg.title << "\n\n";
                stream << "**Priority:** " << priority
                       << " | **Confidence:** " << std::fixed << std::setprecision(0)
                       << (sugg.confidence * 100) << "%"
                       << " | **Est. Savings:** " << std::setprecision(1)
                       << duration_to_ms(sugg.estimated_savings) << " ms\n\n";
                stream << "**File:** `" << sugg.target_file.path.string() << ":" << sugg.target_file.line_start << "`\n\n";
                stream << sugg.description << "\n\n";

                if (!sugg.before_code.code.empty()) {
                    stream << "**Before:**\n```cpp\n" << sugg.before_code.code << "\n```\n\n";
                }
                if (!sugg.after_code.code.empty()) {
                    stream << "**After:**\n```cpp\n" << sugg.after_code.code << "\n```\n\n";
                }

                stream << "---\n\n";
            }
        }

        if (options.include_dependencies) {
            stream << "## Dependency Analysis\n\n";
            stream << "- **Total Includes:** " << analysis.dependencies.total_includes << "\n";
            stream << "- **Unique Headers:** " << analysis.dependencies.unique_headers << "\n";
            stream << "- **Max Include Depth:** " << analysis.dependencies.max_include_depth << "\n";
            stream << "- **Circular Dependencies:** " << analysis.dependencies.circular_dependencies.size() << "\n\n";
        }

        return Result<void, Error>::success();
    }

    Result<std::string, Error> MarkdownExporter::export_to_string(
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const ExportOptions& options
    ) const {
        std::ostringstream ss;
        if (auto result = export_to_stream(ss, analysis, suggestions, options, nullptr); result.is_err()) {
            return Result<std::string, Error>::failure(result.error());
        }
        return Result<std::string, Error>::success(ss.str());
    }
} // namespace bha::exporters