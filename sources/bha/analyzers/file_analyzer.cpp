//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/file_analyzer.hpp"

#include <algorithm>
#include <numeric>

namespace bha::analyzers
{
    namespace {

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

        Duration calculate_percentile(const std::vector<Duration>& sorted_times, double percentile) {
            if (sorted_times.empty()) {
                return Duration::zero();
            }

            const auto index = static_cast<std::size_t>(percentile / 100.0 *
                                                  static_cast<double>(sorted_times.size() - 1));
            return sorted_times[std::min(index, sorted_times.size() - 1)];
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

        Duration total_time = trace.total_time;
        if (total_time == Duration::zero()) {
            for (const auto& unit : trace.units) {
                total_time += unit.metrics.total_time;
            }
        }

        result.files.reserve(trace.units.size());
        std::vector<Duration> all_times;
        all_times.reserve(trace.units.size());

        for (const auto& unit : trace.units) {
            if (unit.metrics.total_time < options.min_duration_threshold) {
                continue;
            }

            auto file_result = analyze_compilation_unit(unit, total_time);
            all_times.push_back(unit.metrics.total_time);
            result.files.push_back(std::move(file_result));
        }

        std::ranges::sort(result.files,
                          [](const auto& a, const auto& b) {
                              return a.compile_time > b.compile_time;
                          });

        for (std::size_t i = 0; i < result.files.size(); ++i) {
            result.files[i].rank = i + 1;
        }

        std::ranges::sort(all_times);

        result.performance.total_build_time = trace.total_time;
        result.performance.total_files = trace.units.size();

        if (!all_times.empty()) {
            const Duration sum = std::accumulate(all_times.begin(), all_times.end(), Duration::zero());
            result.performance.avg_file_time = sum / all_times.size();
            result.performance.median_file_time = calculate_percentile(all_times, 50.0);
            result.performance.p90_file_time = calculate_percentile(all_times, 90.0);
            result.performance.p99_file_time = calculate_percentile(all_times, 99.0);
            result.performance.sequential_time = sum;
        }

        const std::size_t slowest_count = std::min(static_cast<std::size_t>(10), result.files.size());
        result.performance.slowest_files.assign(
            result.files.begin(),
            result.files.begin() + static_cast<std::ptrdiff_t>(slowest_count)
        );
        result.performance.slowest_file_count = slowest_count;

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