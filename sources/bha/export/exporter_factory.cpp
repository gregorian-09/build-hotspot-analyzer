//
// Created by gregorian on 23/10/2025.
//

#include "bha/export/exporter.h"
#include "bha/export/json_exporter.h"
#include "bha/export/html_exporter.h"
#include "bha/export/csv_exporter.h"
#include "bha/export/markdown_exporter.h"
#include "bha/export/text_exporter.h"
#include <algorithm>
#include <cctype>

namespace bha::export_module {

    std::unique_ptr<Exporter> ExporterFactory::create_exporter(const ExportFormat format) {
        switch (format) {
        case ExportFormat::JSON:
            return std::make_unique<JSONExporter>();
        case ExportFormat::HTML:
            return std::make_unique<HTMLExporter>();
        case ExportFormat::CSV:
            return std::make_unique<CSVExporter>();
        case ExportFormat::MARKDOWN:
            return std::make_unique<MarkdownExporter>();
        case ExportFormat::TEXT:
            return std::make_unique<TextExporter>();
        default:
            return std::make_unique<JSONExporter>();
        }
    }

    ExportFormat ExporterFactory::format_from_string(const std::string& format_str) {
        std::string lower = format_str;
        std::ranges::transform(lower, lower.begin(),
                               [](const unsigned char c) { return std::tolower(c); });

        if (lower == "json") return ExportFormat::JSON;
        if (lower == "html") return ExportFormat::HTML;
        if (lower == "csv") return ExportFormat::CSV;
        if (lower == "markdown" || lower == "md") return ExportFormat::MARKDOWN;
        if (lower == "text" || lower == "txt") return ExportFormat::TEXT;

        return ExportFormat::JSON;
    }

    std::string ExporterFactory::format_to_string(const ExportFormat format) {
        switch (format) {
        case ExportFormat::JSON: return "json";
        case ExportFormat::HTML: return "html";
        case ExportFormat::CSV: return "csv";
        case ExportFormat::MARKDOWN: return "markdown";
        case ExportFormat::TEXT: return "text";
        default: return "json";
        }
    }
}