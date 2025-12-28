//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_ERROR_HPP
#define BUILDTIMEHOTSPOTANALYZER_ERROR_HPP

/**
 * @file error.hpp
 * @brief Error types and error handling utilities.
 *
 * Provides a structured error type that carries error codes, messages,
 * and optional context information. Designed to work with Result<T, Error>
 * for explicit error handling throughout the codebase.
 *
 * Error categories:
 * - None: No error (success state)
 * - InvalidArgument: Invalid function arguments or parameters
 * - NotFound: Requested resource not found
 * - ParseError: Failed to parse input data
 * - IoError: File system or I/O operation failed
 * - ConfigError: Configuration validation failed
 * - AnalysisError: Build analysis failed
 * - PluginError: Plugin loading or execution failed
 * - GitError: Git operation failed
 * - InternalError: Unexpected internal error
 *
 * Usage:
 * @code
 *     Result<Data, Error> parse_file(const std::string& path) {
 *         if (!fs::exists(path)) {
 *             return Result<Data, Error>::failure(
 *                 Error::not_found("File not found", path)
 *             );
 *         }
 *         // ...
 *     }
 *
 *     auto result = parse_file("trace.json");
 *     if (result.is_err()) {
 *         std::cerr << result.error() << std::endl;
 *         // Output: [NotFound] File not found (context: trace.json)
 *     }
 * @endcode
 */

#include <string>
#include <optional>
#include <ostream>
#include <utility>

namespace bha {

    /**
     * Error category enumeration.
     *
     * Each category represents a broad class of errors that can occur
     * during build analysis operations.
     */
    enum class ErrorCode {
        None,             ///< No error
        InvalidArgument,  ///< Invalid argument or parameter
        NotFound,         ///< Resource not found
        ParseError,       ///< Parsing failed
        IoError,          ///< I/O operation failed
        ConfigError,      ///< Configuration error
        AnalysisError,    ///< Analysis operation failed
        PluginError,      ///< Plugin operation failed
        GitError,         ///< Git operation failed
        InternalError     ///< Internal/unexpected error
    };

    /**
     * Converts an ErrorCode to its string representation.
     *
     * @param code The error code to convert.
     * @return A string representation of the error code.
     */
    inline const char* error_code_to_string(ErrorCode code) noexcept {
        switch (code) {
            case ErrorCode::None:            return "None";
            case ErrorCode::InvalidArgument: return "InvalidArgument";
            case ErrorCode::NotFound:        return "NotFound";
            case ErrorCode::ParseError:      return "ParseError";
            case ErrorCode::IoError:         return "IoError";
            case ErrorCode::ConfigError:     return "ConfigError";
            case ErrorCode::AnalysisError:   return "AnalysisError";
            case ErrorCode::PluginError:     return "PluginError";
            case ErrorCode::GitError:        return "GitError";
            case ErrorCode::InternalError:   return "InternalError";
        }
        return "Unknown";
    }

    /**
     * Structured error type with code, message, and optional context.
     *
     * Error objects are immutable after construction. They can carry
     * additional context information such as file paths, line numbers,
     * or other relevant details that help diagnose the error.
     */
    class Error {
    public:
        /**
         * Creates an error with the given code and message.
         *
         * @param code The error category.
         * @param message A human-readable error message.
         */
        Error(ErrorCode code, std::string message)
            : code_(code)
            , message_(std::move(message))
            , context_(std::nullopt) {}

        /**
         * Creates an error with code, message, and context.
         *
         * @param code The error category.
         * @param message A human-readable error message.
         * @param context Additional context (e.g., file path, variable name).
         */
        Error(ErrorCode code, std::string message, std::string context)
            : code_(code)
            , message_(std::move(message))
            , context_(std::move(context)) {}

        // Factory methods for common error types

        /**
         * Creates an invalid argument error.
         */
        static Error invalid_argument(std::string message) {
            return {ErrorCode::InvalidArgument, std::move(message)};
        }

        static Error invalid_argument(std::string message, std::string context) {
            return {ErrorCode::InvalidArgument, std::move(message), std::move(context)};
        }

        /**
         * Creates a not found error.
         */
        static Error not_found(std::string message) {
            return {ErrorCode::NotFound, std::move(message)};
        }

