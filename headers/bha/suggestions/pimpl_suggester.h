//
// Created by gregorian on 20/10/2025.
//

#ifndef PIMPL_SUGGESTER_H
#define PIMPL_SUGGESTER_H

#include <string>
#include <vector>
#include "bha/core/result.h"

namespace bha::core
{
    struct Suggestion;
}

namespace bha::suggestions {

    /**
     * @class PIMPLSuggester
     * Suggests when a class can be refactored using the PIMPL (pointer to implementation) idiom.
     *
     * This tool analyzes classes in a translation unit and identifies opportunities where
     * applying the PIMPL pattern (moving implementation details into a private pointer) could
     * reduce compile-time dependencies and improve encapsulation.
     */
    class PIMPLSuggester {
    public:
        /** Default constructor. */
        PIMPLSuggester() = default;
        /** Default destructor. */
        ~PIMPLSuggester() = default;

        /**
         * Suggests PIMPL refactorings for the classes in a given source file.
         *
         * Inspects classes in `file_path` and produces a list of suggestions for which
         * classes would benefit from applying the PIMPL idiom, with estimated compile-time
         * savings and confidence scores.
         *
         * @param file_path Path to the source file to analyze.
         * @return A Result wrapping a vector of @ref core::Suggestion describing PIMPL options.
         */
        static core::Result<std::vector<core::Suggestion>> suggest_pimpl_patterns(
            const std::string& file_path
        );

    private:
        /**
         * @struct ClassAnalysis
         * Data collected when analyzing a class for PIMPL candidacy.
         *
         * Holds metrics needed to judge whether converting a class to PIMPL is likely beneficial.
         */
        struct ClassAnalysis {
            std::string class_name;            ///< Name of the class being analyzed.
            size_t private_members_count;      ///< Count of private data members.
            size_t private_includes_count;     ///< Number of headers included in the private section.
            double estimated_savings_ms;       ///< Estimated compile-time savings (ms) if PIMPL is applied.
        };

        /**
         * Analyze a specific class within a file for PIMPL candidacy.
         *
         * This inspects things like number of private members and included headers
         * to compute a potential savings estimate.
         *
         * @param file_path Path of the source file containing the class.
         * @param class_name Name of the class to analyze.
         * @return A @ref ClassAnalysis object with metrics for that class.
         */
        static ClassAnalysis analyze_class_for_pimpl(
            const std::string& file_path,
            const std::string& class_name
        );

        /**
         * Determines whether a class is a good candidate for PIMPL conversion.
         *
         * Based on metrics in @ref ClassAnalysis (e.g. many private members and includes),
         * returns true if PIMPL is likely to yield benefits.
         *
         * @param analysis The metrics generated from `analyze_class_for_pimpl`.
         * @return `true` if PIMPL is recommended, `false` otherwise.
         */
        static bool is_pimpl_candidate(const ClassAnalysis& analysis);
    };

} // namespace bha::suggestions

#endif //PIMPL_SUGGESTER_H
