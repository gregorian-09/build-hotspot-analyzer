//
// Created by gregorian-rayne on 12/30/25.
//

#include <algorithm>
#include <ranges>

#include "bha/analyzers/analyzer.hpp"
#include <unordered_map>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <utility>

namespace bha::analyzers
{
    namespace {

        bool same_capability_identity(
            const MetricCapability& left,
            const MetricCapability& right
        ) {
            return left.metric == right.metric &&
                left.provenance.producer == right.provenance.producer &&
                left.provenance.producer_version == right.provenance.producer_version &&
                left.provenance.capture_mode == right.provenance.capture_mode &&
                left.provenance.scope == right.provenance.scope &&
                left.provenance.timing_domain == right.provenance.timing_domain &&
                left.provenance.timing_aggregation == right.provenance.timing_aggregation;
        }

        void add_capability(
            std::vector<MetricCapability>& capabilities,
            MetricCapability value
        ) {
            const auto existing = std::ranges::find_if(
                capabilities,
                [&value](const MetricCapability& candidate) {
                    return same_capability_identity(candidate, value);
                }
            );
            if (existing == capabilities.end()) {
                capabilities.push_back(std::move(value));
            } else if (!existing->provenance.has_evidence() && value.provenance.has_evidence()) {
                *existing = std::move(value);
            }
        }

    }  // namespace

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
        for (const auto& capability : trace.metric_capabilities) {
            add_capability(combined_result.metric_capabilities, capability);
        }
        for (const auto& unit : trace.units) {
            for (const auto& capability : unit.metric_capabilities) {
                add_capability(combined_result.metric_capabilities, capability);
            }
        }
        const auto start_time = std::chrono::steady_clock::now();
        const auto total_deadline = options.max_total_time != Duration::zero()
            ? std::optional<std::chrono::steady_clock::time_point>(start_time + options.max_total_time)
            : std::optional<std::chrono::steady_clock::time_point>();

        std::unordered_map<std::string, FileAnalysisResult> file_map;

        const auto analyzers = AnalyzerRegistry::instance().list_analyzers();

        // Launch all analyzers in parallel. Each analyzer is independent
        // (read-only access to trace and options), so wall time becomes
        // max(analyzer times) instead of sum(analyzer times).
        using TimedAnalysis = std::pair<Result<AnalysisResult, Error>, Duration>;
        std::vector<std::future<TimedAnalysis>> futures;
        futures.reserve(analyzers.size());

        for (const auto* analyzer : analyzers) {
            if (total_deadline.has_value() && std::chrono::steady_clock::now() >= *total_deadline) {
                break;
            }
            futures.push_back(std::async(std::launch::async, [analyzer, &trace, &options]() {
                const auto analyzer_start = std::chrono::steady_clock::now();
                auto result = analyzer->analyze(trace, options);
                const auto analyzer_elapsed = std::chrono::duration_cast<Duration>(
                    std::chrono::steady_clock::now() - analyzer_start
                );
                return std::make_pair(std::move(result), analyzer_elapsed);
            }));
        }

        // Merge results sequentially (fast path: mutex-free, results are independent)
        for (auto& future : futures) {
            auto timed_result = future.get();
            auto result = std::move(timed_result.first);
            const auto analyzer_elapsed = timed_result.second;

            if (options.max_analyzer_time != Duration::zero() &&
                analyzer_elapsed >= options.max_analyzer_time) {
                continue;
            }

            if (total_deadline.has_value() &&
                std::chrono::steady_clock::now() >= *total_deadline) {
                continue;
            }

            if (result.is_err()) {
                continue;
            }

            auto& partial = result.value();

            if (!partial.files.empty()) {
                for (auto& file : partial.files) {
                    const std::string key = file.file.string();

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
                        if (file.breakdown.unclassified != Duration::zero()) {
                            existing.breakdown.unclassified = file.breakdown.unclassified;
                        }

                        if (file.memory.max_stack_bytes > 0) {
                            existing.memory.max_stack_bytes = file.memory.max_stack_bytes;
                        }
                    } else {
                        file_map[key] = std::move(file);
                    }
                }
            }

            // Per-translation-unit and serial compile metrics remain valid even
            // when the producer did not expose an exact whole-build duration.
            const bool has_performance_observations =
                partial.performance.total_files > 0 ||
                partial.performance.total_build_time != Duration::zero() ||
                partial.performance.sequential_time != Duration::zero() ||
                partial.performance.parallel_time != Duration::zero() ||
                !partial.performance.slowest_files.empty();
            if (has_performance_observations) {
                if (combined_result.performance.total_build_time == Duration::zero()) {
                    combined_result.performance = partial.performance;
                } else {
                    if (partial.performance.peak_memory.max_stack_bytes >
                        combined_result.performance.peak_memory.max_stack_bytes) {
                        combined_result.performance.peak_memory = partial.performance.peak_memory;
                    }
                    if (partial.performance.total_memory.max_stack_bytes > 0) {
                        combined_result.performance.total_memory.max_stack_bytes +=
                            partial.performance.total_memory.max_stack_bytes;
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

            for (const auto& capability : partial.dependencies.metric_capabilities) {
                add_capability(combined_result.metric_capabilities, capability);
            }

            if (!partial.templates.templates.empty()) {
                combined_result.templates = std::move(partial.templates);
            }

            if (!partial.symbols.symbols.empty()) {
                combined_result.symbols = std::move(partial.symbols);
            }

            if (partial.cache_distribution.compile_requests > 0 ||
                partial.cache_distribution.executed_compilations > 0 ||
                partial.cache_distribution.compilations > 0 ||
                partial.cache_distribution.cache_hits > 0 ||
                partial.cache_distribution.cache_misses > 0 ||
                !partial.cache_distribution.metric_capabilities.empty()) {
                for (const auto& capability : partial.cache_distribution.metric_capabilities) {
                    add_capability(combined_result.metric_capabilities, capability);
                }
                combined_result.cache_distribution = partial.cache_distribution;
            }

            if (partial.build_session.total_commands > 0 ||
                partial.build_session.host_system.has_value() ||
                partial.build_session.compile_trace_references > 0 ||
                !partial.build_session.metric_capabilities.empty()) {
                for (const auto& capability : partial.build_session.metric_capabilities) {
                    add_capability(combined_result.metric_capabilities, capability);
                }
                combined_result.build_session = std::move(partial.build_session);
            }

            if (partial.linker.invocations > 0) {
                for (const auto& capability : partial.linker.metric_capabilities) {
                    add_capability(combined_result.metric_capabilities, capability);
                }
                combined_result.linker = std::move(partial.linker);
            }

            if (!partial.targets.targets.empty()) {
                for (const auto& capability : partial.targets.metric_capabilities) {
                    add_capability(combined_result.metric_capabilities, capability);
                }
                combined_result.targets = std::move(partial.targets);
            }

            if (partial.modules.rules > 0 || !partial.modules.metric_capabilities.empty()) {
                for (const auto& capability : partial.modules.metric_capabilities) {
                    add_capability(combined_result.metric_capabilities, capability);
                }
                combined_result.modules = std::move(partial.modules);
            }

            if (partial.process_resources.observations > 0 ||
                !partial.process_resources.metric_capabilities.empty()) {
                for (const auto& capability : partial.process_resources.metric_capabilities) {
                    add_capability(combined_result.metric_capabilities, capability);
                }
                combined_result.process_resources = std::move(partial.process_resources);
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
