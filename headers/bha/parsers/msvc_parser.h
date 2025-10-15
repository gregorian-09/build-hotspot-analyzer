//
// Created by gregorian on 15/10/2025.
//

#ifndef MSVC_PARSER_H
#define MSVC_PARSER_H

#include "bha/parsers/parser.h"

namespace bha::parsers {

    /**
     * Parser for MSVC build traces (time and include data) emitted by the MSVC toolchain.
     *
     * This parser interprets MSVC’s trace or log output, extracting per-file compilation times,
     * include times, and template instantiation costs. It converts those into
     * `CompilationUnit` objects for downstream analysis.
     */
    class MSVCTraceParser final : public TraceParser {
    public:
        MSVCTraceParser() = default;
        ~MSVCTraceParser() override = default;

        /**
         * Read and parse a file containing an MSVC build trace.
         *
         * @param file_path Path to the trace/log file.
         * @return A Result containing a list of parsed `CompilationUnit` objects or an Error.
         */
        core::Result<std::vector<core::CompilationUnit>> parse(
            std::string_view file_path
        ) override;

        /**
         * Parse MSVC trace content from a string.
         *
         * @param content The raw trace/log text.
         * @return A Result containing a list of `CompilationUnit` objects or an Error.
         */
        core::Result<std::vector<core::CompilationUnit>> parse_string(
            std::string_view content
        ) override;

        /**
         * Return a human-readable name for this parser’s format.
         * Example: "MSVC Build Trace".
         */
        [[nodiscard]] std::string get_format_name() const override;

        /**
         * Return the compiler type associated with this parser.
         * Always returns CompilerType::MSVC.
         */
        [[nodiscard]] CompilerType get_compiler_type() const override;

        /**
         * Determine whether this parser can parse the specified file.
         *
         * Typically checks extension or content signatures.
         *
         * @param file_path Path to the candidate trace file.
         * @return True if parsing is supported, false otherwise.
         */
        [[nodiscard]] bool can_parse(std::string_view file_path) const override;

        /**
         * Return the capabilities supported by this parser (timing, include metrics, etc.).
         */
        [[nodiscard]] ParserCapabilities get_capabilities() const override;

        /**
         * Return the file extensions (or patterns) supported by this parser.
         *
         * Example: { ".etl", ".msvc.log" }.
         */
        [[nodiscard]] std::vector<std::string> get_supported_extensions() const override;

    private:
        /**
         * Represents an entry of compilation time data in MSVC trace output.
         */
        struct TimeEntry {
            std::string file_or_header;
            double time_seconds;
            int count;
        };

        /**
         * Represents a template instantiation entry with time cost.
         */
        struct TemplateEntry {
            std::string template_name;
            double time_seconds;
        };

        /**
         * Parse per-file compilation times from raw trace content.
         *
         * @param content The raw trace or log text.
         * @return A Result containing a vector of `TimeEntry` or an Error.
         */
        static core::Result<std::vector<TimeEntry>> parse_file_times(
            std::string_view content
        );

        /**
         * Parse include processing times from raw trace content.
         *
         * @param content The raw trace or log text.
         * @return A Result containing a vector of `TimeEntry` or an Error.
         */
        static core::Result<std::vector<TimeEntry>> parse_include_times(
            std::string_view content
        );

        /**
         * Parse template instantiation timing entries from raw trace content.
         *
         * @param content The raw trace or log text.
         * @return A Result containing a vector of `TemplateEntry` or an Error.
         */
        static core::Result<std::vector<TemplateEntry>> parse_template_times(
            std::string_view content
        );

        /**
         * Build a `CompilationUnit` from parsed file-, include-, and template-time data.
         *
         * @param file_times Parsed compilation time entries.
         * @param include_times Parsed include times.
         * @param template_times Parsed template instantiation times.
         * @return A Result containing the constructed `CompilationUnit`.
         */
        static core::Result<core::CompilationUnit> build_compilation_unit(
            const std::vector<TimeEntry>& file_times,
            const std::vector<TimeEntry>& include_times,
            const std::vector<TemplateEntry>& template_times
        );

        /**
         * Extract the primary source file path from the list of file-time entries.
         *
         * @param file_times Parsed compilation time entries.
         * @return The path of the main source file (or fallback) used in the compilation unit.
         */
        static std::string extract_main_file(const std::vector<TimeEntry>& file_times);

        /**
         * Parse a single line of trace output to a TimeEntry if valid.
         *
         * @param line A line from the trace text.
         * @return Optional TimeEntry if parsed successfully; std::nullopt otherwise.
         */
        static std::optional<TimeEntry> parse_time_line(std::string_view line);

        /**
         * Parse a single line containing template instantiation timing.
         *
         * @param line A line from the trace text.
         * @return Optional TemplateEntry if parsed successfully; std::nullopt otherwise.
         */
        static std::optional<TemplateEntry> parse_template_line(std::string_view line);

        /**
         * Convert a string representing a time value into a double (seconds).
         *
         * @param time_str The raw time string (e.g., "0.123s").
         * @return Parsed time in seconds.
         */
        static double parse_time_value(std::string_view time_str);
    };

} // namespace bha::parsers


#endif //MSVC_PARSER_H
