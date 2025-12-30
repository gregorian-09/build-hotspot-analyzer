//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_TEMPLATE_ANALYZER_HPP
#define BHA_TEMPLATE_ANALYZER_HPP

/**
 * @file template_analyzer.hpp
 * @brief Template instantiation analysis.
 *
 * Analyzes template instantiation patterns to identify:
 * - Most expensive template instantiations
 * - Templates instantiated many times
 * - Template instantiation hotspots
 */

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Analyzes template instantiation patterns.
     */
    class TemplateAnalyzer : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "TemplateAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes template instantiation costs and hotspots";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_template_analyzer();

}  // namespace bha::analyzers

#endif //BHA_TEMPLATE_ANALYZER_HPP