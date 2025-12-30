//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_EXPORTER_HPP
#define BHA_EXPORTER_HPP


/**
 * @file exporter.hpp
 * @brief Export interfaces for analysis results.
 *
 * Provides a unified interface for exporting analysis results
 * to various formats:
 * - JSON (machine-readable, versioned schema)
 * - HTML (interactive visualization dashboard)
 * - CSV (tabular data for spreadsheets)
 * - SARIF (Static Analysis Results Interchange Format)
 *
 * Design principles:
 * - Streaming support for large datasets
 * - Versioned output formats for compatibility
 * - Configurable detail levels
 * - Support for partial exports
 */

#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/types.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bha::exporters
{
    /**
     * Export format enumeration.
     */
    enum class ExportFormat {
        JSON,
        HTML,
        CSV,
        SARIF,
        Markdown
    };

    /**
     * Export options for controlling output.
     */
    struct ExportOptions {
        // General options
        bool pretty_print = true;           // Format output for readability
        bool include_metadata = true;       // Include version, timestamp, etc.
        bool compress = false;              // Gzip compress output

        // Content options
        bool include_file_details = true;   // Per-file analysis results
        bool include_dependencies = true;   // Dependency graph
        bool include_templates = true;      // Template instantiation data
        bool include_symbols = true;        // Symbol information
        bool include_suggestions = true;    // Optimization suggestions
        bool include_timing = true;         // Timing breakdown

        // Filtering options
        Duration min_compile_time = Duration::zero();  // Min time to include
        double min_confidence = 0.0;        // Min suggestion confidence
        std::size_t max_files = 0;          // 0 = unlimited
        std::size_t max_suggestions = 0;    // 0 = unlimited

        // HTML-specific options
        bool html_interactive = true;       // Include D3.js visualizations
        bool html_offline = true;           // Bundle all assets (no CDN)
        bool html_dark_mode = false;        // Default to dark mode
        std::string html_title = "Build Hotspot Analysis Report";

        // JSON-specific options
        std::string json_schema_version = "1.0.0";
        bool json_streaming = false;        // Stream large arrays
    };

    /**
     * Export metadata included in output.
     */
    struct ExportMetadata {
        std::string bha_version = "2.0.0";
        std::string schema_version = "1.0.0";
        Timestamp generated_at;
        std::string project_name;
        std::string project_path;
        std::string git_commit;
        std::string git_branch;
        Duration total_analysis_time = Duration::zero();
        std::size_t files_analyzed = 0;
        std::size_t suggestions_generated = 0;
    };

    /**
     * Progress callback for long-running exports.
     */
    using ExportProgressCallback = std::function<void(std::size_t current, std::size_t total, std::string_view stage)>;

    /**
     * Interface for all exporters.
     */
    class IExporter {
    public:
        virtual ~IExporter() = default;

        /**
         * Returns the export format this exporter produces.
         */
        [[nodiscard]] virtual ExportFormat format() const noexcept = 0;

        /**
         * Returns the file extension for this format.
         */
        [[nodiscard]] virtual std::string_view file_extension() const noexcept = 0;

        /**
         * Returns a human-readable name for this format.
         */
        [[nodiscard]] virtual std::string_view format_name() const noexcept = 0;

        /**
         * Exports analysis results to a file.
         *
         * @param path Output file path.
         * @param analysis Analysis results to export.
         * @param suggestions Suggestions to include (optional).
         * @param options Export options.
         * @param progress Progress callback (optional).
         * @return Success or error.
         */
        [[nodiscard]] virtual Result<void, Error> export_to_file(
            const fs::path& path,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const = 0;

        /**
         * Exports analysis results to a stream.
         *
         * @param stream Output stream.
         * @param analysis Analysis results to export.
         * @param suggestions Suggestions to include (optional).
         * @param options Export options.
         * @param progress Progress callback (optional).
         * @return Success or error.
         */
        [[nodiscard]] virtual Result<void, Error> export_to_stream(
            std::ostream& stream,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const = 0;

        /**
         * Exports analysis results to a string.
         *
         * @param analysis Analysis results to export.
         * @param suggestions Suggestions to include (optional).
         * @param options Export options.
         * @return Exported string or error.
         */
        [[nodiscard]] virtual Result<std::string, Error> export_to_string(
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options
        ) const = 0;
    };

    /**
     * Factory for creating exporters.
     */
    class ExporterFactory {
    public:
        /**
         * Creates an exporter for the specified format.
         *
         * @param format The export format.
         * @return Exporter instance or error.
         */
        [[nodiscard]] static Result<std::unique_ptr<IExporter>, Error> create(ExportFormat format);

        /**
         * Creates an exporter based on file extension.
         *
         * @param path File path to determine format from.
         * @return Exporter instance or error.
         */
        [[nodiscard]] static Result<std::unique_ptr<IExporter>, Error> create_for_file(const fs::path& path);

        /**
         * Returns all available export formats.
         */
        [[nodiscard]] static std::vector<ExportFormat> available_formats();
    };

    /**
     * Converts format to string.
     */
    [[nodiscard]] std::string_view format_to_string(ExportFormat format) noexcept;

    /**
     * Parses format from string.
     */
    [[nodiscard]] std::optional<ExportFormat> string_to_format(std::string_view str) noexcept;

    /**
     * JSON Exporter.
     *
     * Exports analysis results to JSON format with a versioned schema.
     * Supports streaming for large datasets.
     */
    class JsonExporter : public IExporter {
    public:
        [[nodiscard]] ExportFormat format() const noexcept override { return ExportFormat::JSON; }
        [[nodiscard]] std::string_view file_extension() const noexcept override { return ".json"; }
        [[nodiscard]] std::string_view format_name() const noexcept override { return "JSON"; }

        [[nodiscard]] Result<void, Error> export_to_file(
            const fs::path& path,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const override;

        [[nodiscard]] Result<void, Error> export_to_stream(
            std::ostream& stream,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const override;

        [[nodiscard]] Result<std::string, Error> export_to_string(
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options
        ) const override;
    };

    /**
     * HTML Exporter.
     *
     * Exports analysis results to an interactive HTML dashboard with:
     * - D3.js dependency graph visualization
     * - File explorer with drill-down
     * - Compilation time heatmap
     * - Suggestion cards
     * - Search and filter capabilities
     */
    class HtmlExporter : public IExporter {
    public:
        [[nodiscard]] ExportFormat format() const noexcept override { return ExportFormat::HTML; }
        [[nodiscard]] std::string_view file_extension() const noexcept override { return ".html"; }
        [[nodiscard]] std::string_view format_name() const noexcept override { return "HTML"; }

        [[nodiscard]] Result<void, Error> export_to_file(
            const fs::path& path,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const override;

        [[nodiscard]] Result<void, Error> export_to_stream(
            std::ostream& stream,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const override;

        [[nodiscard]] Result<std::string, Error> export_to_string(
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options
        ) const override;
    };

    /**
     * CSV Exporter.
     *
     * Exports analysis results to CSV format for spreadsheet analysis.
     * Multiple CSV files for different data types.
     */
    class CsvExporter : public IExporter {
    public:
        [[nodiscard]] ExportFormat format() const noexcept override { return ExportFormat::CSV; }
        [[nodiscard]] std::string_view file_extension() const noexcept override { return ".csv"; }
        [[nodiscard]] std::string_view format_name() const noexcept override { return "CSV"; }

        [[nodiscard]] Result<void, Error> export_to_file(
            const fs::path& path,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const override;

        [[nodiscard]] Result<void, Error> export_to_stream(
            std::ostream& stream,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const override;

        [[nodiscard]] Result<std::string, Error> export_to_string(
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options
        ) const override;
    };

    /**
     * Markdown Exporter.
     *
     * Exports analysis results to Markdown format for documentation.
     */
    class MarkdownExporter : public IExporter {
    public:
        [[nodiscard]] ExportFormat format() const noexcept override { return ExportFormat::Markdown; }
        [[nodiscard]] std::string_view file_extension() const noexcept override { return ".md"; }
        [[nodiscard]] std::string_view format_name() const noexcept override { return "Markdown"; }

        [[nodiscard]] Result<void, Error> export_to_file(
            const fs::path& path,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const override;

        [[nodiscard]] Result<void, Error> export_to_stream(
            std::ostream& stream,
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options,
            ExportProgressCallback progress
        ) const override;

        [[nodiscard]] Result<std::string, Error> export_to_string(
            const analyzers::AnalysisResult& analysis,
            const std::vector<Suggestion>& suggestions,
            const ExportOptions& options
        ) const override;
    };
} // namespace bha::exporters

#endif //BHA_EXPORTER_HPP