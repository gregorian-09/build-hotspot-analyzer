//
// Created by gregorian-rayne on 12/30/25.
//

#include <ranges>

#include "bha/analyzers/analyzer.hpp"
#include <unordered_map>

namespace bha::analyzers
{
    AnalyzerRegistry& AnalyzerRegistry::instance() {
        static AnalyzerRegistry registry;
        return registry;
    }

    void AnalyzerRegistry::register_analyzer(std::unique_ptr<IAnalyzer> analyzer) {
        analyzers_.push_back(std::move(analyzer));
    }

    IAnalyzer* AnalyzerRegistry::get_analyzer(const std::string_view name) const {
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

        std::unordered_map<std::string, FileAnalysisResult> file_map;

        for (const auto analyzers = AnalyzerRegistry::instance().list_analyzers(); const auto* analyzer : analyzers) {
            auto result = analyzer->analyze(trace, options);

            if (result.is_err()) {
                continue;
            }

            auto& partial = result.value();

            if (!partial.files.empty()) {
                for (auto& file : partial.files) {
                    std::string key = file.file.string();

                    if (auto it = file_map.find(key); it != file_map.end()) {
                        auto& existing = it->second;

                        if (file.compile_time != Duration::zero()) {
                            existing.compile_time = file.compile_time;
                        }
                        if (file.frontend_time != Duration::zero()) {
                            existing.frontend_time = file.frontend_time;
                        }
                        if (file.backend_time != Duration::zero()) {
                            existing.backend_time = file.backend_time;
                        }
                        if (file.include_count > 0) {
                            existing.include_count = file.include_count;
                        }
                        if (file.template_count > 0) {
                            existing.template_count = file.template_count;
                        }
                        if (file.time_percent > 0.0) {
                            existing.time_percent = file.time_percent;
                        }
                        if (file.rank > 0) {
                            existing.rank = file.rank;
                        }

                        if (file.breakdown.preprocessing != Duration::zero()) {
                            existing.breakdown.preprocessing = file.breakdown.preprocessing;
                        }
                        if (file.breakdown.parsing != Duration::zero()) {
                            existing.breakdown.parsing = file.breakdown.parsing;
                        }
                        if (file.breakdown.semantic_analysis != Duration::zero()) {
                            existing.breakdown.semantic_analysis = file.breakdown.semantic_analysis;
                        }
                        if (file.breakdown.template_instantiation != Duration::zero()) {
                            existing.breakdown.template_instantiation = file.breakdown.template_instantiation;
                        }
                        if (file.breakdown.code_generation != Duration::zero()) {
                            existing.breakdown.code_generation = file.breakdown.code_generation;
                        }
                        if (file.breakdown.optimization != Duration::zero()) {
                            existing.breakdown.optimization = file.breakdown.optimization;
                        }

                        if (file.memory.peak_memory_bytes > 0) {
                            existing.memory.peak_memory_bytes = file.memory.peak_memory_bytes;
                        }
                        if (file.memory.frontend_peak_bytes > 0) {
                            existing.memory.frontend_peak_bytes = file.memory.frontend_peak_bytes;
                        }
                        if (file.memory.backend_peak_bytes > 0) {
                            existing.memory.backend_peak_bytes = file.memory.backend_peak_bytes;
                        }
                        if (file.memory.max_stack_bytes > 0) {
                            existing.memory.max_stack_bytes = file.memory.max_stack_bytes;
                        }
                        if (file.memory.parsing_bytes > 0) {
                            existing.memory.parsing_bytes = file.memory.parsing_bytes;
                        }
                        if (file.memory.semantic_bytes > 0) {
                            existing.memory.semantic_bytes = file.memory.semantic_bytes;
                        }
                        if (file.memory.codegen_bytes > 0) {
                            existing.memory.codegen_bytes = file.memory.codegen_bytes;
                        }
                        if (file.memory.ggc_memory > 0) {
                            existing.memory.ggc_memory = file.memory.ggc_memory;
                        }
                    } else {
                        file_map[key] = std::move(file);
                    }
                }
            }

            if (partial.performance.total_build_time != Duration::zero()) {
                if (combined_result.performance.total_build_time == Duration::zero()) {
                    combined_result.performance = partial.performance;
                } else {
                    if (partial.performance.peak_memory.peak_memory_bytes >
                        combined_result.performance.peak_memory.peak_memory_bytes) {
                        combined_result.performance.peak_memory = partial.performance.peak_memory;
                    }
                    if (partial.performance.total_memory.peak_memory_bytes > 0) {
                        combined_result.performance.total_memory.peak_memory_bytes +=
                            partial.performance.total_memory.peak_memory_bytes;
                    }
                }
            }

            if (!partial.dependencies.headers.empty()) {
                if (combined_result.dependencies.total_includes > 0 ||
                    combined_result.dependencies.unique_headers > 0) {
                    for (auto& header : partial.dependencies.headers) {
                        combined_result.dependencies.headers.push_back(std::move(header));
                    }
                } else if (partial.dependencies.total_includes > 0 ||
                           partial.dependencies.unique_headers > 0) {
                    combined_result.dependencies = std::move(partial.dependencies);
                } else {
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

        combined_result.files.reserve(file_map.size());
        for (auto& file : file_map | std::views::values) {
            combined_result.files.push_back(std::move(file));
        }

        const auto end_time = std::chrono::steady_clock::now();
        combined_result.analysis_time = std::chrono::system_clock::now();
        combined_result.analysis_duration = std::chrono::duration_cast<Duration>(end_time - start_time);

        return Result<AnalysisResult, Error>::success(std::move(combined_result));
    }

}  // namespace bha::analyzers