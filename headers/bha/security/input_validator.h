//
// Created by gregorian on 28/10/2025.
//

#ifndef INPUT_VALIDATOR_H
#define INPUT_VALIDATOR_H

#include "bha/core/result.h"
#include <string>
#include <vector>

namespace bha::security {

    /**
     * Provides input validation utilities for file paths, JSON structures, and trace files.
     *
     * This class helps prevent security issues such as path traversal, symlink abuse,
     * and malformed or malicious input files. It validates paths, file sizes, and
     * JSON depth limits before they are processed by the build analysis system.
     */
    class InputValidator {
    public:
        /**
         * Configuration options controlling input validation rules.
         */
        struct ValidationOptions {
            size_t max_file_size_mb = 1024;           ///< Maximum allowed file size in megabytes.
            size_t max_path_length = 4096;            ///< Maximum allowed length of a file path.
            size_t max_json_depth = 100;              ///< Maximum allowed depth for JSON documents.
            bool allow_absolute_paths = true;         ///< Whether absolute paths are permitted.
            bool allow_symlinks = false;              ///< Whether symbolic links are permitted.
            std::vector<std::string> allowed_directories; ///< List of directories considered safe.
            std::vector<std::string> blocked_patterns;    ///< List of disallowed path patterns (wildcards or regexes).
        };

        /**
         * Construct a new InputValidator instance.
         * @param options Configuration options controlling validation behavior.
         */
        explicit InputValidator(const ValidationOptions& options);

        /**
         * Validate and normalize a file path.
         *
         * Ensures the path is safe, not blocked, and conforms to allowed directory and length constraints.
         *
         * @param path The path to validate.
         * @return A normalized and validated path, or an error if invalid.
         */
        [[nodiscard]] core::Result<std::string> validate_file_path(const std::string& path) const;

        /**
         * Validate file size against configured limits.
         *
         * @param path Path to the file to check.
         * @return Success or an error if the file exceeds the maximum size.
         */
        [[nodiscard]] core::Result<void> validate_file_size(const std::string& path) const;

        /**
         * Validate the structure and depth of a JSON file.
         *
         * Ensures the JSON file exists, is syntactically valid, and does not exceed maximum depth.
         *
         * @param json_path Path to the JSON file.
         * @return Success or an error if invalid or exceeds allowed complexity.
         */
        [[nodiscard]] core::Result<void> validate_json_structure(const std::string& json_path) const;

        /**
         * Validate a build trace file before processing.
         *
         * Checks file size, extension, path safety, and format suitability for analysis.
         *
         * @param path Path to the trace file.
         * @return Success or error if the trace is unsafe or malformed.
         */
        [[nodiscard]] core::Result<void> validate_trace_file(const std::string& path) const;

        /**
         * Check if a path is considered safe according to current options.
         *
         * @param path Path to evaluate.
         * @return True if safe, false otherwise.
         */
        [[nodiscard]] bool is_safe_path(const std::string& path) const;

        /**
         * Check if a path contains traversal sequences (e.g., "../").
         *
         * @param path Path to inspect.
         * @return True if traversal detected, false otherwise.
         */
        static bool contains_path_traversal(const std::string& path);

        /**
         * Check if the path matches any blocked pattern.
         *
         * @param path Path to test.
         * @return True if it matches a blocked pattern, false otherwise.
         */
        [[nodiscard]] bool matches_blocked_pattern(const std::string& path) const;

        /**
         * Check whether the path is within the allowed directories.
         *
         * @param path Path to check.
         * @return True if the path is inside an allowed directory.
         */
        [[nodiscard]] bool is_within_allowed_directories(const std::string& path) const;

    private:
        ValidationOptions options_; ///< Validation configuration.

        /**
         * Normalize a filesystem path by resolving redundant components.
         *
         * @param path Raw input path.
         * @return Normalized path string.
         */
        static std::string normalize_path(const std::string& path);

        /**
         * Check whether a given path is a symbolic link.
         *
         * @param path Path to evaluate.
         * @return True if the path is a symlink.
         */
        static bool is_symbolic_link(const std::string& path);
    };

} // namespace bha::security


#endif //INPUT_VALIDATOR_H
