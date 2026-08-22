// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_LINKER_ANALYZER_HPP
#define BHA_LINKER_ANALYZER_HPP

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    class LinkerAnalyzer final : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "LinkerAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes producer-observed link timing and output sizes";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_linker_analyzer();

}  // namespace bha::analyzers

#endif // BHA_LINKER_ANALYZER_HPP
