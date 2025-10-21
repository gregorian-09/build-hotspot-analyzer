//
// Created by gregorian on 20/10/2025.
//

#ifndef FORWARD_DECL_SUGGESTER_H
#define FORWARD_DECL_SUGGESTER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <string>

namespace bha::suggestions {

    /**
     * @struct ForwardDeclOpportunity
     * Represents a potential opportunity to replace an include with a forward declaration.
     *
     * This structure captures analysis data used to determine when a class include can be
     * safely replaced with a forward declaration, potentially reducing build dependencies
     * and compile times.
     */
    struct ForwardDeclOpportunity {
        /** The name of the class that could be forward declared. */
        std::string class_name;

        /** The file currently being included that provides the full class definition. */
        std::string include_file;

        /** The source file in which the opportunity was detected. */
        std::string current_location;

        /** The number of times the class appears in the analyzed file. */
        int usage_count;

        /** Indicates if the class is used via pointer (safe for forward declaration). */
        bool used_by_pointer;

        /** Indicates if the class is used via reference (typically safe for forward declaration). */
        bool used_by_reference;

        /** Indicates if the class is used by value (requires full definition). */
        bool used_by_value;

        /** A computed confidence score (0.0–1.0) representing the likelihood the suggestion is valid. */
        double confidence;

        /** Estimated compile-time savings in milliseconds if the forward declaration is applied. */
        double estimated_savings_ms;
    };

    /**
     * @class ForwardDeclSuggester
     * Provides suggestions for replacing includes with forward declarations.
     *
     * The ForwardDeclSuggester analyzes include and class usage patterns within source files
     * to detect potential opportunities to replace `#include` directives with forward declarations.
     * This helps minimize unnecessary rebuilds and improve incremental compilation performance.
     */
    class ForwardDeclSuggester {
    public:
        /** Default constructor. */
        ForwardDeclSuggester() = default;

        /**
         * Generates suggestions for introducing forward declarations in a given source file.
         *
         * Analyzes the file to identify classes that are only used by pointer or reference
         * and can therefore be forward declared.
         *
         * @param file_path Path to the source file being analyzed.
         * @param trace The build trace containing timing and dependency information.
         * @return A Result containing a list of @ref core::Suggestion objects.
         */
        static core::Result<std::vector<core::Suggestion>> suggest_forward_declarations(
            const std::string& file_path,
            const core::BuildTrace& trace
        );

        /**
         * Analyzes include usage in a source file to detect forward declaration opportunities.
         *
         * Examines included headers and class usage patterns to identify which includes can be
         * replaced with forward declarations without breaking the build.
         *
         * @param file_path The path to the file being analyzed.
         * @param trace The build trace for timing context.
         * @return A Result containing a list of @ref ForwardDeclOpportunity entries.
         */
        static core::Result<std::vector<ForwardDeclOpportunity>> analyze_includes(
            const std::string& file_path,
            const core::BuildTrace& trace
        );

        /**
         * Extracts all class names defined or referenced in a given file.
         *
         * @param file_path Path to the file to parse.
         * @return A Result containing a list of class names.
         */
/        static core::Result<std::vector<std::string>> extract_classes(
            const std::string& file_path
        );

        /**
         * Extracts all include directives from a given file.
         *
         * @param file_path Path to the file to analyze.
         * @return A Result containing a list of include file paths.
         */
        static core::Result<std::vector<std::string>> extract_includes(
            const std::string& file_path
        );

        /**
         * Computes a confidence score for a detected forward declaration opportunity.
         *
         * The confidence is derived from usage type, frequency, and context patterns.
         *
         * @param opportunity The opportunity to evaluate.
         * @return A floating-point confidence score between 0.0 and 1.0.
         */
        [[nodiscard]] static double calculate_confidence(
            const ForwardDeclOpportunity& opportunity
        ) ;

        /**
         * Estimates the compile-time savings achievable by removing an include.
         *
         * @param include_file The header file that could be forward declared.
         * @param trace The build trace with timing data.
         * @return A Result containing the estimated time savings in milliseconds.
         */
        static core::Result<double> estimate_time_savings(
            const std::string& include_file,
            const core::BuildTrace& trace
        );

    private:
        /**
         * Determines if a class is used by pointer in the given file.
         *
         * @param file_content The contents of the source file.
         * @param class_name The class name to search for.
         * @return True if the class is used by pointer, false otherwise.
         */
        [[nodiscard]] static bool is_class_used_by_pointer(
            const std::string& file_content,
            const std::string& class_name
        );

        /**
         * Determines if a class is used by reference in the given file.
         *
         * @param file_content The contents of the source file.
         * @param class_name The class name to search for.
         * @return True if the class is used by reference, false otherwise.
         */
        [[nodiscard]] static bool is_class_used_by_reference(
            const std::string& file_content,
            const std::string& class_name
        );

        /**
         * Determines if a class is used by value in the given file.
         *
         * @param file_content The contents of the source file.
         * @param class_name The class name to search for.
         * @return True if the class is used by value, false otherwise.
         */
        [[nodiscard]] static bool is_class_used_by_value(
            const std::string& file_content,
            const std::string& class_name
        );

        /**
         * Determines whether a full class definition is required for correct compilation.
         *
         * @param file_content The file's source code.
         * @param class_name The class name to analyze.
         * @return True if the full definition is needed, false otherwise.
         */
        [[nodiscard]] static bool is_full_definition_needed(
            const std::string& file_content,
            const std::string& class_name
        );

        /**
         * Counts how many times a given class name appears in a source file.
         *
         * @param file_content The file's contents.
         * @param class_name The class to count.
         * @return The number of occurrences found.
         */
        [[nodiscard]] static int count_usage(
            const std::string& file_content,
            const std::string& class_name
        );
    };

} // namespace bha::suggestions

#endif //FORWARD_DECL_SUGGESTER_H
