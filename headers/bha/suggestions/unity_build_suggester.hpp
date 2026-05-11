//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_UNITY_BUILD_SUGGESTER_HPP
#define BHA_UNITY_BUILD_SUGGESTER_HPP

#include "bha/suggestions/suggester.hpp"

namespace bha::suggestions {

    /**
     * @brief Suggester for unity/jumbo build opportunities.
     *
     * Unity builds (also known as jumbo or batch builds) combine multiple source
     * files into a single translation unit, reducing:
     * - Header parsing overhead (parsed once per unity file instead of per source)
     * - Linker workload (fewer object files)
     * - Template instantiation duplication
     *
     * This suggester analyzes the codebase to identify files that would benefit
     * from being grouped together based on:
     * - Similar include dependencies
     * - Small file sizes
     * - Compatible namespaces
     * - Build time characteristics
     */
    class UnityBuildSuggester : public ISuggester {
    public:
        /// Stable suggester identifier.
        [[nodiscard]] std::string_view name() const noexcept override {
            return "UnityBuildSuggester";
        }

        /// Human-readable behavior summary for UI/CLI surfaces.
        [[nodiscard]] std::string_view description() const noexcept override {
            return "Suggests unity/jumbo build configurations to reduce compile times";
        }

        /// Primary suggestion type emitted by this suggester.
        [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
            return SuggestionType::UnityBuild;
        }

        /**
         * @brief Generate unity-build suggestions for compatible translation-unit groups.
         *
         * @param context Analysis context containing traces, analyzer outputs, and options.
         * @return Suggestion generation result or structured error.
         */
        [[nodiscard]] Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const override;
    };

    /**
     * @brief Register `UnityBuildSuggester` with the global suggester registry.
     */
    void register_unity_build_suggester();

}  // namespace bha::suggestions


#endif //BHA_UNITY_BUILD_SUGGESTER_HPP
