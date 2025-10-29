//
// Created by gregorian on 28/10/2025.
//

#ifndef ANONYMIZER_H
#define ANONYMIZER_H

#include "bha/core/types.h"
#include <string>
#include <unordered_map>

namespace bha::security {

    /**
     * Provides anonymization of sensitive information in build traces.
     *
     * This class replaces paths, commit SHAs, and other identifiers in build
     * traces with anonymized equivalents. It helps safely share or store build
     * data without exposing internal directory structures or commit metadata.
     */
    class Anonymizer {
    public:
        /**
         * Configuration options for anonymization behavior.
         */
        struct AnonymizationConfig {
            bool anonymize_paths = true;                     ///< Whether to anonymize file and directory paths.
            bool anonymize_commit_info = true;               ///< Whether to anonymize commit SHAs and related metadata.
            bool preserve_extensions = true;                 ///< Whether to keep original file extensions in anonymized paths.
            bool preserve_directory_structure = true;        ///< Whether to maintain folder hierarchy after anonymization.
            std::string replacement_root = "/project";       ///< Root path used as base for anonymized paths.
            std::vector<std::string> preserve_patterns;      ///< List of glob or regex patterns to skip anonymization.
        };

        /**
         * Construct a new Anonymizer instance.
         * @param config Configuration defining how anonymization should be applied.
         */
        explicit Anonymizer(AnonymizationConfig config);

        /**
         * Anonymize an entire build trace, replacing paths and commit info.
         *
         * @param trace Input trace containing potentially sensitive data.
         * @return An anonymized copy of the input trace.
         */
        core::BuildTrace anonymize_trace(const core::BuildTrace& trace);

        /**
         * Anonymize a single filesystem path.
         *
         * Uses deterministic mapping to ensure that identical paths produce
         * consistent anonymized outputs within a single run.
         *
         * @param path The original filesystem path.
         * @return The anonymized path.
         */
        std::string anonymize_path(const std::string& path);

        /**
         * Anonymize a commit SHA or similar identifier.
         *
         * @param sha Original commit SHA string.
         * @return An anonymized, deterministic substitute.
         */
        std::string anonymize_commit_sha(const std::string& sha);

        /**
         * Clear all internal anonymization mappings.
         *
         * Resets the anonymizer so that future anonymizations start fresh.
         */
        void clear_mapping();

        /**
         * Retrieve the mapping of original paths to anonymized paths.
         *
         * @return A const reference to the internal mapping table.
         */
        const std::unordered_map<std::string, std::string>& get_path_mapping() const;

    private:
        AnonymizationConfig config_;                        ///< Current anonymization configuration.
        std::unordered_map<std::string, std::string> path_mapping_;   ///< Map of original to anonymized paths.
        std::unordered_map<std::string, std::string> commit_mapping_; ///< Map of commit SHAs to anonymized tokens.
        size_t path_counter_ = 0;                           ///< Counter used for generating unique path identifiers.
        size_t commit_counter_ = 0;                         ///< Counter used for generating unique commit identifiers.

        /**
         * Hash a string into a deterministic anonymized token.
         * @param input String to hash.
         * @return A hashed representation suitable for anonymization.
         */
        static std::string hash_string(const std::string& input);

        /**
         * Determine whether a given path should be preserved.
         *
         * Checks the path against `preserve_patterns` and configuration rules.
         *
         * @param path Input path.
         * @return True if the path should not be anonymized.
         */
        bool should_preserve_path(const std::string& path) const;

        /**
         * Generate a new anonymized path from an original one.
         *
         * @param original Original static file path.
         * @return A consistent anonymized path.
         */
        std::string generate_anonymous_path(const std::string& original) const;

        /**
         * Generate a new anonymized commit identifier.
         * @return A unique anonymized commit string.
         */
        static std::string generate_anonymous_commit();
    };

} // namespace bha::security


#endif //ANONYMIZER_H
