//
// Created by gregorian on 15/10/2025.
//

#ifndef ERROR_H
#define ERROR_H

#include <cstdint>
#include <source_location>
#include <string>
#include <vector>

namespace bha::core {

    /**
     * Codes representing different kinds of errors, from file I/O to parsing, validation, and internal failures.
     */
    enum class ErrorCode {
        SUCCESS = 0,

        FILE_NOT_FOUND,
        FILE_READ_ERROR,
        FILE_WRITE_ERROR,
        FILE_PARSE_ERROR,

        INVALID_PATH,
        INVALID_FORMAT,
        INVALID_ARGUMENT,
        INVALID_CONFIG,
        INVALID_STATE,

        PARSE_ERROR,
        JSON_PARSE_ERROR,
        TRACE_PARSE_ERROR,
        UNSUPPORTED_FORMAT,
        UNSUPPORTED_COMPILER,
        MALFORMED_DATA,

        GRAPH_ERROR,
        CIRCULAR_DEPENDENCY,
        MISSING_DEPENDENCY,
        INVALID_GRAPH,

        DATABASE_ERROR,
        STORAGE_ERROR,
        QUERY_ERROR,

        OUT_OF_MEMORY,
        RESOURCE_EXHAUSTED,
        TIMEOUT,

        ANALYSIS_ERROR,
        CALCULATION_ERROR,

        VALIDATION_ERROR,
        SECURITY_ERROR,
        PERMISSION_DENIED,

        NETWORK_ERROR,
        CONNECTION_ERROR,

        INTERNAL_ERROR,
        NOT_IMPLEMENTED,
        UNKNOWN_ERROR
    };

    /**
     * Severity levels for errors or warnings.
     */
    enum class ErrorSeverity {
        WARNING,
        ERROR,
        FATAL
    };

    /**
     * Represents an error condition, with code, message, location, and optional suggestions / context.
     */
    struct Error {
        ErrorCode code{};                ///<example></example> The error code.
        std::string message{};           ///< Human-readable message describing the error.
        ErrorSeverity severity{};        ///< Severity level of this error.

        std::string file{};              ///< Source file in which the error was reported.
        uint_least32_t line{};           ///< Line number in the source file.
        std::string function{};          ///< Function name in which the error was reported.

        std::vector<std::string> suggestions{};  ///< Optional suggestions or fixes.
        std::string context{};                   ///< Optional additional context or metadata.

        Error() = default;

        /**
         * Construct an error with code, message, severity, and source location inferred.
         *
         * @param code The error code.
         * @param message Description of what went wrong.
         * @param severity The severity (default is ERROR).
         * @param location Source location (default is current location).
         */
        Error(ErrorCode code,
              std::string message,
              ErrorSeverity severity = ErrorSeverity::ERROR,
              std::source_location location = std::source_location::current());

        /**
         * Construct an error including suggestions.
         *
         * @param code The error code.
         * @param message Description of what went wrong.
         * @param suggestions List of suggestion strings the user or system can try.
         * @param severity The severity (default is ERROR).
         * @param location Source location (default is current location).
         */
        Error(ErrorCode code,
              std::string message,
              std::vector<std::string> suggestions,
              ErrorSeverity severity = ErrorSeverity::ERROR,
              std::source_location location = std::source_location::current());

        /**
         * Get a human-readable representation of the error, including code, location, message, etc.
         *
         * @return A string summarizing the error.
         */
        [[nodiscard]] std::string to_string() const;

        /**
         * Indicates whether the error is fatal (non-recoverable).
         *
         * @return True if the error’s severity is FATAL, false otherwise.
         */
        [[nodiscard]] bool is_fatal() const;

        /**
         * Indicates whether the error is recoverable (i.e. non-fatal).
         *
         * @return True if the error is not fatal (severity WARNING or ERROR), false otherwise.
         */
        [[nodiscard]] bool is_recoverable() const;
    };

    /**
     * Convert an ErrorCode enum value to its string representation.
     *
     * @param code The `ErrorCode` to convert.
     * @return A C-string (null-terminated) name of the code.
     */
    const char* error_code_to_string(ErrorCode code);

    /**
     * Map an error code to a default severity level.
     *
     * @param code The `ErrorCode`.
     * @return The default `ErrorSeverity` for that code.
     */
    ErrorSeverity error_code_to_severity(ErrorCode code);

    /**
     * Create an `Error` object from code and message, inferring source location.
     *
     * @param code The error code.
     * @param message Description of the error.
     * @param location Source location (default is current).
     * @return An `Error` instance.
     */
    Error make_error(ErrorCode code,
                     std::string message,
                     std::source_location location = std::source_location::current());

    /**
     * Create an `Error` object with suggestions.
     *
     * @param code The error code.
     * @param message Description of the error.
     * @param suggestions A list of suggestion strings for remediation.
     * @param location Source location (default is current).
     * @return An `Error` instance including suggestions.
     */
    Error make_error_with_suggestions(ErrorCode code,
                                      std::string message,
                                      std::vector<std::string> suggestions,
                                      std::source_location location = std::source_location::current());

}  // namespace bha::core

#endif //ERROR_H
