//
// Created by gregorian on 15/10/2025.
//

#include "bha/core/error.h"
#include <sstream>
#include <unordered_map>

namespace bha::core {

    Error::Error(const ErrorCode code,
                 std::string message,
                 const ErrorSeverity severity,
                 const std::source_location location)
        : code(code)
        , message(std::move(message))
        , severity(severity)
        , file(location.file_name())
        , line(location.line())
        , function(location.function_name()) {
    }

    Error::Error(const ErrorCode code,
                 std::string message,
                 std::vector<std::string> suggestions,
                 const ErrorSeverity severity,
                 const std::source_location location)
        : code(code)
        , message(std::move(message))
        , severity(severity)
        , file(location.file_name())
        , line(location.line())
        , function(location.function_name())
        , suggestions(std::move(suggestions)) {
    }

    std::string Error::to_string() const {
        std::ostringstream ss;

        switch (severity) {
            case ErrorSeverity::WARNING:
                ss << "[WARNING] ";
                break;
            case ErrorSeverity::ERROR:
                ss << "[ERROR] ";
                break;
            case ErrorSeverity::FATAL:
                ss << "[FATAL] ";
                break;
        }

        ss << error_code_to_string(code) << ": " << message;

        if (!context.empty()) {
            ss << "\n  Context: " << context;
        }

        if (!suggestions.empty()) {
            ss << "\n  Suggestions:";
            for (const auto& suggestion : suggestions) {
                ss << "\n    - " << suggestion;
            }
        }

        ss << "\n  Location: " << file << ":" << line << " in " << function;

        return ss.str();
    }

    bool Error::is_fatal() const {
        return severity == ErrorSeverity::FATAL;
    }

    bool Error::is_recoverable() const {
        return severity == ErrorSeverity::WARNING || severity == ErrorSeverity::ERROR;
    }

    const char* error_code_to_string(ErrorCode code) {
        static const std::unordered_map<ErrorCode, const char*> error_strings = {
            {ErrorCode::SUCCESS, "Success"},

            {ErrorCode::FILE_NOT_FOUND, "File not found"},
            {ErrorCode::FILE_READ_ERROR, "File read error"},
            {ErrorCode::FILE_WRITE_ERROR, "File write error"},
            {ErrorCode::FILE_PARSE_ERROR, "File parse error"},

            {ErrorCode::INVALID_PATH, "Invalid path"},
            {ErrorCode::INVALID_FORMAT, "Invalid format"},
            {ErrorCode::INVALID_ARGUMENT, "Invalid argument"},
            {ErrorCode::INVALID_CONFIG, "Invalid configuration"},
            {ErrorCode::INVALID_STATE, "Invalid state"},

            {ErrorCode::PARSE_ERROR, "Parse error"},
            {ErrorCode::JSON_PARSE_ERROR, "JSON parse error"},
            {ErrorCode::TRACE_PARSE_ERROR, "Trace parse error"},
            {ErrorCode::UNSUPPORTED_FORMAT, "Unsupported format"},
            {ErrorCode::UNSUPPORTED_COMPILER, "Unsupported compiler"},
            {ErrorCode::MALFORMED_DATA, "Malformed data"},

            {ErrorCode::GRAPH_ERROR, "Graph error"},
            {ErrorCode::CIRCULAR_DEPENDENCY, "Circular dependency detected"},
            {ErrorCode::MISSING_DEPENDENCY, "Missing dependency"},
            {ErrorCode::INVALID_GRAPH, "Invalid graph"},

            {ErrorCode::DATABASE_ERROR, "Database error"},
            {ErrorCode::STORAGE_ERROR, "Storage error"},
            {ErrorCode::QUERY_ERROR, "Query error"},

            {ErrorCode::OUT_OF_MEMORY, "Out of memory"},
            {ErrorCode::RESOURCE_EXHAUSTED, "Resource exhausted"},
            {ErrorCode::TIMEOUT, "Operation timed out"},

            {ErrorCode::ANALYSIS_ERROR, "Analysis error"},
            {ErrorCode::CALCULATION_ERROR, "Calculation error"},

            {ErrorCode::VALIDATION_ERROR, "Validation error"},
            {ErrorCode::SECURITY_ERROR, "Security error"},
            {ErrorCode::PERMISSION_DENIED, "Permission denied"},

            {ErrorCode::NETWORK_ERROR, "Network error"},
            {ErrorCode::CONNECTION_ERROR, "Connection error"},

            {ErrorCode::INTERNAL_ERROR, "Internal error"},
            {ErrorCode::NOT_IMPLEMENTED, "Not implemented"},
            {ErrorCode::UNKNOWN_ERROR, "Unknown error"}
        };

        if (const auto it = error_strings.find(code); it != error_strings.end()) {
            return it->second;
        }
        return "Unknown error code";
    }

    ErrorSeverity error_code_to_severity(const ErrorCode code) {
        switch (code) {
            case ErrorCode::SUCCESS:
                return ErrorSeverity::WARNING;

            case ErrorCode::OUT_OF_MEMORY:
            case ErrorCode::RESOURCE_EXHAUSTED:
            case ErrorCode::INTERNAL_ERROR:
            case ErrorCode::SECURITY_ERROR:
                return ErrorSeverity::FATAL;

            case ErrorCode::FILE_NOT_FOUND:
            case ErrorCode::UNSUPPORTED_FORMAT:
            case ErrorCode::UNSUPPORTED_COMPILER:
                return ErrorSeverity::WARNING;

            default:
                return ErrorSeverity::ERROR;
        }
    }

    Error make_error(const ErrorCode code,
                     std::string message,
                     const std::source_location location) {
        return Error{code, std::move(message), error_code_to_severity(code), location};
    }

    Error make_error_with_suggestions(const ErrorCode code,
                                       std::string message,
                                       std::vector<std::string> suggestions,
                                       const std::source_location location) {
        return Error{code, std::move(message), std::move(suggestions),
                     error_code_to_severity(code), location};
    }

} // namespace bha::core