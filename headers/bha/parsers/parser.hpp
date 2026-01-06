//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_PARSER_HPP
#define BUILDTIMEHOTSPOTANALYZER_PARSER_HPP

/**
 * @file parser.hpp
 * @brief Trace parser interface.
 *
 * Defines the interface for build trace parsers. Each compiler that
 * supports timing/tracing output should have a corresponding parser
 * implementation.
 *
 * Supported compilers:
 * - Clang: -ftime-trace JSON output
 * - GCC:   -ftime-report
 * - MSVC:   /Bt+ /d1reportTime
 * - Intel: (future) Various timing options
 */

#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/types.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>

namespace bha::parsers {

    namespace fs = std::filesystem;

    /**
     * Base interface for all trace parsers.
     *
     * Implementations should be stateless and thread-safe for parsing
     * multiple files concurrently.
     */
    class ITraceParser {
    public:
        virtual ~ITraceParser() = default;

        /**
         * Returns the parser name (e.g., "Clang", "GCC").
         */
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;

        /**
         * Returns the compiler type this parser handles.
         */
        [[nodiscard]] virtual CompilerType compiler_type() const noexcept = 0;

        /**
         * Returns file extensions this parser can handle.
         *
         * @return Vector of extensions (e.g., {".json"}).
         */
        [[nodiscard]] virtual std::vector<std::string> supported_extensions() const = 0;

        /**
         * Checks if this parser can handle the given file based on path.
         *
         * @param path Path to the trace file.
         * @return True if this parser might be able to parse the file.
         */
        [[nodiscard]] virtual bool can_parse(const fs::path& path) const = 0;

        /**
         * Checks if this parser can handle the given content.
         *
         * Performs a quick inspection of the content to determine if
         * this parser can handle it.
         *
         * @param content The file content to inspect.
         * @return True if this parser can parse the content.
         */
        [[nodiscard]] virtual bool can_parse_content(std::string_view content) const = 0;

        /**
         * Parses a trace file into a compilation unit.
         *
         * @param path Path to the trace file.
         * @return The parsed compilation unit or an error.
         */
        [[nodiscard]] virtual Result<CompilationUnit, Error> parse_file(
            const fs::path& path
        ) const = 0;

        /**
         * Parses trace content directly.
         *
         * @param content The trace content.
         * @param source_hint Optional hint about the source file.
         * @return The parsed compilation unit or an error.
         */
        [[nodiscard]] virtual Result<CompilationUnit, Error> parse_content(
            std::string_view content,
            const fs::path& source_hint
        ) const = 0;

        /**
         * Returns whether this parser supports streaming for large files.
         */
        [[nodiscard]] virtual bool supports_streaming() const noexcept {
            return false;
        }

        /**
         * Parses a trace file in streaming mode.
         *
         * For very large trace files, this allows processing events
         * incrementally without loading the entire file into memory.
         *
         * @param path Path to the trace file.
         * @param template_callback Callback called for each parsed TemplateInstantiation.
         * @param include_callback Callback called for each parsed IncludeInfo.
         * @return Result indicating success or an error.
         */
        [[nodiscard]] virtual Result<void, Error> parse_streaming(
            const fs::path& path,
            const std::function<void(const TemplateInstantiation&)> &template_callback,
            const std::function<void(const IncludeInfo&)> &include_callback
        ) const {
            (void)path;
            (void)template_callback;
            (void)include_callback;
            return Result<void, Error>::failure(
                Error::internal_error("Streaming not supported")
            );
        }
    };

    /**
     * Factory for creating appropriate parsers based on file content.
     */
    class ParserRegistry {
    public:
        /**
         * Gets the singleton registry instance.
         */
        static ParserRegistry& instance();

        /**
         * Registers a parser.
         *
         * @param parser The parser to register.
         */
        void register_parser(std::unique_ptr<ITraceParser> parser);

        /**
         * Finds a parser that can handle the given file.
         *
         * @param path Path to the trace file.
         * @return A parser that can handle the file, or nullptr.
         */
        [[nodiscard]] ITraceParser* find_parser_for_file(const fs::path& path) const;

        /**
         * Finds a parser that can handle the given content.
         *
         * @param content The trace content.
         * @return A parser that can handle the content, or nullptr.
         */
        [[nodiscard]] ITraceParser* find_parser_for_content(std::string_view content) const;

        /**
         * Gets a parser by compiler type.
         *
         * @param type The compiler type.
         * @return A parser for that compiler, or nullptr.
         */
        [[nodiscard]] ITraceParser* get_parser(CompilerType type) const;

        /**
         * Lists all registered parsers.
         */
        [[nodiscard]] std::vector<ITraceParser*> list_parsers() const;

    private:
        ParserRegistry() = default;
        std::vector<std::unique_ptr<ITraceParser>> parsers_;
    };

    /**
     * Helper to parse a single trace file with auto-detection.
     *
     * @param path Path to the trace file.
     * @return The parsed compilation unit or an error.
     */
    [[nodiscard]] inline Result<CompilationUnit, Error> parse_trace_file(const fs::path& path) {
        const auto* parser = ParserRegistry::instance().find_parser_for_file(path);
        if (!parser) {
            return Result<CompilationUnit, Error>::failure(
                Error::not_found("No parser found for file", path.string())
            );
        }
        return parser->parse_file(path);
    }

    /**
     * Helper to parse multiple trace files in parallel.
     *
     * @param paths Paths to trace files.
     * @return Vector of results (one per file).
     */
    [[nodiscard]] std::vector<Result<CompilationUnit, Error>> parse_trace_files(
        const std::vector<fs::path>& paths
    );

    /**
     * Returns all file extensions supported by registered parsers.
     *
     * This includes extensions for all compiler trace formats:
     * - Clang: .json (-ftime-trace)
     * - GCC: .txt, .log, .report (-ftime-report)
     * - MSVC: .txt, .log, .btlog (/Bt+ /d1reportTime)
     *
     * @return Set of supported extensions (e.g., {".json", ".txt", ".log"}).
     */
    [[nodiscard]] std::vector<std::string> get_supported_trace_extensions();

    /**
     * Checks if a file extension is a supported trace format.
     *
     * @param ext The file extension (with or without leading dot).
     * @return True if the extension is supported.
     */
    [[nodiscard]] bool is_supported_trace_extension(std::string_view ext);

    /**
     * Collects all trace files from a path (file or directory).
     *
     * If path is a file, returns it directly (if supported extension).
     * If path is a directory, recursively finds all supported trace files.
     *
     * @param path File or directory path.
     * @param recursive Whether to search subdirectories (default: true).
     * @return List of trace file paths.
     */
    [[nodiscard]] std::vector<fs::path> collect_trace_files(
        const fs::path& path,
        bool recursive = true
    );

}  // namespace bha::parsers

#endif //BUILDTIMEHOTSPOTANALYZER_PARSER_HPP