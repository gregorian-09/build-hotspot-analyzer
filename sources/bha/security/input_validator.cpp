//
// Created by gregorian on 28/10/2025.
//

#include "bha/security/input_validator.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <regex>
#include <simdjson.h>

namespace fs = std::filesystem;

namespace bha::security {

    InputValidator::InputValidator(const ValidationOptions& options)
        : options_(options) {}

    core::Result<std::string> InputValidator::validate_file_path(const std::string& path) const
    {
        if (path.empty()) {
            return core::Result<std::string>::failure(core::Error{
                core::ErrorCode::VALIDATION_ERROR,
                "File path cannot be empty"
            });
        }

        if (path.length() > options_.max_path_length) {
            return core::Result<std::string>::failure(core::Error{
                core::ErrorCode::VALIDATION_ERROR,
                "File path exceeds maximum length of " +
                          std::to_string(options_.max_path_length)
            });
        }

        if (contains_path_traversal(path)) {
            return core::Result<std::string>::failure(core::Error{
                core::ErrorCode::VALIDATION_ERROR,
                "Path traversal detected in: " + path
            });
        }

        if (matches_blocked_pattern(path)) {
            return core::Result<std::string>::failure(core::Error{
                core::ErrorCode::VALIDATION_ERROR,
                "Path matches blocked pattern: " + path
            });
        }

        std::string normalized;
        try {
            normalized = normalize_path(path);
        } catch (const std::exception& e) {
            return core::Result<std::string>::failure(core::Error{
                core::ErrorCode::VALIDATION_ERROR,
                "Failed to normalize path: " + std::string(e.what())
            });
        }

        if (!options_.allow_symlinks && is_symbolic_link(normalized)) {
            return core::Result<std::string>::failure(core::Error{
                core::ErrorCode::VALIDATION_ERROR,
                "Symbolic links not allowed: " + path
            });
        }

        if (!options_.allowed_directories.empty() &&
            !is_within_allowed_directories(normalized)) {
            return core::Result<std::string>::failure(core::Error{
                core::ErrorCode::VALIDATION_ERROR,
                "Path outside allowed directories: " + path
            });
        }

        if (!fs::exists(normalized)) {
            return core::Result<std::string>::failure(core::Error{
                core::ErrorCode::FILE_NOT_FOUND,
                "File does not exist: " + path
            });
        }

        return core::Result<std::string>::success(normalized);
    }

    core::Result<void> InputValidator::validate_file_size(const std::string& path) const
    {
        std::error_code ec;
        const auto file_size = fs::file_size(path, ec);

        if (ec) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::FILE_READ_ERROR,
                "Failed to get file size: " + ec.message()
            });
        }

        if (const size_t max_bytes = options_.max_file_size_mb * 1024 * 1024; file_size > max_bytes) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::VALIDATION_ERROR,
                "File too large: " + std::to_string(file_size / (1024 * 1024)) +
                          "MB (max: " + std::to_string(options_.max_file_size_mb) + "MB)"
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> InputValidator::validate_json_structure(const std::string& json_path) {
        std::ifstream file(json_path);
        if (!file.is_open()) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::FILE_NOT_FOUND,
                "Cannot open file: " + json_path
            });
        }

        std::string content(
            (std::istreambuf_iterator(file)),
            std::istreambuf_iterator<char>()
        );

        try {
            simdjson::dom::parser parser;
            simdjson::dom::element doc = parser.parse(content);

            (void)doc;
        } catch (const simdjson::simdjson_error& e)
        {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::PARSE_ERROR,
                e.what()
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> InputValidator::validate_trace_file(const std::string& path) const
    {
        auto path_result = validate_file_path(path);
        if (!path_result.is_success()) {
            return core::Result<void>::failure(path_result.error());
        }

        if (auto size_result = validate_file_size(path_result.value()); !size_result.is_success()) {
            return size_result;
        }

        if (auto json_result = validate_json_structure(path_result.value()); !json_result.is_success()) {
            return json_result;
        }

        return core::Result<void>::success();
    }

    bool InputValidator::is_safe_path(const std::string& path) const {
        if (contains_path_traversal(path)) {
            return false;
        }

        if (matches_blocked_pattern(path)) {
            return false;
        }

        if (!options_.allowed_directories.empty()) {
            try {
                const std::string normalized = normalize_path(path);
                return is_within_allowed_directories(normalized);
            } catch (...) {
                return false;
            }
        }

        return true;
    }

    bool InputValidator::contains_path_traversal(const std::string& path)
    {
        if (path.find("..") != std::string::npos) {
            return true;
        }

        if (path.find("./") != std::string::npos || path.find("/.") != std::string::npos) {
            std::string normalized = path;
            std::ranges::replace(normalized, '\\', '/');

            if (normalized.find("/../") != std::string::npos ||
                normalized.find("/./") != std::string::npos) {
                return true;
            }
        }

        return false;
    }

    bool InputValidator::matches_blocked_pattern(const std::string& path) const {
        return std::ranges::any_of(options_.blocked_patterns, [&](const std::string& pattern) {
            try {
                const std::regex rx(glob_to_regex(pattern), std::regex::ECMAScript);
                return std::regex_search(path, rx);
            } catch (...) {
                return false;
            }
        });
    }

    bool InputValidator::is_within_allowed_directories(const std::string& path) const {
        if (options_.allowed_directories.empty()) {
            return true;
        }

        try {
            fs::path normalized = fs::weakly_canonical(path);

            for (const auto& allowed_dir : options_.allowed_directories) {
                fs::path allowed = fs::weakly_canonical(allowed_dir);

                auto [it1, it2] = std::mismatch(
                    allowed.begin(), allowed.end(),
                    normalized.begin(), normalized.end()
                );

                if (it1 == allowed.end()) {
                    return true;
                }
            }
        } catch (...) {
            return false;
        }

        return false;
    }

    std::string InputValidator::normalize_path(const std::string& path)
    {
        return fs::weakly_canonical(path).string();
    }

    std::string InputValidator::glob_to_regex(const std::string& glob) {
        std::string rx;
        rx.reserve(glob.size() * 2);

        rx += "^";
        for (const char c : glob) {
            switch (c) {
            case '*': rx += ".*"; break;
            case '?': rx += "."; break;
            case '.': rx += "\\."; break;
            case '/': rx += "/"; break;
            default:
                if (std::isalnum(static_cast<unsigned char>(c)))
                    rx += c;
                else {
                    rx += "\\";
                    rx += c;
                }
            }
        }
        rx += "$";
        return rx;
    }


    bool InputValidator::is_symbolic_link(const std::string& path)
    {
        std::error_code ec;
        return fs::is_symlink(path, ec);
    }

} // namespace bha::security