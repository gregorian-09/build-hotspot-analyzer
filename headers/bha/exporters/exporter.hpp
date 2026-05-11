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
     * @brief Supported export payload formats.
     */
    enum class ExportFormat {
        JSON,
        HTML,
        CSV,
        SARIF,
        Markdown
    };

    /**
     * @brief Export options controlling output shape and filtering.
     */
    struct ExportOptions {
        /// Pretty-print structured output where format supports it.
        bool pretty_print = true;
        /// Include metadata block (version, timestamps, provenance).
        bool include_metadata = true;
        /// Compress output payload when sink supports compression.
        bool compress = false;

        /// Include per-file analysis sections.
        bool include_file_details = true;
        /// Include dependency graph sections.
        bool include_dependencies = true;
        /// Include template-instantiation sections.
        bool include_templates = true;
        /// Include symbol-analysis sections.
        bool include_symbols = true;
        /// Include optimization suggestion sections.
        bool include_suggestions = true;
        /// Include timing breakdown sections.
        bool include_timing = true;

        /// Minimum compile duration filter for included file entries.
        Duration min_compile_time = Duration::zero();
        /// Minimum confidence threshold filter for suggestion entries.
        double min_confidence = 0.0;
        /// Maximum number of file entries to emit (`0` = unlimited).
        std::size_t max_files = 0;
        /// Maximum number of suggestions to emit (`0` = unlimited).
        std::size_t max_suggestions = 0;

        /// Include interactive visualizations when exporting HTML.
        bool html_interactive = true;
        /// Bundle assets for offline HTML viewing.
        bool html_offline = true;
        /// Use dark mode defaults in HTML output.
        bool html_dark_mode = true;
        /// HTML document title.
        std::string html_title = "Build Hotspot Analysis Report";

        /// Declared schema version for JSON output.
        std::string json_schema_version = "0.1.0";
        /// Stream large JSON arrays progressively.
        bool json_streaming = false;
    };

    /**
     * @brief Metadata emitted alongside analysis payloads.
     */
    struct ExportMetadata {
        /// BHA binary/library version.
        std::string bha_version = "0.1.0";
        /// Output schema version.
        std::string schema_version = "0.1.0";
        /// Timestamp when export was generated.
        Timestamp generated_at;
        /// Logical project name.
        std::string project_name;
        /// Project root path string.
        std::string project_path;
        /// Git commit hash when available.
        std::string git_commit;
        /// Git branch name when available.
        std::string git_branch;
        /// Total analysis runtime.
        Duration total_analysis_time = Duration::zero();
        /// Number of files analyzed.
        std::size_t files_analyzed = 0;
        /// Number of suggestions generated.
        std::size_t suggestions_generated = 0;
    };

    /**
     * @brief Progress callback signature for long-running exports.
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
     * PR annotation output formats.
     */
    enum class PRAnnotationFormat {
        GitHub,
        GitLabCodeQuality
    };

    /**
     * Converts PR annotation format to string.
     */
    [[nodiscard]] std::string_view pr_annotation_format_to_string(PRAnnotationFormat format) noexcept;

    /**
     * Parses PR annotation format from string.
     */
    [[nodiscard]] std::optional<PRAnnotationFormat> string_to_pr_annotation_format(std::string_view str) noexcept;

    /**
     * Exports suggestions as PR-native annotations.
     *
     * - GitHub: workflow command annotations (`::warning ...::message`)
     * - GitLabCodeQuality: Code Quality JSON artifact format
     */
    [[nodiscard]] Result<std::string, Error> export_pr_annotations(
        const std::vector<Suggestion>& suggestions,
        PRAnnotationFormat format,
        const fs::path& project_root = {},
        std::size_t max_suggestions = 0
    );

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
     * SARIF Exporter.
     *
     * Exports suggestions as SARIF 2.1.0 for code-scanning pipelines.
     */
    class SarifExporter : public IExporter {
    public:
        [[nodiscard]] ExportFormat format() const noexcept override { return ExportFormat::SARIF; }
        [[nodiscard]] std::string_view file_extension() const noexcept override { return ".sarif"; }
        [[nodiscard]] std::string_view format_name() const noexcept override { return "SARIF"; }

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
