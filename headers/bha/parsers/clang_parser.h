//
// Created by gregorian on 15/10/2025.
//

#ifndef CLANG_PARSER_H
#define CLANG_PARSER_H

#include "bha/parsers/parser.h"
#include <simdjson.h>

namespace bha::parsers {

    /**
     * Parser implementation for Clang's time-trace JSON format.
     *
     * This parser reads and interprets Clang’s time-trace output, which records
     * detailed timing information about the compilation process. It extracts
     * key metrics such as preprocessing, parsing, code generation, and optimization
     * times, as well as template instantiation data.
     *
     * The parser converts these traces into `CompilationUnit` objects, which can
     * then be analyzed or visualized by the rest of the build analysis system.
     */
    class ClangTimeTraceParser final : public TraceParser {
    public:
        ClangTimeTraceParser() = default;
        ~ClangTimeTraceParser() override = default;

        /**
         * Parses a Clang time-trace file from disk and converts it into a list of
         * `CompilationUnit` objects.
         *
         * @param file_path Path to the Clang time-trace JSON file.
         * @return A Result containing parsed compilation units, or an Error on failure.
         */
        core::Result<std::vector<core::CompilationUnit>> parse(
            std::string_view file_path
        ) override;

        /**
         * Parses Clang time-trace data from a raw string.
         *
         * Useful for in-memory traces or testing environments where the trace
         * content is provided without a file.
         *
         * @param content The raw JSON content of the Clang time-trace.
         * @return A Result containing parsed compilation units, or an Error on failure.
         */
        core::Result<std::vector<core::CompilationUnit>> parse_string(
            std::string_view content
        ) override;

        /**
         * Returns a human-readable name for this parser’s format.
         * Example: "Clang Time Trace".
         */
        [[nodiscard]] std::string get_format_name() const override;

        /**
         * Returns the compiler type this parser supports.
         * Always returns CompilerType::CLANG.
         */
        [[nodiscard]] CompilerType get_compiler_type() const override;

        /**
         * Determines if the parser can handle the provided file.
         * Typically, checks if the file has a `.json` extension and contains
         * expected Clang trace markers.
         *
         * @param file_path Path to the potential Clang trace file.
         * @return True if the parser can parse the file, false otherwise.
         */
        [[nodiscard]] bool can_parse(std::string_view file_path) const override;

        /**
         * Returns a description of the parser’s capabilities.
         * Includes information such as support for timing metrics and
         * template instantiation extraction.
         */
        [[nodiscard]] ParserCapabilities get_capabilities() const override;

        /**
         * Lists supported file extensions for Clang time-trace files.
         * Typically returns { ".json" }.
         */
        [[nodiscard]] std::vector<std::string> get_supported_extensions() const override;

    private:
        /**
         * Represents a single event entry from the Clang time-trace JSON.
         *
         * Each event contains timing, name, and thread information,
         * which together describe a portion of the compilation process.
         */
        struct TraceEvent {
            std::string name;
            std::string phase;
            uint64_t timestamp_us;
            uint64_t duration_us;
            std::string detail;
            int pid;
            int tid;
        };

        /**
         * Parses the list of events from a Clang time-trace document.
         *
         * @param doc The parsed simdjson document representing the trace.
         * @return A Result containing parsed events, or an Error if extraction fails.
         */
        static core::Result<std::vector<TraceEvent>> parse_trace_events(
            simdjson::ondemand::document& doc
        );

        /**
         * Builds a `CompilationUnit` from the parsed trace events.
         *
         * Aggregates timing, template, and file data into a structured unit
         * suitable for analysis.
         *
         * @param events Parsed trace events.
         * @param file_path Path to the trace file.
         * @return A Result containing a populated `CompilationUnit`, or an Error.
         */
        static core::Result<core::CompilationUnit> build_compilation_unit(
            const std::vector<TraceEvent>& events,
            std::string_view file_path
        );

        /**
         * Extracts timing metrics from trace events and populates
         * the corresponding fields in a `CompilationUnit`.
         *
         * @param events List of trace events.
         * @param unit Compilation unit to update with timing data.
         */
        static void extract_timing_from_events(
            const std::vector<TraceEvent>& events,
            core::CompilationUnit& unit
        ) ;

        /**
         * Extracts template instantiation data from trace events and adds
         * them to the given `CompilationUnit`.
         *
         * @param events List of trace events.
         * @param unit Compilation unit to update with template information.
         */
        static void extract_template_instantiations(
            const std::vector<TraceEvent>& events,
            core::CompilationUnit& unit
        ) ;

        /**
         * Attempts to extract the source file path from trace events.
         *
         * @param events List of trace events.
         * @return File path inferred from trace metadata, or an empty string if unavailable.
         */
        static std::string extract_file_path_from_events(
            const std::vector<TraceEvent>& events
        );

        /**
         * Converts a timestamp in microseconds to milliseconds.
         *
         * @param us Time in microseconds.
         * @return Equivalent time in milliseconds as a double.
         */
        [[nodiscard]] static double microseconds_to_milliseconds(uint64_t us) ;
    };

} // namespace bha::parsers


#endif //CLANG_PARSER_H
