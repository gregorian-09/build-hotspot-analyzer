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
#include <unordered_set>

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
            std::tm time_info{};
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
            for (const auto& [path, total_parse_time, inclusion_count, including_files, included_by, impact_score] : analysis.dependencies.headers) {
                json h;
                h["path"] = path.string();
                h["inclusion_count"] = inclusion_count;
                h["including_files"] = including_files;
                h["parse_time_ms"] = duration_to_ms(total_parse_time);
                h["impact_score"] = impact_score;
                h["included_by"] = included_by;
                headers_array.push_back(h);
            }
            deps["headers"] = headers_array;

            json nodes = json::array();
            json links = json::array();
            std::unordered_set<std::string> seen;

            // Source file nodes
            for (const auto& file : analysis.files) {
                if (std::string file_id = file.file.string(); seen.insert(file_id).second) {
                    nodes.push_back({
                        {"id", file_id},
                        {"type", "source"}
                    });
                }
            }

            // Header nodes
            for (const auto& hinfo : analysis.dependencies.headers) {
                if (std::string hdr_id = hinfo.path.string(); seen.insert(hdr_id).second) {
                    nodes.push_back({
                        {"id", hdr_id},
                        {"type", "header"}
                    });
                }
            }

            // Include links
            for (const auto& hinfo : analysis.dependencies.headers) {
                std::string hdr_id = hinfo.path.string();
                for (const auto& incl_by : hinfo.included_by) {
                    links.push_back({
                        {"source", incl_by},
                        {"target", hdr_id},
                        {"type", "include"}
                    });
                }
            }

            deps["graph"] = {
                {"nodes", nodes},
                {"links", links}
            };

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
        // Generate embedded JSON data for JavaScript
        ExportOptions json_opts = options;
        json_opts.pretty_print = false;

        JsonExporter json_exporter;
        auto json_result = json_exporter.export_to_string(analysis, suggestions, json_opts);
        if (json_result.is_err()) {
            return Result<void, Error>::failure(json_result.error());
        }

        std::string theme_class = options.html_dark_mode ? "dark-theme" : "light-theme";

        stream << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)HTML" << escape_html(options.html_title) << R"HTML(</title>
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <style>
        :root {
            --bg-primary: #0f0f23;
            --bg-secondary: #1a1a2e;
            --bg-tertiary: #16213e;
            --text-primary: #e8e8e8;
            --text-secondary: #a0a0a0;
            --text-muted: #6c757d;
            --border-color: #2d2d44;
            --accent-color: #00d4ff;
            --accent-hover: #00b8e6;
            --success-color: #10b981;
            --warning-color: #f59e0b;
            --danger-color: #ef4444;
            --shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
            --glow: 0 0 20px rgba(0, 212, 255, 0.3);
        }
        .light-theme {
            --bg-primary: #ffffff;
            --bg-secondary: #f8fafc;
            --bg-tertiary: #f1f5f9;
            --text-primary: #0f172a;
            --text-secondary: #475569;
            --text-muted: #94a3b8;
            --border-color: #e2e8f0;
            --accent-color: #3b82f6;
            --accent-hover: #2563eb;
            --shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
            --glow: 0 0 20px rgba(59, 130, 246, 0.2);
        }

        * { box-sizing: border-box; margin: 0; padding: 0; }

        body {
            font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg-primary);
            color: var(--text-primary);
            line-height: 1.6;
            transition: background 0.3s ease, color 0.3s ease;
        }

        .container { max-width: 1600px; margin: 0 auto; padding: 20px; }

        header {
            background: linear-gradient(135deg, var(--bg-secondary) 0%, var(--bg-tertiary) 100%);
            border-bottom: 2px solid var(--accent-color);
            padding: 30px 0;
            margin-bottom: 40px;
            box-shadow: var(--shadow);
        }

        h1 {
            font-size: 2.5rem;
            font-weight: 700;
            background: linear-gradient(135deg, var(--accent-color) 0%, var(--accent-hover) 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
            margin-bottom: 8px;
        }

        h2 {
            font-size: 1.75rem;
            margin-bottom: 20px;
            color: var(--text-primary);
            display: flex;
            align-items: center;
            gap: 10px;
        }

        h2 i { color: var(--accent-color); }

        .summary-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 24px;
            margin-bottom: 40px;
        }

        .summary-card {
            background: var(--bg-secondary);
            border: 1px solid var(--border-color);
            border-radius: 16px;
            padding: 24px;
            transition: all 0.3s ease;
            position: relative;
            overflow: hidden;
        }

        .summary-card::before {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            height: 4px;
            background: linear-gradient(90deg, var(--accent-color), var(--accent-hover));
            transform: scaleX(0);
            transition: transform 0.3s ease;
        }

        .summary-card:hover {
            transform: translateY(-4px);
            box-shadow: var(--glow);
            border-color: var(--accent-color);
        }

        .summary-card:hover::before {
            transform: scaleX(1);
        }

        .summary-card h3 {
            font-size: 0.875rem;
            color: var(--text-secondary);
            margin-bottom: 8px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        .summary-card .value {
            font-size: 2.5rem;
            font-weight: 700;
            color: var(--accent-color);
            line-height: 1;
        }

        .summary-card .unit {
            font-size: 0.875rem;
            color: var(--text-muted);
            margin-top: 4px;
        }

        .section {
            margin-bottom: 50px;
            background: var(--bg-secondary);
            border-radius: 16px;
            padding: 30px;
            border: 1px solid var(--border-color);
        }

        .tabs {
            display: flex;
            gap: 8px;
            border-bottom: 2px solid var(--border-color);
            margin-bottom: 30px;
            overflow-x: auto;
            padding-bottom: 0;
        }

        .tab {
            padding: 12px 24px;
            cursor: pointer;
            border: none;
            background: transparent;
            color: var(--text-secondary);
            font-size: 1rem;
            font-weight: 500;
            border-bottom: 3px solid transparent;
            transition: all 0.2s ease;
            white-space: nowrap;
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .tab:hover {
            color: var(--accent-color);
            background: var(--bg-tertiary);
        }

        .tab.active {
            color: var(--accent-color);
            border-bottom-color: var(--accent-color);
        }

        .tab-content { display: none; }
        .tab-content.active { display: block; }

        table {
            width: 100%;
            border-collapse: separate;
            border-spacing: 0;
            background: var(--bg-tertiary);
            border-radius: 12px;
            overflow: hidden;
            box-shadow: var(--shadow);
        }

        th, td {
            padding: 16px;
            text-align: left;
            border-bottom: 1px solid var(--border-color);
        }

        th {
            background: var(--bg-secondary);
            font-weight: 600;
            color: var(--text-primary);
            position: sticky;
            top: 0;
            z-index: 10;
        }

        tr:hover {
            background: rgba(0, 212, 255, 0.05);
        }

        tr:last-child td {
            border-bottom: none;
        }

        .time-bar-container {
            width: 100%;
            height: 8px;
            background: var(--border-color);
            border-radius: 4px;
            overflow: hidden;
        }

        .time-bar {
            height: 100%;
            background: linear-gradient(90deg, var(--accent-color), var(--accent-hover));
            border-radius: 4px;
            transition: width 0.3s ease;
        }

        .suggestion-card {
            background: var(--bg-tertiary);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            padding: 24px;
            margin-bottom: 20px;
            transition: all 0.3s ease;
        }

        .suggestion-card:hover {
            transform: translateX(4px);
            box-shadow: var(--shadow);
            border-color: var(--accent-color);
        }

        .suggestion-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 12px;
            flex-wrap: wrap;
            gap: 12px;
        }

        .suggestion-title {
            font-weight: 600;
            font-size: 1.2rem;
            color: var(--text-primary);
        }

        .suggestion-badge {
            display: inline-block;
            padding: 6px 12px;
            border-radius: 6px;
            font-size: 0.75rem;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        .badge-high { background: var(--danger-color); color: white; }
        .badge-medium { background: var(--warning-color); color: black; }
        .badge-low { background: var(--success-color); color: white; }

        .suggestion-meta {
            font-size: 0.875rem;
            color: var(--text-secondary);
            margin-bottom: 12px;
            display: flex;
            gap: 16px;
            flex-wrap: wrap;
        }

        .suggestion-meta-item {
            display: flex;
            align-items: center;
            gap: 6px;
        }

        .code-block {
            background: #1e1e1e;
            color: #d4d4d4;
            padding: 20px;
            border-radius: 8px;
            font-family: 'Fira Code', 'Monaco', 'Courier New', monospace;
            font-size: 0.875rem;
            overflow-x: auto;
            overflow-y: auto;
            max-height: 400px;
            margin-top: 12px;
            border: 1px solid rgba(0, 212, 255, 0.2);
            word-wrap: break-word;
            white-space: pre-wrap;
        }

        .controls {
            margin-bottom: 24px;
            display: flex;
            gap: 12px;
            flex-wrap: wrap;
            align-items: center;
        }

        input[type="text"], input[type="number"], select {
            padding: 12px 16px;
            border: 2px solid var(--border-color);
            border-radius: 8px;
            background: var(--bg-tertiary);
            color: var(--text-primary);
            transition: all 0.2s ease;
            font-size: 0.95rem;
        }

        input[type="text"] {
            flex: 1;
            min-width: 250px;
        }

        input[type="number"], select {
            min-width: 150px;
        }

        input[type="text"]:focus, input[type="number"]:focus, select:focus {
            outline: none;
            border-color: var(--accent-color);
            box-shadow: 0 0 0 3px rgba(0, 212, 255, 0.1);
        }

        select option {
            background: var(--bg-tertiary);
            color: var(--text-primary);
        }

        .btn {
            padding: 12px 20px;
            border: 2px solid var(--accent-color);
            border-radius: 8px;
            background: transparent;
            color: var(--accent-color);
            cursor: pointer;
            font-size: 0.95rem;
            font-weight: 500;
            transition: all 0.2s ease;
        }

        .btn:hover {
            background: var(--accent-color);
            color: white;
        }

        .btn-small {
            padding: 8px 16px;
            font-size: 0.875rem;
        }

        .info-badge {
            display: inline-block;
            padding: 8px 12px;
            background: var(--bg-tertiary);
            border: 1px solid var(--border-color);
            border-radius: 6px;
            font-size: 0.875rem;
            color: var(--text-secondary);
        }

        .info-badge strong {
            color: var(--accent-color);
        }

        #include-tree-container, #timeline-container, #treemap-container,
        #template-container, #dependency-graph-container {
            width: 100%;
            min-height: 500px;
            background: var(--bg-tertiary);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            overflow: hidden;
            position: relative;
        }

        .dep-stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }

        .dep-stat {
            background: var(--bg-tertiary);
            padding: 20px;
            border-radius: 12px;
            text-align: center;
            border: 1px solid var(--border-color);
            transition: all 0.3s ease;
        }

        .dep-stat:hover {
            transform: translateY(-2px);
            box-shadow: var(--shadow);
            border-color: var(--accent-color);
        }

        .dep-stat-value {
            font-size: 2rem;
            font-weight: bold;
            color: var(--accent-color);
            line-height: 1;
        }

        .dep-stat-label {
            font-size: 0.875rem;
            color: var(--text-secondary);
            margin-top: 8px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        .tooltip {
            position: absolute;
            padding: 12px 16px;
            background: rgba(0, 0, 0, 0.95);
            color: white;
            border-radius: 8px;
            pointer-events: none;
            font-size: 0.875rem;
            z-index: 1000;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.5);
            border: 1px solid var(--accent-color);
            max-width: 400px;
        }

        .node {
            cursor: pointer;
            transition: all 0.2s ease;
        }

        .node:hover {
            filter: brightness(1.3);
        }

        .link {
            stroke: var(--border-color);
            stroke-opacity: 0.6;
            fill: none;
            transition: all 0.2s ease;
        }

        .link:hover {
            stroke: var(--accent-color);
            stroke-opacity: 1;
            stroke-width: 2px;
        }

        .tree-node {
            cursor: pointer;
        }

        .tree-node circle {
            fill: var(--accent-color);
            stroke: var(--border-color);
            stroke-width: 2px;
        }

        .tree-node.header circle {
            fill: var(--warning-color);
        }

        .tree-node.source circle {
            fill: var(--success-color);
        }

        .tree-node text {
            font-size: 12px;
            fill: var(--text-primary);
        }

        .tree-link {
            fill: none;
            stroke: var(--border-color);
            stroke-width: 1.5px;
            stroke-opacity: 0.6;
        }

        .treemap-cell {
            stroke: var(--bg-primary);
            stroke-width: 2px;
            cursor: pointer;
            transition: opacity 0.2s;
        }

        .treemap-cell:hover {
            opacity: 0.8;
            stroke: var(--accent-color);
            stroke-width: 3px;
        }

        .treemap-label {
            font-size: 11px;
            fill: white;
            pointer-events: none;
            text-shadow: 1px 1px 2px rgba(0,0,0,0.8);
        }

        .timeline-bar {
            cursor: pointer;
            transition: opacity 0.2s;
        }

        .timeline-bar:hover {
            opacity: 0.8;
            stroke: var(--accent-color);
            stroke-width: 2px;
        }

        .zoom-controls {
            position: absolute;
            top: 20px;
            right: 20px;
            display: flex;
            gap: 8px;
            z-index: 100;
        }

        .zoom-btn {
            width: 36px;
            height: 36px;
            border-radius: 8px;
            border: 1px solid var(--border-color);
            background: var(--bg-secondary);
            color: var(--text-primary);
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: all 0.2s ease;
        }

        .zoom-btn:hover {
            background: var(--accent-color);
            color: white;
            border-color: var(--accent-color);
        }

        .legend {
            display: flex;
            gap: 20px;
            margin-bottom: 20px;
            flex-wrap: wrap;
        }

        .legend-item {
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 0.875rem;
        }

        .legend-color {
            width: 20px;
            height: 20px;
            border-radius: 4px;
            border: 1px solid var(--border-color);
        }

        .pagination {
            display: flex;
            gap: 8px;
            align-items: center;
            justify-content: center;
            margin-top: 20px;
        }

        .pagination button {
            padding: 8px 16px;
            border: 1px solid var(--border-color);
            background: var(--bg-tertiary);
            color: var(--text-primary);
            border-radius: 6px;
            cursor: pointer;
            transition: all 0.2s ease;
        }

        .pagination button:hover:not(:disabled) {
            background: var(--accent-color);
            color: white;
            border-color: var(--accent-color);
        }

        .pagination button:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }

        .pagination .page-info {
            padding: 8px 16px;
            color: var(--text-secondary);
        }

        .warning-banner {
            background: var(--warning-color);
            color: black;
            padding: 16px 20px;
            border-radius: 8px;
            margin-bottom: 20px;
            display: flex;
            align-items: center;
            gap: 12px;
            font-weight: 500;
        }

        @media (max-width: 768px) {
            .summary-grid { grid-template-columns: 1fr; }
            h1 { font-size: 1.75rem; }
            .tabs { flex-wrap: wrap; }
        }

        .loading {
            display: flex;
            align-items: center;
            justify-content: center;
            min-height: 400px;
            color: var(--text-secondary);
        }

        .spinner {
            border: 3px solid var(--border-color);
            border-top-color: var(--accent-color);
            border-radius: 50%;
            width: 40px;
            height: 40px;
            animation: spin 0.8s linear infinite;
        }

        @keyframes spin {
            to { transform: rotate(360deg); }
        }
    </style>
