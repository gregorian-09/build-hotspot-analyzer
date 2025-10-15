//
// Created by gregorian on 15/10/2025.
//

#ifndef GCC_HEADER_H
#define GCC_HEADER_H

#include "bha/parsers/parser.h"
#include <regex>

namespace bha::parsers {

/**
 * Parser implementation for GCC's time-report output format.
 *
 * This parser interprets GCC’s compilation time reports, which summarize
 * the time spent in different compilation phases such as parsing, optimization,
 * code generation, and linking. It extracts structured performance data
 * and converts it into `CompilationUnit` objects for further analysis.
 *
 * The GCC time report is typically produced using the `-ftime-report` compiler flag.
 */
class GCCTimeReportParser final : public TraceParser {
public:
    GCCTimeReportParser() = default;
    ~GCCTimeReportParser() override = default;

    /**
     * Parses a GCC time-report file from disk and converts it into a list of
     * `CompilationUnit` objects.
     *
     * @param file_path Path to the GCC time-report file.
     * @return A Result containing parsed compilation units, or an Error if parsing fails.
     */
    core::Result<std::vector<core::CompilationUnit>> parse(
        std::string_view file_path
    ) override;

    /**
     * Parses GCC time-report content from a raw string.
     *
     * Useful for analyzing report data that has already been loaded into memory.
     *
     * @param content Raw text content of a GCC time-report.
     * @return A Result containing parsed compilation units, or an Error if parsing fails.
     */
    core::Result<std::vector<core::CompilationUnit>> parse_string(
        std::string_view content
    ) override;

    /**
     * Returns a human-readable name for this parser’s format.
     * Example: "GCC Time Report".
     */
    [[nodiscard]] std::string get_format_name() const override;

    /**
     * Returns the compiler type supported by this parser.
     * Always returns CompilerType::GCC.
     */
    [[nodiscard]] CompilerType get_compiler_type() const override;

    /**
     * Determines whether the parser can process the given file.
     *
     * This may involve checking for typical file extensions or
     * validating the file’s content structure.
     *
     * @param file_path Path to a potential GCC time-report file.
     * @return True if the parser can handle the file, false otherwise.
     */
    [[nodiscard]] bool can_parse(std::string_view file_path) const override;

    /**
     * Returns a description of the parser’s supported capabilities,
     * including timing extraction and phase analysis.
     */
    [[nodiscard]] ParserCapabilities get_capabilities() const override;

    /**
     * Lists supported file extensions for GCC time-report files.
     * Typically returns { ".txt", ".log" }.
     */
    [[nodiscard]] std::vector<std::string> get_supported_extensions() const override;

private:
    /**
     * Represents a single entry in a GCC time-report.
     *
     * Each entry corresponds to a compiler phase (e.g., “parse”, “optimize”)
     * and contains user time, system time, wall time, and percentage of total time.
     */
    struct TimeEntry {
        std::string phase_name;
        double usr_time;
        double sys_time;
        double wall_time;
        double percentage;
    };

    /**
     * Parses all time entries from the provided GCC time-report text.
     *
     * @param content The raw report text.
     * @return A Result containing parsed time entries, or an Error on failure.
     */
    static core::Result<std::vector<TimeEntry>> parse_time_entries(
        std::string_view content
    );

    /**
     * Builds a `CompilationUnit` from the parsed time entries.
     *
     * Aggregates per-phase timing data into a single structured compilation unit
     * that can be compared or visualized.
     *
     * @param entries List of parsed time entries.
     * @param file_path Path to the analyzed report file.
     * @return A Result containing the constructed `CompilationUnit`.
     */
    static core::Result<core::CompilationUnit> build_compilation_unit(
        const std::vector<TimeEntry>& entries,
        std::string_view file_path
    );

    /**
     * Extracts and maps phase timing data from time entries into
     * the appropriate fields of the provided `CompilationUnit`.
     *
     * @param entries Parsed GCC time entries.
     * @param unit Compilation unit to populate.
     */
    static void extract_timing_from_entries(
        const std::vector<TimeEntry>& entries,
        core::CompilationUnit& unit
    );

    /**
     * Attempts to extract the source file path from the time-report content.
     *
     * GCC reports do not always include file paths, but this method may infer one
     * from metadata or context if available.
     *
     * @param content The raw report text.
     * @return Extracted file path, or an empty string if none found.
     */
    static std::string extract_file_path_from_content(
        std::string_view content
    );

    /**
     * Determines whether a given line of text matches the expected format
     * of a GCC time-report entry.
     *
     * @param line A single line from the report file.
     * @return True if the line is part of a time entry, false otherwise.
     */
    [[nodiscard]] static bool is_time_report_line(std::string_view line) ;

    /**
     * Parses an individual GCC time-report line into a `TimeEntry` structure.
     *
     * If the line does not conform to the expected format, the method returns `std::nullopt`.
     *
     * @param line A single line from the report.
     * @return Parsed `TimeEntry`, or std::nullopt if parsing fails.
     */
    static std::optional<TimeEntry> parse_time_entry_line(std::string_view line);
};

} // namespace bha::parsers
 // namespace bha::parsers

#endif //GCC_HEADER_H
