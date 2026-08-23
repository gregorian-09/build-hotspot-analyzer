// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_MODULE_ANALYZER_HPP
#define BHA_MODULE_ANALYZER_HPP

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Analyzes explicit P1689 module dependency rules.
     */
    class ModuleAnalyzer : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "ModuleAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes producer-defined C++ module dependency rules";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_module_analyzer();

}  // namespace bha::analyzers

#endif // BHA_MODULE_ANALYZER_HPP