</head>
<body class=")HTML" << theme_class << R"HTML(">
    <header>
        <div class="container">
            <h1><i class="fas fa-chart-line"></i> )HTML" << escape_html(options.html_title) << R"HTML(</h1>
            <p style="color: var(--text-secondary); margin-top: 5px;">
                <i class="far fa-clock"></i> Generated on )HTML" << format_timestamp(std::chrono::system_clock::now()) << R"HTML(
            </p>
        </div>
    </header>

    <div class="container">
        <div class="summary-grid">
            <div class="summary-card">
                <h3><i class="fas fa-file-code"></i> Total Files</h3>
                <div class="value">)HTML" << analysis.files.size() << R"HTML(</div>
            </div>
            <div class="summary-card">
                <h3><i class="fas fa-stopwatch"></i> Total Build Time</h3>
                <div class="value">)HTML" << std::fixed << std::setprecision(1)
                             << duration_to_ms(analysis.performance.total_build_time) / 1000.0 << R"HTML(</div>
                <div class="unit">seconds</div>
            </div>
            <div class="summary-card">
                <h3><i class="fas fa-tachometer-alt"></i> Avg File Time</h3>
                <div class="value">)HTML" << std::fixed << std::setprecision(1)
                             << duration_to_ms(analysis.performance.avg_file_time) << R"HTML(</div>
                <div class="unit">ms</div>
            </div>
            <div class="summary-card">
                <h3><i class="fas fa-lightbulb"></i> Suggestions</h3>
                <div class="value">)HTML" << suggestions.size() << R"HTML(</div>
            </div>
        </div>

        <div class="tabs">
            <div class="tab active" onclick="showTab('files')">
                <i class="fas fa-list"></i> Files
            </div>
            <div class="tab" onclick="showTab('include-tree')">
                <i class="fas fa-sitemap"></i> Include Tree
            </div>
            <div class="tab" onclick="showTab('timeline')">
                <i class="fas fa-stream"></i> Timeline
            </div>
            <div class="tab" onclick="showTab('treemap')">
                <i class="fas fa-th"></i> Treemap
            </div>
            <div class="tab" onclick="showTab('templates')">
                <i class="fas fa-code"></i> Templates
            </div>
            <div class="tab" onclick="showTab('suggestions')">
                <i class="fas fa-magic"></i> Suggestions
            </div>
            <div class="tab" onclick="showTab('dependencies')">
                <i class="fas fa-project-diagram"></i> Dependencies
            </div>
        </div>

        <div id="files" class="tab-content active">
            <div class="section">
                <h2><i class="fas fa-folder-open"></i> File Analysis</h2>
                <div class="controls">
                    <input type="text" id="file-search" placeholder="🔍 Search files..." onkeyup="filterFiles()">
                    <select id="files-sort" onchange="sortFiles()">
                        <option value="time-desc">Time (High to Low)</option>
                        <option value="time-asc">Time (Low to High)</option>
                        <option value="name-asc">Name (A-Z)</option>
                        <option value="lines-desc">Lines (High to Low)</option>
                    </select>
                    <input type="number" id="files-limit" value="100" min="10" max="10000"
                           placeholder="Show top N" style="max-width: 150px;">
                    <button class="btn btn-small" onclick="applyFileLimit()">Apply</button>
                </div>
                <div class="info-badge" style="margin-bottom: 16px;">
                    <i class="fas fa-info-circle"></i>
                    Showing <strong id="files-shown">0</strong> of <strong id="files-total">0</strong> files
                </div>
                <div style="max-height: 600px; overflow-y: auto;">
                    <table id="files-table">
                        <thead>
                            <tr>
                                <th>File</th>
                                <th>Total Time</th>
                                <th>Frontend</th>
                                <th>Backend</th>
                                <th>Lines</th>
                                <th style="width: 200px;">Distribution</th>
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
                            <tr data-time=")HTML" << time_ms << R"HTML(" data-name=")HTML"
                                << escape_html(file.file.string()) << R"HTML(" data-lines=")HTML"
                                << file.lines_of_code << R"HTML(">
                                <td><i class="fas fa-file-code" style="color: var(--accent-color); margin-right: 8px;"></i>)HTML"
                                    << escape_html(file.file.string()) << R"HTML(</td>
                                <td><strong>)HTML" << std::fixed << std::setprecision(1) << time_ms << R"HTML( ms</strong></td>
                                <td>)HTML" << std::fixed << std::setprecision(1) << fe_ms << R"HTML( ms</td>
                                <td>)HTML" << std::fixed << std::setprecision(1) << be_ms << R"HTML( ms</td>
                                <td>)HTML" << file.lines_of_code << R"HTML(</td>
                                <td>
                                    <div class="time-bar-container">
                                        <div class="time-bar" style="width: )HTML" << bar_width << R"HTML(%"></div>
                                    </div>
                                </td>
                            </tr>)HTML";
        }

        stream << R"HTML(
                        </tbody>
                    </table>
                </div>
            </div>
        </div>

        <div id="include-tree" class="tab-content">
            <div class="section">
                <h2><i class="fas fa-sitemap"></i> Include Dependency Tree</h2>
                <div class="controls">
                    <input type="number" id="tree-depth" value="3" min="1" max="10"
                           placeholder="Max depth" style="max-width: 150px;">
                    <input type="number" id="tree-limit" value="50" min="10" max="500"
                           placeholder="Max nodes" style="max-width: 150px;">
                    <button class="btn btn-small" onclick="renderIncludeTree()">Refresh</button>
                </div>
                <p style="color: var(--text-secondary); margin-bottom: 20px;">
                    Click nodes to expand/collapse. Limited to top nodes by impact to prevent overload.
                    <span style="color: var(--success-color);">●</span> Source files,
                    <span style="color: var(--warning-color);">●</span> Header files
                </p>
                <div id="include-tree-container">
                    <div class="zoom-controls">
                        <button class="zoom-btn" onclick="resetIncludeTree()" title="Reset View">
                            <i class="fas fa-compress"></i>
                        </button>
                    </div>
                    <div class="loading">
                        <div class="spinner"></div>
                    </div>
                </div>
            </div>
        </div>

        <div id="timeline" class="tab-content">
            <div class="section">
                <h2><i class="fas fa-stream"></i> Compilation Timeline</h2>
                <div class="controls">
                    <input type="number" id="timeline-limit" value="100" min="10" max="1000"
                           placeholder="Max files" style="max-width: 150px;">
                    <select id="timeline-sort">
                        <option value="time">Sort by Time</option>
                        <option value="name">Sort by Name</option>
                    </select>
                    <button class="btn btn-small" onclick="renderTimeline()">Refresh</button>
                </div>
                <p style="color: var(--text-secondary); margin-bottom: 20px;">
                    Shows top N slowest files. Bar length = compile time. Hover for details.
                </p>
                <div class="legend">
                    <div class="legend-item">
                        <div class="legend-color" style="background: var(--accent-color);"></div>
                        <span>Frontend</span>
                    </div>
                    <div class="legend-item">
                        <div class="legend-color" style="background: var(--warning-color);"></div>
                        <span>Backend</span>
                    </div>
                </div>
                <div id="timeline-container">
                    <div class="loading">
                        <div class="spinner"></div>
                    </div>
                </div>
            </div>
        </div>

        <div id="treemap" class="tab-content">
            <div class="section">
                <h2><i class="fas fa-th"></i> File Size vs Compile Time</h2>
                <div class="controls">
                    <input type="number" id="treemap-limit" value="100" min="10" max="500"
                           placeholder="Max files" style="max-width: 150px;">
                    <select id="treemap-metric">
                        <option value="lines">Size by Lines</option>
                        <option value="time">Size by Time</option>
                    </select>
                    <button class="btn btn-small" onclick="renderTreemap()">Refresh</button>
                </div>
                <p style="color: var(--text-secondary); margin-bottom: 20px;">
                    Rectangle size = lines/time (configurable). Color intensity = compile time.
                    Limited to top N files for readability.
                </p>
                <div id="treemap-container">
                    <div class="loading">
                        <div class="spinner"></div>
                    </div>
                </div>
            </div>
        </div>

        <div id="templates" class="tab-content">
            <div class="section">
                <h2><i class="fas fa-code"></i> Template Instantiation Analysis</h2>
                <div class="controls">
                    <input type="number" id="template-limit" value="50" min="5" max="200"
                           placeholder="Max templates" style="max-width: 150px;">
                    <button class="btn btn-small" onclick="renderTemplates()">Refresh</button>
                </div>
                <p style="color: var(--text-secondary); margin-bottom: 20px;">
                    Click segments to see details. Shows top N most expensive templates.
                </p>
                <div id="template-container">
                    <div class="loading">
                        <div class="spinner"></div>
                    </div>
                </div>
            </div>
        </div>

        <div id="suggestions" class="tab-content">
            <div class="section">
                <h2><i class="fas fa-magic"></i> Optimization Suggestions</h2>)HTML";

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
                        <span class="suggestion-meta-item">
                            <i class="fas fa-map-marker-alt"></i>
                            )HTML" << escape_html(sugg.target_file.path.string()) << R"HTML(:)HTML" << sugg.target_file.line_start << R"HTML(
                        </span>
                        <span class="suggestion-meta-item">
                            <i class="fas fa-percentage"></i>
                            Confidence: )HTML" << std::fixed << std::setprecision(0) << (sugg.confidence * 100) << R"HTML(%
                        </span>
                        <span class="suggestion-meta-item">
                            <i class="fas fa-clock"></i>
                            Est. savings: )HTML" << std::fixed << std::setprecision(1) << duration_to_ms(sugg.estimated_savings) << R"HTML( ms
                        </span>
                    </div>
                    <p>)HTML" << escape_html(sugg.description) << R"HTML(</p>)HTML";

            if (!sugg.before_code.code.empty() || !sugg.after_code.code.empty()) {
                stream << R"HTML(
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px; margin-top: 16px; overflow: hidden;">)HTML";
                if (!sugg.before_code.code.empty()) {
                    stream << R"HTML(
                        <div style="min-width: 0;">
                            <div style="font-size: 0.875rem; color: var(--text-secondary); margin-bottom: 8px; font-weight: 600;">
                                <i class="fas fa-times-circle" style="color: var(--danger-color);"></i> Before:
                            </div>
                            <pre class="code-block">)HTML" << escape_html(sugg.before_code.code) << R"HTML(</pre>
                        </div>)HTML";
                }
                if (!sugg.after_code.code.empty()) {
                    stream << R"HTML(
                        <div style="min-width: 0;">
                            <div style="font-size: 0.875rem; color: var(--text-secondary); margin-bottom: 8px; font-weight: 600;">
                                <i class="fas fa-check-circle" style="color: var(--success-color);"></i> After:
                            </div>
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

        <div id="dependencies" class="tab-content">
            <div class="section">
                <h2><i class="fas fa-project-diagram"></i> Dependency Analysis</h2>

                <div class="dep-stats">
                    <div class="dep-stat">
                        <div class="dep-stat-value">)HTML" << analysis.dependencies.total_includes << R"HTML(</div>
                        <div class="dep-stat-label">Total Includes</div>
                    </div>
                    <div class="dep-stat">
                        <div class="dep-stat-value">)HTML" << analysis.dependencies.unique_headers << R"HTML(</div>
                        <div class="dep-stat-label">Unique Headers</div>
                    </div>
                    <div class="dep-stat">
                        <div class="dep-stat-value">)HTML" << analysis.dependencies.max_include_depth << R"HTML(</div>
                        <div class="dep-stat-label">Max Depth</div>
                    </div>
                    <div class="dep-stat">
                        <div class="dep-stat-value" style="color: )HTML"
                            << (analysis.dependencies.circular_dependencies.empty() ? "var(--success-color)" : "var(--danger-color)")
                            << R"HTML(">)HTML" << analysis.dependencies.circular_dependencies.size() << R"HTML(</div>
                        <div class="dep-stat-label">Circular Deps</div>
                    </div>
                </div>

                <h3 style="margin: 30px 0 16px; color: var(--text-primary);">
                    <i class="fas fa-network-wired"></i> Dependency Graph
                </h3>
                <div class="controls">
                    <input type="number" id="dep-limit" value="50" min="10" max="200"
                           placeholder="Max nodes" style="max-width: 150px;">
                    <button class="btn btn-small" onclick="renderDependencyGraph()">Refresh</button>
                </div>
                <p style="color: var(--text-secondary); margin-bottom: 16px;">
                    Drag nodes to rearrange. Scroll to zoom. Limited to most connected nodes.
                    Blue = headers, Green = source files.
                </p>
                <div id="dependency-graph-container">
                    <div class="zoom-controls">
                        <button class="zoom-btn" onclick="resetZoom()" title="Reset View">
                            <i class="fas fa-compress"></i>
                        </button>
                    </div>
                    <svg id="dependency-graph"></svg>
                </div>
            </div>
        </div>
    </div>

    <script src="https://d3js.org/d3.v7.min.js"></script>
    <script>
    (function() {
        var analysisData = )HTML" << json_result.value() << R"HTML(;

        let currentTransform = d3.zoomIdentity;
        let graphSimulation = null;
        let allFilesData = [];

        // Initialize
        document.addEventListener('DOMContentLoaded', function() {
            allFilesData = (analysisData.files || []).slice();
            updateFileStats();
            applyFileLimit();
        });

        function showTab(tabId) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            document.querySelector('.tab[onclick*="' + tabId + '"]').classList.add('active');
            document.getElementById(tabId).classList.add('active');

            if (tabId === 'include-tree') renderIncludeTree();
            if (tabId === 'timeline') renderTimeline();
            if (tabId === 'treemap') renderTreemap();
            if (tabId === 'templates') renderTemplates();
            if (tabId === 'dependencies') renderDependencyGraph();
        }

        function updateFileStats() {
            const tbody = document.querySelector('#files-table tbody');
            const rows = tbody.querySelectorAll('tr');
            const visibleRows = Array.from(rows).filter(r => r.style.display !== 'none');

            document.getElementById('files-shown').textContent = visibleRows.length;
            document.getElementById('files-total').textContent = rows.length;
        }

        function filterFiles() {
            const query = document.getElementById('file-search').value.toLowerCase();
            const tbody = document.querySelector('#files-table tbody');
            const rows = tbody.querySelectorAll('tr');

            rows.forEach(row => {
                row.style.display = row.textContent.toLowerCase().includes(query) ? '' : 'none';
            });
            updateFileStats();
        }

        function sortFiles() {
            const sortBy = document.getElementById('files-sort').value;
            const tbody = document.querySelector('#files-table tbody');
            const rows = Array.from(tbody.querySelectorAll('tr'));

            rows.sort((a, b) => {
                switch(sortBy) {
                    case 'time-desc':
                        return parseFloat(b.dataset.time) - parseFloat(a.dataset.time);
                    case 'time-asc':
                        return parseFloat(a.dataset.time) - parseFloat(b.dataset.time);
                    case 'name-asc':
                        return a.dataset.name.localeCompare(b.dataset.name);
                    case 'lines-desc':
                        return parseInt(b.dataset.lines) - parseInt(a.dataset.lines);
                    default:
                        return 0;
                }
            });

            rows.forEach(row => tbody.appendChild(row));
            updateFileStats();
        }

        function applyFileLimit() {
            const limit = parseInt(document.getElementById('files-limit').value) || 100;
            const tbody = document.querySelector('#files-table tbody');
            const rows = Array.from(tbody.querySelectorAll('tr'));

            rows.forEach((row, idx) => {
                row.style.display = idx < limit ? '' : 'none';
            });
            updateFileStats();
        }

        function renderIncludeTree() {
            const container = d3.select('#include-tree-container');
            container.selectAll('*').remove();

            if (!analysisData.dependencies || !analysisData.dependencies.graph) {
                container.html('<div class="loading"><p>No dependency data available</p></div>');
                return;
            }

            const maxDepth = parseInt(document.getElementById('tree-depth').value) || 3;
            const maxNodes = parseInt(document.getElementById('tree-limit').value) || 50;

            const width = container.node().getBoundingClientRect().width;
            const height = 800;

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            const g = svg.append('g');

            const zoom = d3.zoom()
                .scaleExtent([0.1, 4])
                .on('zoom', (event) => {
                    g.attr('transform', event.transform);
                });

            svg.call(zoom);

            // Build tree from graph data with limits
            const graph = analysisData.dependencies.graph;
            const allNodes = graph.nodes || [];
            const allLinks = graph.links || [];

            if (!allNodes.length) {
                container.html('<div class="loading"><p>No nodes available</p></div>');
                return;
            }

            // Create node map for all nodes
            const allNodeMap = new Map(allNodes.map(n => [n.id, {...n, children: []}]));

            // Build parent-child relationships
            const hasIncoming = new Set();
            allLinks.forEach(link => {
                const sourceId = typeof link.source === 'object' ? link.source.id : link.source;
                const targetId = typeof link.target === 'object' ? link.target.id : link.target;

                if (allNodeMap.has(sourceId) && allNodeMap.has(targetId)) {
                    hasIncoming.add(targetId);
                    const source = allNodeMap.get(sourceId);
                    const target = allNodeMap.get(targetId);
                    if (source && target && !source.children.find(c => c.id === target.id)) {
                        source.children.push(target);
                    }
                }
            });

            // Find root nodes (nodes with no incoming edges) - typically source files
            let roots = allNodes.filter(n => !hasIncoming.has(n.id));

            if (roots.length === 0) {
                // If no roots found (circular deps), pick nodes with most outgoing connections
                const outgoingCount = new Map();
                allLinks.forEach(l => {
                    const sourceId = typeof l.source === 'object' ? l.source.id : l.source;
                    outgoingCount.set(sourceId, (outgoingCount.get(sourceId) || 0) + 1);
                });
                roots = allNodes
                    .slice()
                    .sort((a, b) => (outgoingCount.get(b.id) || 0) - (outgoingCount.get(a.id) || 0))
                    .slice(0, 5);
            } else {
                // Limit roots to prevent overcrowding
                roots = roots.slice(0, Math.min(5, Math.ceil(maxNodes / 10)));
            }

            if (roots.length === 0) {
                container.html('<div class="loading"><p>No root nodes found</p></div>');
                return;
            }

            // Helper function to count descendants
            function countDescendants(node, depth = 0) {
                if (depth >= maxDepth || !node.children || node.children.length === 0) {
                    return 1;
                }
                return 1 + node.children.reduce((sum, child) => sum + countDescendants(child, depth + 1), 0);
            }

            // Helper function to prune tree to maxNodes
            function pruneTree(node, remainingNodes, depth = 0) {
                if (remainingNodes <= 0 || depth >= maxDepth) {
                    node._children = node.children;
                    node.children = null;
                    return 1;
                }

                if (!node.children || node.children.length === 0) {
                    return 1;
                }

                let used = 1;
                const keptChildren = [];

                for (const child of node.children) {
                    if (used >= remainingNodes) {
                        break;
                    }
                    const childCount = pruneTree(child, remainingNodes - used, depth + 1);
                    used += childCount;
                    keptChildren.push(child);
                }

                if (keptChildren.length < node.children.length) {
                    node._children = node.children.slice(keptChildren.length);
                }
                node.children = keptChildren;

                return used;
            }

            // Create tree layout for each root
            const tree = d3.tree().size([height - 100, width - 200]);

            let yOffset = 50;
            let nodesPerRoot = Math.floor(maxNodes / roots.length);

            roots.forEach((root, idx) => {
                const rootNode = allNodeMap.get(root.id);
                if (!rootNode) return;

                // Prune to fit within maxNodes budget
                pruneTree(rootNode, nodesPerRoot, 0);

                const hierarchy = d3.hierarchy(rootNode);
                const treeData = tree(hierarchy);

                const treeG = g.append('g')
                    .attr('transform', `translate(100, ${yOffset})`);

                // Links
                treeG.selectAll('.tree-link')
                    .data(treeData.links())
                    .join('path')
                    .attr('class', 'tree-link')
                    .attr('d', d3.linkHorizontal()
                        .x(d => d.y)
                        .y(d => d.x));

                // Nodes
                const node = treeG.selectAll('.tree-node')
                    .data(treeData.descendants())
                    .join('g')
                    .attr('class', d => `tree-node ${d.data.type || 'header'}`)
                    .attr('transform', d => `translate(${d.y},${d.x})`)
                    .on('click', function(event, d) {
                        if (d._children) {
                            d.children = d._children;
                            d._children = null;
                        } else if (d.children) {
                            d._children = d.children;
                            d.children = null;
                        }
                        renderIncludeTree();
                    })
                    .on('mouseover', (event, d) => {
                        showTooltip(event, {
                            name: d.data.id.split('/').pop().split('\\\\').pop(),
                            fullPath: d.data.id,
                            type: d.data.type,
                            depth: d.depth,
                            children: (d.children || []).length + (d._children || []).length
                        });
                    })
                    .on('mouseout', hideTooltip);

                node.append('circle')
                    .attr('r', d => {
                        if (d.children && d.children.length > 0) return 6;
                        if (d._children && d._children.length > 0) return 5;
                        return 4;
                    })
                    .style('fill', d => {
                        if (d._children) return 'var(--text-muted)'; // Has collapsed children
                        return null; // Use CSS default
                    });

                node.append('text')
                    .attr('dx', 10)
                    .attr('dy', 4)
                    .text(d => {
                        const name = d.data.id.split('/').pop().split('\\\\').pop();
                        const shortName = name.length > 30 ? name.substring(0, 27) + '...' : name;
                        const childIndicator = d._children ? ` (+${d._children.length})` : '';
                        return shortName + childIndicator;
                    });

                yOffset += Math.max(treeData.height * 80 + 100, 150);
            });

            svg.call(zoom.transform, d3.zoomIdentity);
        }

        function resetIncludeTree() {
            renderIncludeTree();
        }

        function renderTimeline() {
            const container = d3.select('#timeline-container');
            container.selectAll('*').remove();

            if (!analysisData.files || !analysisData.files.length) {
                container.html('<div class="loading"><p>No file data available</p></div>');
                return;
            }

            const limit = parseInt(document.getElementById('timeline-limit').value) || 100;
            const sortBy = document.getElementById('timeline-sort').value;

            let files = analysisData.files.slice();

            if (sortBy === 'time') {
                files.sort((a,b) => b.total_time_ms - a.total_time_ms);
            } else {
                files.sort((a,b) => a.path.localeCompare(b.path));
            }

            files = files.slice(0, limit);

            const width = container.node().getBoundingClientRect().width;
            const barHeight = 20;
            const padding = 2;
            const height = Math.min(files.length * (barHeight + padding) + 60, 2000);

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            const maxTime = d3.max(files, d => d.total_time_ms);
            const xScale = d3.scaleLinear()
                .domain([0, maxTime])
                .range([200, width - 40]);

            const g = svg.append('g')
                .attr('transform', 'translate(0, 30)');

            // Axis
            g.append('g')
                .attr('transform', `translate(0, -10)`)
                .call(d3.axisTop(xScale).ticks(10).tickFormat(d => d + 'ms'))
                .selectAll('text')
                .style('fill', 'var(--text-secondary)');

            // Bars
            files.forEach((file, i) => {
                const y = i * (barHeight + padding);
                const fileName = file.path.split('/').pop().split('\\\\').pop();

                // Frontend bar
                g.append('rect')
                    .attr('class', 'timeline-bar')
                    .attr('x', 200)
                    .attr('y', y)
                    .attr('width', Math.max(1, xScale(file.frontend_time_ms) - 200))
                    .attr('height', barHeight)
                    .attr('fill', 'var(--accent-color)')
                    .on('mouseover', (event) => {
                        showTooltip(event, {
                            name: fileName,
                            fullPath: file.path,
                            time: file.frontend_time_ms,
                            phase: 'Frontend'
                        });
                    })
                    .on('mouseout', hideTooltip);

                // Backend bar
                g.append('rect')
                    .attr('class', 'timeline-bar')
                    .attr('x', xScale(file.frontend_time_ms))
                    .attr('y', y)
                    .attr('width', Math.max(1, xScale(file.total_time_ms) - xScale(file.frontend_time_ms)))
                    .attr('height', barHeight)
                    .attr('fill', 'var(--warning-color)')
                    .on('mouseover', (event) => {
                        showTooltip(event, {
                            name: fileName,
                            fullPath: file.path,
                            time: file.backend_time_ms,
                            phase: 'Backend'
                        });
                    })
                    .on('mouseout', hideTooltip);

                // Label
                g.append('text')
                    .attr('x', 195)
                    .attr('y', y + barHeight / 2)
                    .attr('dy', '0.35em')
                    .attr('text-anchor', 'end')
                    .style('font-size', '11px')
                    .style('fill', 'var(--text-primary)')
                    .text(fileName.length > 25 ? fileName.substring(0, 22) + '...' : fileName);
            });
        }

        function renderTreemap() {
            const container = d3.select('#treemap-container');
            container.selectAll('*').remove();

            if (!analysisData.files || !analysisData.files.length) {
                container.html('<div class="loading"><p>No file data available</p></div>');
                return;
            }

            const limit = parseInt(document.getElementById('treemap-limit').value) || 100;
            const metric = document.getElementById('treemap-metric').value;

            const width = container.node().getBoundingClientRect().width;
            const height = 600;

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            // Get top N files by time
            let files = analysisData.files.slice()
                .sort((a, b) => b.total_time_ms - a.total_time_ms)
                .slice(0, limit);

            // Prepare data
            const root = {
                name: 'root',
                children: files.map(f => ({
                    name: f.path.split('/').pop().split('\\\\').pop(),
                    fullPath: f.path,
                    value: metric === 'lines' ? (f.lines_of_code || 1) : f.total_time_ms,
                    time: f.total_time_ms,
                    lines: f.lines_of_code
                }))
            };

            const hierarchy = d3.hierarchy(root)
                .sum(d => d.value)
                .sort((a, b) => b.value - a.value);

            const treemap = d3.treemap()
                .size([width, height])
                .padding(2)
                .round(true);

            treemap(hierarchy);

            const maxTime = d3.max(files, f => f.total_time_ms);
            const colorScale = d3.scaleSequential(d3.interpolateRdYlGn)
                .domain([maxTime, 0]);

            const cell = svg.selectAll('g')
                .data(hierarchy.leaves())
                .join('g')
                .attr('transform', d => `translate(${d.x0},${d.y0})`);

            cell.append('rect')
                .attr('class', 'treemap-cell')
                .attr('width', d => d.x1 - d.x0)
                .attr('height', d => d.y1 - d.y0)
                .attr('fill', d => colorScale(d.data.time))
                .on('mouseover', (event, d) => {
                    showTooltip(event, {
                        name: d.data.name,
                        fullPath: d.data.fullPath,
                        time: d.data.time,
                        lines: d.data.lines
                    });
                })
                .on('mouseout', hideTooltip);

            cell.append('text')
                .attr('class', 'treemap-label')
                .attr('x', 4)
                .attr('y', 16)
                .text(d => {
                    const width = d.x1 - d.x0;
                    if (width < 60) return '';
                    const name = d.data.name;
                    return name.length > width / 7 ? name.substring(0, Math.floor(width / 7)) + '...' : name;
                });
        }

        function renderTemplates() {
            const container = d3.select('#template-container');
            container.selectAll('*').remove();

            if (!analysisData.templates || !analysisData.templates.templates ||
                !analysisData.templates.templates.length) {
                container.html('<div class="loading"><p>No template data available</p></div>');
                return;
            }

            const limit = parseInt(document.getElementById('template-limit').value) || 50;

            const width = container.node().getBoundingClientRect().width;
            const height = 600;
            const radius = Math.min(width, height) / 2 - 40;

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            const g = svg.append('g')
                .attr('transform', `translate(${width / 2},${height / 2})`);

            // Get top N templates
            const templates = analysisData.templates.templates
                .slice()
                .sort((a, b) => b.time_ms - a.time_ms)
                .slice(0, limit);

            const root = {
                name: 'Templates',
                children: templates.map(t => ({
                    name: t.name.length > 40 ? t.name.substring(0, 37) + '...' : t.name,
                    fullName: t.name,
                    value: t.time_ms,
                    count: t.count,
                    percentage: t.time_percent
                }))
            };

            const hierarchy = d3.hierarchy(root)
                .sum(d => d.value)
                .sort((a, b) => b.value - a.value);

            const partition = d3.partition()
                .size([2 * Math.PI, radius]);

            partition(hierarchy);

            const arc = d3.arc()
                .startAngle(d => d.x0)
                .endAngle(d => d.x1)
                .innerRadius(d => d.y0)
                .outerRadius(d => d.y1);

            const color = d3.scaleOrdinal(d3.schemeCategory10);

            g.selectAll('path')
                .data(hierarchy.descendants().filter(d => d.depth > 0))
                .join('path')
                .attr('d', arc)
                .attr('fill', d => color(d.data.name))
                .attr('stroke', 'var(--bg-primary)')
                .attr('stroke-width', 2)
                .style('cursor', 'pointer')
                .style('opacity', 0.8)
                .on('mouseover', function(event, d) {
                    d3.select(this).style('opacity', 1);
                    showTooltip(event, {
                        name: d.data.fullName || d.data.name,
                        time: d.data.value,
                        count: d.data.count,
                        percentage: d.data.percentage
                    });
                })
                .on('mouseout', function() {
                    d3.select(this).style('opacity', 0.8);
                    hideTooltip();
                });

            // Center label
            g.append('text')
                .attr('text-anchor', 'middle')
                .attr('dy', '0.35em')
                .style('font-size', '16px')
                .style('font-weight', 'bold')
                .style('fill', 'var(--text-primary)')
                .text(`Top ${templates.length} Templates`);
        }

        function renderDependencyGraph() {
            const container = d3.select('#dependency-graph-container');
            const svg = d3.select('#dependency-graph');
            svg.selectAll('*').remove();

            if (!analysisData.dependencies || !analysisData.dependencies.graph) {
                svg.append('text')
                   .attr('x', 20).attr('y', 30)
                   .text('No dependency graph data available')
                   .style('fill', 'var(--text-secondary)');
                return;
            }

            const limit = parseInt(document.getElementById('dep-limit').value) || 50;

            const graph = analysisData.dependencies.graph;

            if (!graph.nodes || !graph.nodes.length) {
                svg.append('text')
                   .attr('x', 20).attr('y', 30)
                   .text('No nodes in dependency graph')
                   .style('fill', 'var(--text-secondary)');
                return;
            }

            // Calculate node importance (number of connections)
            const nodeConnections = new Map();
            graph.nodes.forEach(n => nodeConnections.set(n.id, 0));
            (graph.links || []).forEach(l => {
                const sourceId = typeof l.source === 'object' ? l.source.id : l.source;
                const targetId = typeof l.target === 'object' ? l.target.id : l.target;
                nodeConnections.set(sourceId, (nodeConnections.get(sourceId) || 0) + 1);
                nodeConnections.set(targetId, (nodeConnections.get(targetId) || 0) + 1);
            });

            // Separate sources and headers
            const sourceNodes = graph.nodes.filter(n => n.type === 'source');
            const headerNodes = graph.nodes.filter(n => n.type === 'header');

            // Get top nodes from each category to ensure variety
            const halfLimit = Math.floor(limit / 2);
            const topSources = sourceNodes
                .slice()
                .sort((a, b) => (nodeConnections.get(b.id) || 0) - (nodeConnections.get(a.id) || 0))
                .slice(0, Math.min(halfLimit, sourceNodes.length));

            const topHeaders = headerNodes
                .slice()
                .sort((a, b) => (nodeConnections.get(b.id) || 0) - (nodeConnections.get(a.id) || 0))
                .slice(0, Math.min(limit - topSources.length, headerNodes.length));

            const topNodes = [...topSources, ...topHeaders];
            const topNodeIds = new Set(topNodes.map(n => n.id));

            let nodes = topNodes.map(d => ({...d}));
            let links = (graph.links || [])
                .filter(l => {
                    const sourceId = typeof l.source === 'object' ? l.source.id : l.source;
                    const targetId = typeof l.target === 'object' ? l.target.id : l.target;
                    return topNodeIds.has(sourceId) && topNodeIds.has(targetId);
                })
                .map(d => ({
                    source: typeof d.source === 'object' ? d.source.id : d.source,
                    target: typeof d.target === 'object' ? d.target.id : d.target,
                    type: d.type
                }));

            if (!nodes.length) {
                svg.append('text')
                   .attr('x', 20).attr('y', 30)
                   .text('No nodes to display')
                   .style('fill', 'var(--text-secondary)');
                return;
            }

            const width = container.node().getBoundingClientRect().width;
            const height = 600;

            svg.attr('width', width).attr('height', height);

            const g = svg.append('g');

            const zoom = d3.zoom()
                .scaleExtent([0.1, 10])
                .on('zoom', (event) => {
                    g.attr('transform', event.transform);
                    currentTransform = event.transform;
                });

            svg.call(zoom);

            const simulation = d3.forceSimulation(nodes)
                .force('link', d3.forceLink(links)
                    .id(d => d.id)
                    .distance(100)
                    .strength(0.5))
                .force('charge', d3.forceManyBody()
                    .strength(-300)
                    .distanceMax(400))
                .force('center', d3.forceCenter(width / 2, height / 2))
                .force('collision', d3.forceCollide().radius(30));

            graphSimulation = simulation;

            const defs = svg.append('defs');

            defs.append('marker')
                .attr('id', 'arrowhead')
                .attr('viewBox', '0 -5 10 10')
                .attr('refX', 20)
                .attr('refY', 0)
                .attr('markerWidth', 6)
                .attr('markerHeight', 6)
                .attr('orient', 'auto')
                .append('path')
                .attr('d', 'M0,-5L10,0L0,5')
                .attr('fill', 'var(--border-color)');

            const link = g.append('g')
                .selectAll('path')
                .data(links)
                .join('path')
                .attr('class', 'link')
                .attr('stroke', 'var(--border-color)')
                .attr('stroke-width', 1.5)
                .attr('fill', 'none')
                .attr('marker-end', 'url(#arrowhead)')
                .on('mouseover', function() {
                    d3.select(this)
                        .attr('stroke', 'var(--accent-color)')
                        .attr('stroke-width', 2.5);
                })
                .on('mouseout', function() {
                    d3.select(this)
                        .attr('stroke', 'var(--border-color)')
                        .attr('stroke-width', 1.5);
                });

            const node = g.append('g')
                .selectAll('g')
                .data(nodes)
                .join('g')
                .attr('class', 'node')
                .call(d3.drag()
                    .on('start', dragstarted)
                    .on('drag', dragged)
                    .on('end', dragended));

            node.append('circle')
                .attr('r', d => d.type === 'source' ? 10 : 7)
                .attr('fill', d => d.type === 'source' ? 'var(--success-color)' : 'var(--accent-color)')
                .attr('stroke', '#fff')
                .attr('stroke-width', 2)
                .on('mouseover', function(event, d) {
                    showTooltip(event, {
                        name: d.id.split('/').pop().split('\\\\').pop(),
                        fullPath: d.id,
                        type: d.type,
                        connections: nodeConnections.get(d.id) || 0
                    });
                    d3.select(this)
                        .attr('r', d.type === 'source' ? 14 : 11)
                        .style('filter', 'brightness(1.5)');
                })
                .on('mouseout', function(event, d) {
                    hideTooltip();
                    d3.select(this)
                        .attr('r', d.type === 'source' ? 10 : 7)
                        .style('filter', 'brightness(1)');
                });

            node.append('text')
                .text(d => {
                    const name = d.id.split('/').pop().split('\\\\').pop();
                    return name.length > 20 ? name.substring(0, 17) + '...' : name;
                })
                .attr('x', 12)
                .attr('y', 4)
                .style('font-size', '10px')
                .style('fill', 'var(--text-primary)')
                .style('pointer-events', 'none');

            simulation.on('tick', () => {
                link.attr('d', d => {
                    const dx = d.target.x - d.source.x;
                    const dy = d.target.y - d.source.y;
                    const dr = Math.sqrt(dx * dx + dy * dy);
                    return `M${d.source.x},${d.source.y}A${dr},${dr} 0 0,1 ${d.target.x},${d.target.y}`;
                });

                node.attr('transform', d => `translate(${d.x},${d.y})`);
            });

            function dragstarted(event, d) {
                if (!event.active) simulation.alphaTarget(0.3).restart();
                d.fx = d.x;
                d.fy = d.y;
            }

            function dragged(event, d) {
                d.fx = event.x;
                d.fy = event.y;
            }

            function dragended(event, d) {
                if (!event.active) simulation.alphaTarget(0);
                d.fx = null;
                d.fy = null;
            }

            svg.call(zoom.transform, d3.zoomIdentity.translate(width / 2, height / 2).scale(0.8).translate(-width / 2, -height / 2));
        }

        function resetZoom() {
            const svg = d3.select('#dependency-graph');
            const container = d3.select('#dependency-graph-container');
            const width = container.node().getBoundingClientRect().width;
            const height = 600;

            svg.transition()
                .duration(750)
                .call(d3.zoom().transform,
                    d3.zoomIdentity.translate(width / 2, height / 2).scale(0.8).translate(-width / 2, -height / 2));
        }

        // ==================== TOOLTIP HELPER ====================
        let tooltip = null;

        function showTooltip(event, data) {
            if (!tooltip) {
                tooltip = d3.select('body').append('div')
                    .attr('class', 'tooltip')
                    .style('opacity', 0);
            }

            let html = '';
            if (data.name) {
                html = `<strong>${data.name}</strong><br/>`;
            }
            if (data.fullPath) {
                html += `Path: ${data.fullPath}<br/>`;
            }
            if (data.time !== undefined) {
                html += `Time: ${data.time.toFixed(1)} ms<br/>`;
            }
            if (data.phase) {
                html += `Phase: ${data.phase}<br/>`;
            }
            if (data.lines !== undefined) {
                html += `Lines: ${data.lines}<br/>`;
            }
            if (data.count !== undefined) {
                html += `Instantiations: ${data.count}<br/>`;
            }
            if (data.percentage !== undefined) {
                html += `Percentage: ${data.percentage.toFixed(1)}%<br/>`;
            }
            if (data.type) {
                html += `Type: ${data.type}<br/>`;
            }
            if (data.connections !== undefined) {
                html += `Connections: ${data.connections}<br/>`;
            }
            if (data.depth !== undefined) {
                html += `Depth: ${data.depth}<br/>`;
            }
            if (data.children !== undefined && data.children > 0) {
                html += `Children: ${data.children}`;
            }

            tooltip.html(html)
                .style('left', (event.pageX + 10) + 'px')
                .style('top', (event.pageY - 10) + 'px')
                .transition()
                .duration(200)
                .style('opacity', 1);
        }

        function hideTooltip() {
            if (tooltip) {
                tooltip.transition()
                    .duration(200)
                    .style('opacity', 0);
            }
        }

        window.showTab = showTab;
        window.filterFiles = filterFiles;
        window.sortFiles = sortFiles;
        window.applyFileLimit = applyFileLimit;
        window.resetZoom = resetZoom;
        window.resetIncludeTree = resetIncludeTree;
        window.renderIncludeTree = renderIncludeTree;
        window.renderTimeline = renderTimeline;
        window.renderTreemap = renderTreemap;
        window.renderTemplates = renderTemplates;
        window.renderDependencyGraph = renderDependencyGraph;
    })();
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