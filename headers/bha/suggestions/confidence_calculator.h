//
// Created by gregorian on 20/10/2025.
//

#ifndef CONFIDENCE_CALCULATOR_H
#define CONFIDENCE_CALCULATOR_H

namespace bha::suggestions {

    /**
     * @class ConfidenceCalculator
     * Provides confidence scoring utilities for suggestion accuracy estimation.
     *
     * This class computes confidence scores (0.0–1.0) for different suggestion types,
     * such as forward declarations, PCH inclusion, include removal, and PIMPL opportunities.
     * The scores reflect how likely a suggestion is to be correct or beneficial based
     * on project metrics and contextual heuristics.
     */
    class ConfidenceCalculator {
    public:
        /** Default constructor. */
        ConfidenceCalculator() = default;

        /**
         * Calculates confidence for a forward declaration suggestion.
         *
         * The score increases when the class is used via pointers or references (indicating
         * a forward declaration is sufficient) and decreases when used by value (requiring
         * a full definition).
         *
         * @param used_by_pointer True if the class is used via pointers.
         * @param used_by_reference True if the class is used via references.
         * @param used_by_value True if the class is used by value.
         * @param usage_count Number of times the class appears in the file.
         * @return Confidence score between 0.0 and 1.0.
         */
        [[nodiscard]] static double calculate_forward_declaration_confidence(
            bool used_by_pointer,
            bool used_by_reference,
            bool used_by_value,
            int usage_count
        ) ;

        /**
         * Calculates confidence for a header split suggestion.
         *
         * Used to assess how confidently a header can be split into smaller components
         * based on dependency metrics such as dependent count and inclusion depth.
         *
         * @param num_dependents Number of files depending on the header.
         * @param average_include_depth The average inclusion depth in the dependency graph.
         * @return Confidence score between 0.0 and 1.0.
         */
        [[nodiscard]] static double calculate_header_split_confidence(
            int num_dependents,
            double average_include_depth
        ) ;

        /**
         * Calculates confidence for a PCH optimization or inclusion suggestion.
         *
         * Higher inclusion frequency and higher compile time yield higher confidence
         * in suggesting the header for inclusion in the PCH.
         *
         * @param inclusion_count Number of files including the header.
         * @param total_files Total number of translation units in the build.
         * @param compile_time_ms Average compile time for the header (in ms).
         * @param average_file_time_ms Average compile time per file (in ms).
         * @return Confidence score between 0.0 and 1.0.
         */
        [[nodiscard]] static double calculate_pch_confidence(
            int inclusion_count,
            int total_files,
            double compile_time_ms,
            double average_file_time_ms
        );

        /**
         * Calculates confidence for include removal suggestions.
         *
         * Confidence decreases if the include is not transitive or
         * has direct usages in the current file.
         *
         * @param is_transitive True if the include is covered transitively.
         * @param direct_usage_count Number of direct references to entities from this include.
         * @return Confidence score between 0.0 and 1.0.
         */
        [[nodiscard]] static double calculate_include_removal_confidence(
            bool is_transitive,
            int direct_usage_count
        ) ;

        /**
         * Calculates confidence for PIMPL (pointer-to-implementation) refactoring suggestions.
         *
         * Based on the number of private members and the amount of header coupling
         * in the class definition.
         *
         * @param private_member_count Number of private data members in the class.
         * @param included_headers_in_private Number of headers included in the private section.
         * @return Confidence score between 0.0 and 1.0.
         */
        [[nodiscard]] static double calculate_pimpl_confidence(
            int private_member_count,
            int included_headers_in_private
        ) ;

        /**
         * Calculates confidence for moving inline or template-heavy code to source files.
         *
         * Inline or template-heavy code tends to be more confidently suggested for relocation
         * if used in multiple files or has high compilation overhead.
         *
         * @param is_template True if the function or class is a template.
         * @param is_inline True if the code is inline.
         * @param usage_count Number of translation units using the symbol.
         * @return Confidence score between 0.0 and 1.0.
         */
        [[nodiscard]] static double calculate_move_to_cpp_confidence(
            bool is_template,
            bool is_inline,
            int usage_count
        ) ;

        /**
         * Normalizes a raw confidence score into the [0.0, 1.0] range.
         *
         * Ensures that confidence scores remain bounded regardless of heuristic weighting.
         *
         * @param raw_score The unbounded or intermediate confidence value.
         * @return A normalized confidence score between CONFIDENCE_MIN and CONFIDENCE_MAX.
         */
        [[nodiscard]] static double normalize_confidence(double raw_score) ;

    private:
        /** Weight for pointer-based usage in forward declaration scoring. */
        static constexpr double POINTER_WEIGHT = 0.9;

        /** Weight for reference-based usage in forward declaration scoring. */
        static constexpr double REFERENCE_WEIGHT = 0.85;

        /** Weight for value-based usage in forward declaration scoring. */
        static constexpr double VALUE_WEIGHT = 0.3;

        /** Multiplier applied for high usage frequency. */
        static constexpr double HIGH_USAGE_MULTIPLIER = 1.2;

        /** Minimum allowed confidence score. */
        static constexpr double CONFIDENCE_MIN = 0.0;

        /** Maximum allowed confidence score. */
        static constexpr double CONFIDENCE_MAX = 1.0;
    };

} // namespace bha::suggestions


#endif //CONFIDENCE_CALCULATOR_H
