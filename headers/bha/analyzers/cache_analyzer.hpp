// Created by gregorian-rayne on 8/22/26.

#ifndef BHA_CACHE_ANALYZER_HPP
#define BHA_CACHE_ANALYZER_HPP

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers {

    /**
     * Reports exact cache counters attached to a build trace.
     */
    class CacheAnalyzer final : public IAnalyzer {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "CacheAnalyzer";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyzes producer-observed compiler-cache outcomes";
        }

        [[nodiscard]] Result<AnalysisResult, Error> analyze(
            const BuildTrace& trace,
            const AnalysisOptions& options
        ) const override;
    };

    void register_cache_analyzer();

}  // namespace bha::analyzers

#endif // BHA_CACHE_ANALYZER_HPP
