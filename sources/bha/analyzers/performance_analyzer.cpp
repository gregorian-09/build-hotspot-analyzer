//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/performance_analyzer.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace bha::analyzers {
    namespace {

        Duration calculate_percentile(
            std::vector<Duration>& times,
            const double percentile
        ) {
            if (times.empty()) {
                return Duration::zero();
            }

            std::ranges::sort(times);
            const auto index = static_cast<std::size_t>(
                static_cast<double>(times.size() - 1) * percentile / 100.0
            );
            return times[index];
        }

    }  // namespace

    Result<AnalysisResult, Error> PerformanceAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& options
    ) const {
        AnalysisResult result;

        if (trace.units.empty()) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        result.performance.total_build_time = trace.total_time;
        result.performance.total_files = trace.units.size();

        std::vector<Duration> compile_times;
        compile_times.reserve(trace.units.size());
        Duration sequential_total = Duration::zero();

        for (const auto& unit : trace.units) {
            const Duration compile_time = unit.metrics.total_time;
            compile_times.push_back(compile_time);
            const auto sequential_sum = utils::checked_add_duration(
                sequential_total,
                compile_time
            );
            if (!sequential_sum.has_value()) {
                return Result<AnalysisResult, Error>::failure(
                    Error::analysis_error(
                        "Sequential compile timing exceeded the supported aggregate duration range"
                    )
                );
            }
            sequential_total = *sequential_sum;

            FileAnalysisResult file_result;
            file_result.file = unit.source_file;
            file_result.compile_time = compile_time;
            file_result.frontend_time = unit.metrics.frontend_time;
            file_result.backend_time = unit.metrics.backend_time;
            file_result.breakdown = unit.metrics.breakdown;
            file_result.memory = unit.metrics.memory;
            file_result.include_count = unit.includes.size();
            file_result.template_count = unit.templates.size();

            result.files.push_back(std::move(file_result));
        }

        result.performance.sequential_time = sequential_total;
        result.performance.parallel_time = trace.total_time;

        // A parallelism ratio requires both exact serial observations and an
        // exact build wall time. Zero remains the unavailable sentinel when
        // the producer did not provide the latter.
        if (trace.total_time > Duration::zero()) {
            result.performance.parallelism_efficiency =
                static_cast<double>(sequential_total.count()) /
                static_cast<double>(trace.total_time.count());
        }

        if (!compile_times.empty()) {
            result.performance.avg_file_time = sequential_total / compile_times.size();
            result.performance.median_file_time = calculate_percentile(compile_times, 50.0);
            result.performance.p90_file_time = calculate_percentile(compile_times, 90.0);
            result.performance.p99_file_time = calculate_percentile(compile_times, 99.0);
        }

        std::size_t files_with_memory = 0;
        for (const auto& file : result.files) {
            if (!file.memory.has_data()) {
                continue;
            }

            const auto memory_sum = utils::checked_add(
                result.performance.total_memory.max_stack_bytes,
                file.memory.max_stack_bytes
            );
            if (!memory_sum.has_value()) {
                return Result<AnalysisResult, Error>::failure(
                    Error::analysis_error("Total stack-memory telemetry overflowed")
                );
            }
            result.performance.total_memory.max_stack_bytes = *memory_sum;
            if (file.memory.max_stack_bytes > result.performance.peak_memory.max_stack_bytes) {
                result.performance.peak_memory = file.memory;
            }
            ++files_with_memory;
        }

        if (files_with_memory > 0) {
            result.performance.average_memory.max_stack_bytes =
                result.performance.total_memory.max_stack_bytes / files_with_memory;
        }

        std::ranges::sort(
            result.files,
            [](const auto& left, const auto& right) {
                if (left.compile_time != right.compile_time) {
                    return left.compile_time > right.compile_time;
                }
                return left.file < right.file;
            }
        );

        const Duration slow_threshold = options.min_duration_threshold;
        std::size_t slowest_count = 0;
        for (const auto& file : result.files) {
            if (file.compile_time < slow_threshold) {
                continue;
            }

            ++slowest_count;
            if (result.performance.slowest_files.size() < 20) {
                result.performance.slowest_files.push_back(file);
            }
        }
        result.performance.slowest_file_count = slowest_count;

        if (trace.total_time > Duration::zero()) {
            for (auto& file : result.files) {
                file.time_percent = 100.0 *
                    static_cast<double>(file.compile_time.count()) /
                    static_cast<double>(trace.total_time.count());
            }
        }

        for (std::size_t index = 0; index < result.files.size(); ++index) {
            result.files[index].rank = index + 1;
        }

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_performance_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<PerformanceAnalyzer>()
        );
    }
}  // namespace bha::analyzers
