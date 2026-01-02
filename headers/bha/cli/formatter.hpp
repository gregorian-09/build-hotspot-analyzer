//
// Created by gregorian-rayne on 1/2/26.
//

#ifndef BHA_FORMATTER_HPP
#define BHA_FORMATTER_HPP

/**
 * @file formatter.hpp
 * @brief Output formatting utilities for CLI.
 *
 * Provides consistent formatting for:
 * - Tables
 * - Duration/time values
 * - File sizes
 * - Colors and styles
 * - JSON output
 */

#include "bha/types.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace bha::cli
{
    /**
     * Terminal color codes.
     */
    namespace colors {

        extern const char* RESET;
        extern const char* BOLD;
        extern const char* DIM;
        extern const char* UNDERLINE;

        extern const char* RED;
        extern const char* GREEN;
        extern const char* YELLOW;
        extern const char* BLUE;
        extern const char* MAGENTA;
        extern const char* CYAN;
        extern const char* WHITE;

        extern const char* BG_RED;
        extern const char* BG_GREEN;
        extern const char* BG_YELLOW;

        /**
         * Returns true if colors should be used.
         */
        bool enabled();

        /**
         * Enable/disable colors globally.
         */
        void set_enabled(bool enable);

    }  // namespace colors

    /**
     * Table column definition.
     */
    struct Column {
        std::string header;
        std::size_t width = 0;    // 0 = auto
        bool right_align = false;
        std::optional<std::string> color;
    };

    /**
     * Table row data.
     */
    using Row = std::vector<std::string>;

    /**
     * Table formatter for aligned output.
     */
    class Table {
    public:
        explicit Table(std::vector<Column> columns);

        /**
         * Adds a row to the table.
         */
        void add_row(Row row);

        /**
         * Adds a separator row.
         */
        void add_separator();

        /**
         * Renders the table to a string.
         */
        [[nodiscard]] std::string render() const;

        /**
         * Renders the table to a stream.
         */
        void render(std::ostream& out) const;

        /**
         * Clears all rows.
         */
        void clear();

        /**
         * Sets whether to show headers.
         */
        void set_show_headers(bool show) { show_headers_ = show; }

        /**
         * Sets whether to show borders.
         */
        void set_show_borders(bool show) { show_borders_ = show; }

    private:
        void calculate_widths();

        std::vector<Column> columns_;
        std::vector<Row> rows_;
        std::vector<bool> separators_;
        bool show_headers_ = true;
        bool show_borders_ = false;
    };

    /**
     * Formats a duration for display.
     */
    [[nodiscard]] std::string format_duration(Duration d);

    /**
     * Formats a duration in milliseconds.
     */
    [[nodiscard]] std::string format_ms(double ms);

    /**
     * Formats a percentage.
     */
    [[nodiscard]] std::string format_percent(double pct);

    /**
     * Formats a file size.
     */
    [[nodiscard]] std::string format_size(std::size_t bytes);

    /**
     * Formats a count with comma separators.
     */
    [[nodiscard]] std::string format_count(std::size_t count);

    /**
     * Formats a file path for display (truncates if too long).
     */
    [[nodiscard]] std::string format_path(const fs::path& path, std::size_t max_width = 60);

    /**
     * Formats a timestamp.
     */
    [[nodiscard]] std::string format_timestamp(Timestamp ts);

    /**
     * Colorizes text based on priority.
     */
    [[nodiscard]] std::string colorize_priority(Priority priority);

    /**
     * Colorizes a suggestion type.
     */
    [[nodiscard]] std::string colorize_type(SuggestionType type);

    /**
     * Colorizes a duration based on threshold.
     */
    [[nodiscard]] std::string colorize_duration(Duration d, Duration warning_threshold, Duration critical_threshold);

    /**
     * Creates a colored bar graph.
     */
    [[nodiscard]] std::string bar_graph(double value, double max_value, std::size_t width = 20);

    /**
     * Summary printer for analysis results.
     */
    class SummaryPrinter {
    public:
        explicit SummaryPrinter(std::ostream& out);

        /**
         * Prints build summary.
         */
        void print_build_summary(const analyzers::AnalysisResult& result) const;

        /**
         * Prints file summary table.
         */
        void print_file_summary(const std::vector<analyzers::FileAnalysisResult>& files, std::size_t limit = 10) const;

        /**
         * Prints include summary.
         */
        void print_include_summary(const analyzers::DependencyAnalysisResult& deps, std::size_t limit = 10) const;

        /**
         * Print a summary of template analysis results.
         *
         * This function outputs a concise overview of the provided template
         * analysis information. It lists up to `limit` entries from
         * the `templates` result, optionally truncating template names for brevity.
         *
         * @param templates The results of template analysis to summarize.
         * @param limit The maximum number of template entries to display.
         *              Default is 10.
         * @param no_truncate If true, full template names are shown
         *                    without truncation; otherwise, names may be shortened
         *                    for a more compact display.
         */
        void print_template_summary(const analyzers::TemplateAnalysisResult& templates, std::size_t limit = 10, bool no_truncate = false) const;

        /**
         * Prints suggestion summary.
         */
        void print_suggestions(const std::vector<Suggestion>& suggestions, std::size_t limit = 0) const;

    private:
        std::ostream& out_;
    };

    /**
     * JSON output helpers.
     */
    namespace json {

        /**
         * Converts analysis result to JSON string.
         */
        [[nodiscard]] std::string to_json(const analyzers::AnalysisResult& result, bool pretty = true);

        /**
         * Converts suggestions to JSON string.
         */
        [[nodiscard]] std::string to_json(const std::vector<Suggestion>& suggestions, bool pretty = true);

        /**
         * Converts file analysis results to JSON string.
         */
        [[nodiscard]] std::string to_json(const std::vector<analyzers::FileAnalysisResult>& files, bool pretty = true);

    }  // namespace json
}  // namespace bha::cli

#endif //BHA_FORMATTER_HPP