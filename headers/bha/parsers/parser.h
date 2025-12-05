//
// Created by gregorian on 15/10/2025.
//

#ifndef PARSER_H
#define PARSER_H

#include <functional>
#include "bha/core/types.h"
#include "bha/core/result.h"
#include <string>
#include <string_view>
#include <memory>
#include <vector>

namespace bha::parsers
{
    enum class CompilerType {
        CLANG,
        GCC,
        MSVC,
        UNKNOWN
    };

    struct ParserCapabilities {
        bool supports_timing = false;
        bool supports_templates = false;
        bool supports_preprocessing = false;
        bool supports_optimization = false;
        bool supports_dependencies = false;
    };

    /**
     * Abstract interface for parsing compiler trace or build log files
     * into structured `CompilationUnit` objects.
     *
     * Implementations of this interface are responsible for reading trace files
     * produced by different compilers (e.g., Clang, MSVC, GCC) and converting
     * them into a standard internal representation.
     */
    class TraceParser {
    public:
        virtual ~TraceParser() = default;

        /**
         * Parses a trace file from disk into a list of `CompilationUnit` objects.
         *
         * @param file_path Path to the trace or log file.
         * @return A Result containing the parsed compilation units, or an Error if parsing fails.
         */
        virtual core::Result<std::vector<core::CompilationUnit>> parse(
            std::string_view file_path
        ) = 0;

        /**
         * Parses trace data from a string instead of a file.
         *
         * Useful for tests, in-memory parsing, or network-based ingestion.
         *
         * @param content The raw trace or log content as a string.
         * @return A Result containing the parsed compilation units, or an Error if parsing fails.
         */
        virtual core::Result<std::vector<core::CompilationUnit>> parse_string(
            std::string_view content
        ) = 0;

        /**
         * Returns a human-readable name describing the trace format.
         *
         * Example: "Clang Trace", "MSVC JSON Log", "GCC Build Output".
         */
        [[nodiscard]] virtual std::string get_format_name() const = 0;

        /**
         * Returns the compiler type that this parser supports.
         *
         * Example: CompilerType::CLANG, CompilerType::MSVC, etc.
         */
        [[nodiscard]] virtual CompilerType get_compiler_type() const = 0;

        /**
         * Determines whether this parser can handle the specified file.
         *
         * Usually checks the file extension or inspects the first few lines of the file.
         *
         * @param file_path Path to the file to test.
         * @return True if the parser can handle this file, false otherwise.
         */
        [[nodiscard]] virtual bool can_parse(std::string_view file_path) const = 0;

        /**
         * Returns a set of capabilities describing what this parser supports,
         * such as template tracking or dependency extraction.
         */
        [[nodiscard]] virtual ParserCapabilities get_capabilities() const = 0;

        /**
         * Returns a list of file extensions that this parser can handle.
         *
         * Example: { ".json", ".log", ".trace" }.
         */
        [[nodiscard]] virtual std::vector<std::string> get_supported_extensions() const = 0;
    };

    /**
     * Factory class responsible for creating and managing `TraceParser` instances.
     *
     * The factory provides automatic detection of compilers and their corresponding
     * parsers, as well as manual registration for custom or third-party parser implementations.
     */
    class ParserFactory {
    public:
        /**
         * Creates a parser suitable for the given trace file.
         *
         * The function attempts to detect the compiler type automatically from
         * the file name or contents.
         *
         * @param file_path Path to the trace or log file.
         * @return A Result containing a unique pointer to the created parser, or an Error if detection fails.
         */
        static core::Result<std::unique_ptr<TraceParser>> create_parser(
            std::string_view file_path
        );

        /**
         * Creates a parser explicitly for the specified compiler type.
         *
         * @param type Compiler type to create a parser for.
         * @return A Result containing a unique pointer to the parser, or an Error if the compiler type is unsupported.
         */
        static core::Result<std::unique_ptr<TraceParser>> create_parser_for_compiler(
            CompilerType type
        );

        /**
         * Registers a new parser factory function for a specific compiler type.
         *
         * Enables dynamic extension or plugin-based parser registration at runtime.
         *
         * @param type Compiler type the parser handles.
         * @param factory Function that creates instances of the parser.
         */
        static void register_parser(
            CompilerType type,
            std::function<std::unique_ptr<TraceParser>()> factory
        );

        /**
         * Attempts to determine the compiler type based on the contents of a file.
         *
         * @param file_path Path to the file to inspect.
         * @return The detected compiler type.
         */
        static CompilerType detect_compiler_from_file(std::string_view file_path);

        /**
         * Attempts to determine the compiler type based on the raw trace content.
         *
         * @param content The raw trace data.
         * @return The detected compiler type.
         */
        static CompilerType detect_compiler_from_content(std::string_view content);

        /**
         * Returns a list of all compiler types that have registered parsers.
         */
        static std::vector<CompilerType> get_supported_compilers();

        /**
         * Detects the compiler version of the executable located at the given path.
         *
         * @param compiler_path a string view representing the path to the compiler binary or executable.
         * @param out_version an output parameter (by reference) which will be set to the version string if detection succeeds.
         * @return a Result holding the detected {@enum CompilerType} on success, or an error code on failure.
         */
        static core::Result<CompilerType> detect_compiler_version(
            std::string_view compiler_path,
            std::string& out_version
        );

    };

}

#endif //PARSER_H
