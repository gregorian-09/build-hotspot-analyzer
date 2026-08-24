//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/file_analyzer.hpp"

#include <algorithm>

namespace bha::analyzers
{
    namespace {

        bool has_negative_timing(const CompilationUnit& unit) {
            for (const auto value : {
                unit.metrics.total_time,
                unit.metrics.frontend_time,
                unit.metrics.backend_time,
                unit.metrics.breakdown.preprocessing,
                unit.metrics.breakdown.parsing,
                unit.metrics.breakdown.semantic_analysis,
                unit.metrics.breakdown.template_instantiation,
                unit.metrics.breakdown.code_generation,
                unit.metrics.breakdown.optimization,
                unit.metrics.breakdown.unclassified
            }) {
                if (value < Duration::zero()) {
                    return true;
                }
            }
            return false;
        }

        FileAnalysisResult analyze_compilation_unit(
            const CompilationUnit& unit,
            const Duration total_time
        ) {
            FileAnalysisResult result;
            result.file = unit.source_file;
            result.compile_time = unit.metrics.total_time;
            result.frontend_time = unit.metrics.frontend_time;
            result.backend_time = unit.metrics.backend_time;
            result.breakdown = unit.metrics.breakdown;
            result.memory = unit.metrics.memory;

            if (total_time.count() > 0) {
                result.time_percent = 100.0 * static_cast<double>(unit.metrics.total_time.count()) /
                                      static_cast<double>(total_time.count());
            }

            result.include_count = unit.includes.size();
            result.template_count = unit.templates.size();

            return result;
        }

    }  // namespace

    Result<AnalysisResult, Error> FileAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        const auto start_time = std::chrono::steady_clock::now();

        if (trace.units.empty()) {
            result.analysis_time = std::chrono::system_clock::now();
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        if (trace.total_time < Duration::zero()) {
            return Result<AnalysisResult, Error>::failure(
                Error::analysis_error("Build timing cannot be negative")
            );
        }

        const Duration total_time = trace.total_time;

        result.files.reserve(trace.units.size());

        for (const auto& unit : trace.units) {
            if (has_negative_timing(unit)) {
                return Result<AnalysisResult, Error>::failure(
                    Error::analysis_error("Compilation timing cannot be negative")
                );
            }
            if (unit.metrics.total_time < options.min_duration_threshold) {
                continue;
            }

            auto file_result = analyze_compilation_unit(unit, total_time);
            result.files.push_back(std::move(file_result));
        }

        std::ranges::sort(result.files,
                          [](const auto& a, const auto& b) {
                              return a.compile_time > b.compile_time;
                          });

        for (std::size_t i = 0; i < result.files.size(); ++i) {
            result.files[i].rank = i + 1;
        }

        const auto end_time = std::chrono::steady_clock::now();
        result.analysis_time = std::chrono::system_clock::now();
        result.analysis_duration = std::chrono::duration_cast<Duration>(end_time - start_time);

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_file_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<FileAnalyzer>()
        );
    }
}  // namespace bha::analyzers
