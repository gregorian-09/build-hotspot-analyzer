//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/analyzer.hpp"

namespace bha::analyzers
{
    AnalyzerRegistry& AnalyzerRegistry::instance() {
        static AnalyzerRegistry registry;
        return registry;
    }

    void AnalyzerRegistry::register_analyzer(std::unique_ptr<IAnalyzer> analyzer) {
        analyzers_.push_back(std::move(analyzer));
    }

    IAnalyzer* AnalyzerRegistry::get_analyzer(std::string_view name) const {
        for (const auto& analyzer : analyzers_) {
            if (analyzer->name() == name) {
                return analyzer.get();
            }
        }
        return nullptr;
    }

    std::vector<IAnalyzer*> AnalyzerRegistry::list_analyzers() const {
        std::vector<IAnalyzer*> result;
        result.reserve(analyzers_.size());

        for (const auto& analyzer : analyzers_) {
            result.push_back(analyzer.get());
        }

        return result;
    }

    Result<AnalysisResult, Error> run_full_analysis(
        const BuildTrace& trace,
        const AnalysisOptions& options
    ) {
        AnalysisResult combined_result;
        const auto start_time = std::chrono::steady_clock::now();

        for (const auto analyzers = AnalyzerRegistry::instance().list_analyzers(); const auto* analyzer : analyzers) {
            auto result = analyzer->analyze(trace, options);

            if (result.is_err()) {
                continue;
            }

            auto& partial = result.value();

            if (!partial.files.empty()) {
                combined_result.files = std::move(partial.files);
            }

            if (partial.performance.total_build_time != Duration::zero()) {
                combined_result.performance = std::move(partial.performance);
            }

            if (!partial.dependencies.headers.empty()) {
                // If we already have stats, preserve them while merging headers
                if (combined_result.dependencies.total_includes > 0 ||
                    combined_result.dependencies.unique_headers > 0) {
                    // Append new headers but keep existing stats
                    for (auto& header : partial.dependencies.headers) {
                        combined_result.dependencies.headers.push_back(std::move(header));
                    }
                    } else if (partial.dependencies.total_includes > 0 ||
                               partial.dependencies.unique_headers > 0) {
                        // New result has stats, use it entirely
                        combined_result.dependencies = std::move(partial.dependencies);
                               } else {
                                   // Neither has stats, just use headers from new result
                                   combined_result.dependencies.headers = std::move(partial.dependencies.headers);
                               }
            }

            if (!partial.templates.templates.empty()) {
                combined_result.templates = std::move(partial.templates);
            }

            if (!partial.symbols.symbols.empty()) {
                combined_result.symbols = std::move(partial.symbols);
            }
        }

        const auto end_time = std::chrono::steady_clock::now();
        combined_result.analysis_time = std::chrono::system_clock::now();
        combined_result.analysis_duration = std::chrono::duration_cast<Duration>(end_time - start_time);

        return Result<AnalysisResult, Error>::success(std::move(combined_result));
    }
}  // namespace bha::analyzers