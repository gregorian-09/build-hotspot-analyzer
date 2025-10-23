//
// Created by gregorian on 23/10/2025.
//

#include "bha/export/report_generator.h"
#include <filesystem>
#include <utility>

namespace bha::export_module {

    ReportGenerator::ReportGenerator(Options options)
        : options_(std::move(options)) {}

    core::Result<void> ReportGenerator::generate(
        const core::MetricsSummary& metrics,
        const std::vector<core::Suggestion>& suggestions,
        const core::BuildTrace& trace
    ) const
    {
        const auto exporter = ExporterFactory::create_exporter(options_.format);

        if (auto result = exporter->export_report(metrics, suggestions, trace, options_.output_path); !result.is_success()) {
            return result;
        }

        if (options_.auto_open && options_.format == ExportFormat::HTML) {
            open_file_in_browser(options_.output_path);
        }

        return core::Result<void>::success();
    }

    core::Result<void> ReportGenerator::generate_multi_format(
        const core::MetricsSummary& metrics,
        const std::vector<core::Suggestion>& suggestions,
        const core::BuildTrace& trace,
        const std::vector<ExportFormat>& formats,
        const std::string& base_output_path
    ) {
        for (const auto format : formats) {
            std::string output_path = get_output_path_for_format(base_output_path, format);

            const auto exporter = ExporterFactory::create_exporter(format);

            if (auto result = exporter->export_report(metrics, suggestions, trace, output_path); !result.is_success()) {
                return core::Result<void>::failure(core::Error{
                    result.error().code,
                    "Failed to generate " +
                              ExporterFactory::format_to_string(format) +
                              " report: " + result.error().message
                });
            }
        }

        return core::Result<void>::success();
    }

    std::string ReportGenerator::get_output_path_for_format(
        const std::string& base_path,
        const ExportFormat format
    ) {
        namespace fs = std::filesystem;

        const fs::path path(base_path);
        const std::string stem = path.stem().string();
        const std::string dir = path.parent_path().string();

        const auto exporter = ExporterFactory::create_exporter(format);
        const std::string extension = exporter->get_default_extension();

        if (dir.empty()) {
            return stem + extension;
        }
        return dir + "/" + stem + extension;
    }

    bool ReportGenerator::open_file_in_browser(const std::string& path) {
    #ifdef _WIN32
        std::string command = R"(start "" ")" + path + "\"";
    #elif __APPLE__
        std::string command = "open \"" + path + "\"";
    #else
        std::string command = "xdg-open \"" + path + "\" 2>/dev/null";
    #endif
        return system(command.c_str()) == 0;
    }

};