        static Error not_found(std::string message, std::string context) {
            return {ErrorCode::NotFound, std::move(message), std::move(context)};
        }

        /**
         * Creates a parse error.
         */
        static Error parse_error(std::string message) {
            return {ErrorCode::ParseError, std::move(message)};
        }

        static Error parse_error(std::string message, std::string context) {
            return {ErrorCode::ParseError, std::move(message), std::move(context)};
        }

        /**
         * Creates an I/O error.
         */
        static Error io_error(std::string message) {
            return {ErrorCode::IoError, std::move(message)};
        }

        static Error io_error(std::string message, std::string context) {
            return {ErrorCode::IoError, std::move(message), std::move(context)};
        }

        /**
         * Creates a configuration error.
         */
        static Error config_error(std::string message) {
            return {ErrorCode::ConfigError, std::move(message)};
        }

        static Error config_error(std::string message, std::string context) {
            return {ErrorCode::ConfigError, std::move(message), std::move(context)};
        }

        /**
         * Creates an analysis error.
         */
        static Error analysis_error(std::string message) {
            return {ErrorCode::AnalysisError, std::move(message)};
        }

        static Error analysis_error(std::string message, std::string context) {
            return {ErrorCode::AnalysisError, std::move(message), std::move(context)};
        }

        /**
         * Creates a plugin error.
         */
        static Error plugin_error(std::string message) {
            return {ErrorCode::PluginError, std::move(message)};
        }

        static Error plugin_error(std::string message, std::string context) {
            return {ErrorCode::PluginError, std::move(message), std::move(context)};
        }

        /**
         * Creates a Git error.
         */
        static Error git_error(std::string message) {
            return {ErrorCode::GitError, std::move(message)};
        }

        static Error git_error(std::string message, std::string context) {
            return {ErrorCode::GitError, std::move(message), std::move(context)};
        }

        /**
         * Creates an internal error.
         */
        static Error internal_error(std::string message) {
            return {ErrorCode::InternalError, std::move(message)};
        }

        static Error internal_error(std::string message, std::string context) {
            return {ErrorCode::InternalError, std::move(message), std::move(context)};
        }

        /**
         * Returns the error code.
         */
        [[nodiscard]] ErrorCode code() const noexcept {
            return code_;
        }

        /**
         * Returns the error message.
         */
        [[nodiscard]] const std::string& message() const noexcept {
            return message_;
        }

        /**
         * Returns the optional context string.
         */
        [[nodiscard]] const std::optional<std::string>& context() const noexcept {
            return context_;
        }

        /**
         * Checks if this error has associated context.
         */
        [[nodiscard]] bool has_context() const noexcept {
            return context_.has_value();
        }

        /**
         * Creates a new error with additional context appended.
         *
         * @param additional_context Context to append.
         * @return A new Error with combined context.
         */
        [[nodiscard]] Error with_context(std::string additional_context) const {
            if (context_.has_value()) {
                return {code_, message_, *context_ + "; " + std::move(additional_context)};
            }
            return {code_, message_, std::move(additional_context)};
        }

        /**
         * Formats the error as a string.
         *
         * Format: "[ErrorCode] message" or "[ErrorCode] message (context: ...)"
         *
         * @return Formatted error string.
         */
        [[nodiscard]] std::string to_string() const {
            std::string result = "[";
            result += error_code_to_string(code_);
            result += "] ";
            result += message_;
            if (context_.has_value()) {
                result += " (context: ";
                result += *context_;
                result += ")";
            }
            return result;
        }

        /**
         * Equality comparison.
         */
        bool operator==(const Error& other) const {
            return code_ == other.code_ &&
                   message_ == other.message_ &&
                   context_ == other.context_;
        }

        bool operator!=(const Error& other) const {
            return !(*this == other);
        }

    private:
        ErrorCode code_;
        std::string message_;
        std::optional<std::string> context_;
    };

    /**
     * Stream insertion operator for Error.
     */
    inline std::ostream& operator<<(std::ostream& os, const Error& error) {
        return os << error.to_string();
    }

    /**
     * Stream insertion operator for ErrorCode.
     */
    inline std::ostream& operator<<(std::ostream& os, ErrorCode code) {
        return os << error_code_to_string(code);
    }

}  // namespace bha

#endif //BUILDTIMEHOTSPOTANALYZER_ERROR_HPP