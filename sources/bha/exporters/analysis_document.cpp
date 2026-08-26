#include "bha/exporters/analysis_document.hpp"

#include "bha/utils/time_utils.hpp"
#include "bha/version.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace bha::exporters {
    namespace {
        using json = nlohmann::json;

        double duration_to_ms(const Duration duration) {
            return static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(duration).count()
            ) / 1000.0;
        }

        json serialize_metric_capability(const MetricCapability& capability) {
            const auto& provenance = capability.provenance;
            return {
                {"metric", capability.metric},
                {"evidence", to_string(provenance.evidence)},
                {"producer", provenance.producer},
                {"producer_version", provenance.producer_version},
                {"capture_mode", provenance.capture_mode},
                {"scope", provenance.scope},
                {"timing_domain", to_string(provenance.timing_domain)},
                {"timing_aggregation", to_string(provenance.timing_aggregation)},
                {"limitation", provenance.limitation}
            };
        }

        json serialize_metric_capabilities(const std::vector<MetricCapability>& capabilities) {
            json result = json::array();
            for (const auto& capability : capabilities) {
                result.push_back(serialize_metric_capability(capability));
            }
            return result;
        }

        json serialize_source_location(const SourceLocation& location) {
            return {
                {"file", location.file.string()},
                {"line", location.line},
                {"column", location.column}
            };
        }

        json serialize_paths(const std::vector<fs::path>& paths) {
            json result = json::array();
            for (const auto& path : paths) {
                result.push_back(path.string());
            }
            return result;
        }

        json serialize_time_breakdown(const TimeBreakdown& breakdown) {
            return {
                {"preprocessing_ms", duration_to_ms(breakdown.preprocessing)},
                {"parsing_ms", duration_to_ms(breakdown.parsing)},
                {"semantic_analysis_ms", duration_to_ms(breakdown.semantic_analysis)},
                {"template_instantiation_ms", duration_to_ms(breakdown.template_instantiation)},
                {"code_generation_ms", duration_to_ms(breakdown.code_generation)},
                {"optimization_ms", duration_to_ms(breakdown.optimization)},
                {"unclassified_ms", duration_to_ms(breakdown.unclassified)}
            };
        }

        json serialize_file(const analyzers::FileAnalysisResult& file) {
            return {
                {"path", file.file.string()},
                {"compile_time_ms", duration_to_ms(file.compile_time)},
                {"frontend_time_ms", duration_to_ms(file.frontend_time)},
                {"backend_time_ms", duration_to_ms(file.backend_time)},
                {"breakdown", serialize_time_breakdown(file.breakdown)},
                {"memory", {{"max_stack_bytes", file.memory.max_stack_bytes}}},
                {"time_percent", file.time_percent},
                {"rank", file.rank},
                {"include_count", file.include_count},
                {"template_count", file.template_count}
            };
        }

        json serialize_performance(const analyzers::PerformanceAnalysisResult& performance) {
            json result = {
                {"total_build_time_ms", duration_to_ms(performance.total_build_time)},
                {"sequential_time_ms", duration_to_ms(performance.sequential_time)},
                {"parallel_time_ms", duration_to_ms(performance.parallel_time)},
                {"parallelism_efficiency", performance.parallelism_efficiency},
                {"total_files", performance.total_files},
                {"slowest_file_count", performance.slowest_file_count},
                {"avg_file_time_ms", duration_to_ms(performance.avg_file_time)},
                {"median_file_time_ms", duration_to_ms(performance.median_file_time)},
                {"p90_file_time_ms", duration_to_ms(performance.p90_file_time)},
                {"p99_file_time_ms", duration_to_ms(performance.p99_file_time)},
                {"memory", {
                    {"total_max_stack_bytes", performance.total_memory.max_stack_bytes},
                    {"peak_max_stack_bytes", performance.peak_memory.max_stack_bytes},
                    {"average_max_stack_bytes", performance.average_memory.max_stack_bytes}
                }},
                {"slowest_files", json::array()}
            };
            for (const auto& file : performance.slowest_files) {
                result["slowest_files"].push_back(serialize_file(file));
            }
            return result;
        }

        json serialize_cache(const analyzers::CacheDistributionAnalysisResult& cache) {
            return {
                {"compile_requests", cache.compile_requests},
                {"executed_compilations", cache.executed_compilations},
                {"non_compilation_requests", cache.non_compilation_requests},
                {"unsupported_compiler_requests", cache.unsupported_compiler_requests},
                {"non_cacheable_requests", cache.non_cacheable_requests},
                {"compilations", cache.compilations},
                {"cache_hits", cache.cache_hits},
                {"cache_misses", cache.cache_misses},
                {"cache_errors", cache.cache_errors},
                {"cache_timeouts", cache.cache_timeouts},
                {"cache_read_errors", cache.cache_read_errors},
                {"non_cacheable_compilations", cache.non_cacheable_compilations},
                {"forced_recaches", cache.forced_recaches},
                {"cache_write_errors", cache.cache_write_errors},
                {"cache_writes", cache.cache_writes},
                {"compilation_failures", cache.compilation_failures},
                {"hit_rate_percent", cache.hit_rate_percent.has_value()
                    ? json(*cache.hit_rate_percent)
                    : json(nullptr)},
                {"metric_capabilities", serialize_metric_capabilities(cache.metric_capabilities)}
            };
        }

        json serialize_build_session(const analyzers::BuildSessionAnalysisResult& session) {
            json result = {
                {"timed_commands", session.timed_commands},
                {"total_commands", session.total_commands},
                {"wall_clock_time_ms", duration_to_ms(session.wall_clock_time)},
                {"serial_time_ms", duration_to_ms(session.serial_time)},
                {"peak_parallelism", session.peak_parallelism},
                {"average_parallelism", session.average_parallelism},
                {"critical_path_time_ms", duration_to_ms(session.critical_path_time)},
                {"critical_path", session.critical_path},
                {"compile_trace_references", session.compile_trace_references},
                {"step_metrics", json::array()},
                {"host_telemetry", {
                    {"memory_samples", session.host_telemetry.memory_samples},
                    {"peak_memory_used_kib", session.host_telemetry.peak_memory_used_kib.has_value()
                        ? json(*session.host_telemetry.peak_memory_used_kib)
                        : json(nullptr)},
                    {"cpu_load_samples", session.host_telemetry.cpu_load_samples},
                    {"peak_before_cpu_load_average", session.host_telemetry.peak_before_cpu_load_average.has_value()
                        ? json(*session.host_telemetry.peak_before_cpu_load_average)
                        : json(nullptr)},
                    {"peak_after_cpu_load_average", session.host_telemetry.peak_after_cpu_load_average.has_value()
                        ? json(*session.host_telemetry.peak_after_cpu_load_average)
                        : json(nullptr)},
                    {"metric_capabilities", serialize_metric_capabilities(
                        session.host_telemetry.metric_capabilities
                    )}
                }},
                {"host_system", nullptr},
                {"metric_capabilities", serialize_metric_capabilities(session.metric_capabilities)}
            };
            for (const auto& step : session.step_metrics) {
                result["step_metrics"].push_back({
                    {"role", to_string(step.role)},
                    {"total_commands", step.total_commands},
                    {"timed_commands", step.timed_commands},
                    {"wall_clock_time_ms", duration_to_ms(step.wall_clock_time)},
                    {"result_observations", step.result_observations},
                    {"successful_commands", step.successful_commands},
                    {"failed_commands", step.failed_commands},
                    {"output_observations", step.output_observations},
                    {"stdout_bytes", step.stdout_bytes.has_value() ? json(*step.stdout_bytes) : json(nullptr)},
                    {"stderr_bytes", step.stderr_bytes.has_value() ? json(*step.stderr_bytes) : json(nullptr)}
                });
            }
            if (session.host_system.has_value()) {
                const auto& host = *session.host_system;
                result["host_system"] = {
                    {"os_name", host.os_name.has_value() ? json(*host.os_name) : json(nullptr)},
                    {"os_platform", host.os_platform.has_value() ? json(*host.os_platform) : json(nullptr)},
                    {"os_release", host.os_release.has_value() ? json(*host.os_release) : json(nullptr)},
                    {"os_version", host.os_version.has_value() ? json(*host.os_version) : json(nullptr)},
                    {"is_64_bits", host.is_64_bits.has_value() ? json(*host.is_64_bits) : json(nullptr)},
                    {"logical_cpu_count", host.logical_cpu_count.has_value()
                        ? json(*host.logical_cpu_count) : json(nullptr)},
                    {"physical_cpu_count", host.physical_cpu_count.has_value()
                        ? json(*host.physical_cpu_count) : json(nullptr)},
                    {"total_physical_memory_mib", host.total_physical_memory_mib.has_value()
                        ? json(*host.total_physical_memory_mib) : json(nullptr)},
                    {"total_virtual_memory_mib", host.total_virtual_memory_mib.has_value()
                        ? json(*host.total_virtual_memory_mib) : json(nullptr)},
                    {"processor_name", host.processor_name.has_value()
                        ? json(*host.processor_name) : json(nullptr)},
                    {"vendor_string", host.vendor_string.has_value()
                        ? json(*host.vendor_string) : json(nullptr)}
                };
            }
            return result;
        }

        json serialize_linker(const analyzers::LinkerAnalysisResult& linker) {
            return {
                {"invocations", linker.invocations},
                {"timed_invocations", linker.timed_invocations},
                {"output_size_observations", linker.output_size_observations},
                {"wall_clock_time_ms", duration_to_ms(linker.wall_clock_time)},
                {"output_bytes", linker.output_bytes},
                {"trace_wall_clock_time_ms", linker.trace_wall_clock_time.has_value()
                    ? json(duration_to_ms(*linker.trace_wall_clock_time)) : json(nullptr)},
                {"lto_time_ms", linker.lto_time.has_value()
                    ? json(duration_to_ms(*linker.lto_time)) : json(nullptr)},
                {"metric_capabilities", serialize_metric_capabilities(linker.metric_capabilities)}
            };
        }

        json serialize_targets(const analyzers::BuildTargetAnalysisResult& targets) {
            json result = {
                {"target_commands", targets.target_commands},
                {"matched_commands", targets.matched_commands},
                {"unmatched_commands", targets.unmatched_commands},
                {"pch_targets", targets.pch_targets},
                {"pch_headers", targets.pch_headers},
                {"targets", json::array()},
                {"metric_capabilities", serialize_metric_capabilities(targets.metric_capabilities)}
            };
            for (const auto& target : targets.targets) {
                result["targets"].push_back({
                    {"id", target.id},
                    {"name", target.name},
                    {"type", target.type},
                    {"dependencies", target.dependencies},
                    {"compile_commands", target.compile_commands},
                    {"timed_compile_commands", target.timed_compile_commands},
                    {"compile_wall_clock_time_ms", duration_to_ms(target.compile_wall_clock_time)},
                    {"link_commands", target.link_commands},
                    {"timed_link_commands", target.timed_link_commands},
                    {"link_wall_clock_time_ms", duration_to_ms(target.link_wall_clock_time)},
                    {"output_size_observations", target.output_size_observations},
                    {"output_bytes", target.output_bytes},
                    {"precompile_headers", serialize_paths(target.precompile_headers)}
                });
            }
            return result;
        }

        json serialize_modules(const analyzers::ModuleAnalysisResult& modules) {
            json result = {
                {"rules", modules.rules},
                {"provided_modules", modules.provided_modules},
                {"required_modules", modules.required_modules},
                {"resolved_dependencies", modules.resolved_dependencies},
                {"unresolved_dependencies", modules.unresolved_dependencies},
                {"unowned_dependencies", modules.unowned_dependencies},
                {"dependencies", json::array()},
                {"metric_capabilities", serialize_metric_capabilities(modules.metric_capabilities)}
            };
            for (const auto& [required, owner] : modules.dependencies) {
                result["dependencies"].push_back({{"required", required}, {"owner", owner}});
            }
            return result;
        }

        json serialize_process_resources(const analyzers::ProcessResourceAnalysisResult& resources) {
            return {
                {"observations", resources.observations},
                {"total_process_time_ms", duration_to_ms(resources.total_process_time)},
                {"total_user_time_ms", duration_to_ms(resources.total_user_time)},
                {"peak_memory_kib", resources.peak_memory_kib},
                {"metric_capabilities", serialize_metric_capabilities(resources.metric_capabilities)}
            };
        }

        json serialize_file_target(const FileTarget& target) {
            json result = {
                {"path", target.path.string()},
                {"line_start", target.line_start},
                {"line_end", target.line_end},
                {"column_start", target.col_start},
                {"column_end", target.col_end},
                {"action", to_string(target.action)},
                {"note", target.note.has_value() ? json(*target.note) : json(nullptr)}
            };
            return result;
        }

        json serialize_suggestion(const Suggestion& suggestion) {
            json result = {
                {"id", suggestion.id},
                {"type", to_string(suggestion.type)},
                {"priority", to_string(suggestion.priority)},
                {"confidence", suggestion.confidence},
                {"title", suggestion.title},
                {"description", suggestion.description},
                {"rationale", suggestion.rationale},
                {"estimated_savings_ms", duration_to_ms(suggestion.estimated_savings)},
                {"estimated_savings_percent", suggestion.estimated_savings_percent},
                {"estimated_savings_evidence", to_string(suggestion.estimated_savings_evidence)},
                {"target_file", serialize_file_target(suggestion.target_file)},
                {"secondary_files", json::array()},
                {"edits", json::array()},
                {"implementation_steps", suggestion.implementation_steps},
                {"impact", {
                    {"files_benefiting", json::array()},
                    {"total_files_affected", suggestion.impact.total_files_affected},
                    {"cumulative_savings_ms", duration_to_ms(suggestion.impact.cumulative_savings)},
                    {"rebuild_files_count", suggestion.impact.rebuild_files_count}
                }},
                {"caveats", suggestion.caveats},
                {"verification", suggestion.verification},
                {"is_safe", suggestion.is_safe},
                {"application_mode", to_string(resolve_application_mode(suggestion))}
            };
            for (const auto& target : suggestion.secondary_files) {
                result["secondary_files"].push_back(serialize_file_target(target));
            }
            for (const auto& file : suggestion.impact.files_benefiting) {
                result["impact"]["files_benefiting"].push_back(file.string());
            }
            if (suggestion.before_code.code.size() > 0) {
                result["before_code"] = {
                    {"file", suggestion.before_code.file.string()},
                    {"line", suggestion.before_code.line},
                    {"code", suggestion.before_code.code}
                };
            }
            if (suggestion.after_code.code.size() > 0) {
                result["after_code"] = {
                    {"file", suggestion.after_code.file.string()},
                    {"line", suggestion.after_code.line},
                    {"code", suggestion.after_code.code}
                };
            }
            for (const auto& edit : suggestion.edits) {
                result["edits"].push_back({
                    {"file", edit.file.string()},
                    {"start_line", edit.start_line},
                    {"start_column", edit.start_col},
                    {"end_line", edit.end_line},
                    {"end_column", edit.end_col},
                    {"new_text", edit.new_text},
                    {"byte_offset", edit.byte_offset.has_value() ? json(*edit.byte_offset) : json(nullptr)},
                    {"byte_length", edit.byte_length.has_value() ? json(*edit.byte_length) : json(nullptr)}
                });
            }
            if (suggestion.documentation_link.has_value()) {
                result["documentation_link"] = *suggestion.documentation_link;
            }
            if (suggestion.refactor_class_name.has_value()) {
                result["refactor_class_name"] = *suggestion.refactor_class_name;
            }
            if (suggestion.refactor_compile_commands_path.has_value()) {
                result["refactor_compile_commands_path"] = suggestion.refactor_compile_commands_path->string();
            }
            if (suggestion.application_summary.has_value()) {
                result["application_summary"] = *suggestion.application_summary;
            }
            if (suggestion.application_guidance.has_value()) {
                result["application_guidance"] = *suggestion.application_guidance;
            }
            if (suggestion.auto_apply_blocked_reason.has_value()) {
                result["auto_apply_blocked_reason"] = *suggestion.auto_apply_blocked_reason;
            }
            if (!suggestion.hotspot_origins.empty()) {
                result["hotspot_origins"] = json::array();
                for (const auto& origin : suggestion.hotspot_origins) {
                    result["hotspot_origins"].push_back({
                        {"kind", origin.kind},
                        {"source", origin.source.string()},
                        {"target", origin.target.string()},
                        {"estimated_cost_ms", duration_to_ms(origin.estimated_cost)},
                        {"chain", origin.chain},
                        {"note", origin.note}
                    });
                }
            }
            return result;
        }

        json serialize_dependencies(const analyzers::DependencyAnalysisResult& dependencies) {
            json result = {
                {"total_includes", dependencies.total_includes},
                {"unique_headers", dependencies.unique_headers},
                {"max_include_depth", dependencies.max_include_depth},
                {"total_include_time_ms", duration_to_ms(dependencies.total_include_time)},
                {"metric_capabilities", serialize_metric_capabilities(dependencies.metric_capabilities)},
                {"headers", json::array()},
                {"graph", {{"nodes", json::array()}, {"links", json::array()}}}
            };
            std::vector<std::string> node_ids;
            for (const auto& header : dependencies.headers) {
                result["headers"].push_back({
                    {"path", header.path.string()},
                    {"total_parse_time_ms", duration_to_ms(header.total_parse_time)},
                    {"self_parse_time_ms", header.self_parse_time.has_value()
                        ? json(duration_to_ms(*header.self_parse_time)) : json(nullptr)},
                    {"inclusion_count", header.inclusion_count},
                    {"including_files", header.including_files},
                    {"included_by", serialize_paths(header.included_by)}
                });
                const std::string header_id = header.path.generic_string();
                result["graph"]["nodes"].push_back({{"id", header_id}, {"type", "header"}});
                for (const auto& source : header.included_by) {
                    const std::string source_id = fs::path(source).generic_string();
                    if (std::find(node_ids.begin(), node_ids.end(), source_id) == node_ids.end()) {
                        result["graph"]["nodes"].push_back({{"id", source_id}, {"type", "source"}});
                        node_ids.push_back(source_id);
                    }
                    result["graph"]["links"].push_back({
                        {"source", source_id}, {"target", header_id}, {"type", "include"}
                    });
                }
            }
            return result;
        }

        json serialize_templates(const analyzers::TemplateAnalysisResult& templates) {
            json result = {
                {"total_template_time_ms", duration_to_ms(templates.total_template_time)},
                {"template_time_percent", templates.template_time_percent},
                {"total_instantiations", templates.total_instantiations},
                {"templates", json::array()}
            };
            for (const auto& info : templates.templates) {
                json item = {
                    {"name", info.name},
                    {"full_signature", info.full_signature},
                    {"total_time_ms", duration_to_ms(info.total_time)},
                    {"instantiation_count", info.instantiation_count},
                    {"time_percent", info.time_percent},
                    {"locations", json::array()},
                    {"files_using", info.files_using}
                };
                for (const auto& location : info.locations) {
                    item["locations"].push_back(serialize_source_location(location));
                }
                result["templates"].push_back(std::move(item));
            }
            return result;
        }

        json serialize_symbols(const analyzers::SymbolAnalysisResult& symbols) {
            json result = {
                {"total_symbols", symbols.total_symbols},
                {"unused_symbols", symbols.unused_symbols},
                {"symbols", json::array()}
            };
            for (const auto& symbol : symbols.symbols) {
                result["symbols"].push_back({
                    {"name", symbol.name},
                    {"type", symbol.type},
                    {"defined_in", symbol.defined_in.string()},
                    {"used_in", serialize_paths(symbol.used_in)},
                    {"usage_count", symbol.usage_count}
                });
            }
            return result;
        }
    }

    nlohmann::json make_analysis_document(
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const AnalysisDocumentOptions& options
    ) {
        json output;
        if (options.include_metadata) {
            output["$schema"] = "https://json-schema.org/draft/2020-12/schema";
            output["$id"] = "https://bha.dev/schemas/analysis-v" + options.schema_version + ".json";
            output["document_type"] = "bha-analysis";
            output["schema_version"] = options.schema_version;
            output["bha_version"] = VERSION_STRING;
            output["generated_at"] = utils::format_timestamp_iso8601(std::chrono::system_clock::now());
            output["analysis_time"] = utils::format_timestamp_iso8601(analysis.analysis_time);
        }

        output["performance"] = serialize_performance(analysis.performance);
        output["summary"] = {
            {"total_files", analysis.files.size()},
            {"total_compile_time_ms", duration_to_ms(analysis.performance.total_build_time)},
            {"analysis_duration_ms", duration_to_ms(analysis.analysis_duration)},
            {"metric_capabilities", serialize_metric_capabilities(analysis.metric_capabilities)}
        };

        if (options.include_file_details) {
            output["files"] = json::array();
            for (const auto& file : analysis.files) {
                if (options.min_compile_time > Duration::zero() &&
                    file.compile_time < options.min_compile_time) {
                    continue;
                }
                if (options.max_files > 0 && output["files"].size() >= options.max_files) {
                    break;
                }
                output["files"].push_back(serialize_file(file));
            }
        }
        if (options.include_dependencies) {
            output["dependencies"] = serialize_dependencies(analysis.dependencies);
        }
        if (options.include_templates) {
            output["templates"] = serialize_templates(analysis.templates);
        }
        if (options.include_symbols) {
            output["symbols"] = serialize_symbols(analysis.symbols);
        }

        output["cache_distribution"] = serialize_cache(analysis.cache_distribution);
        output["build_session"] = serialize_build_session(analysis.build_session);
        output["linker"] = serialize_linker(analysis.linker);
        output["targets"] = serialize_targets(analysis.targets);
        output["modules"] = serialize_modules(analysis.modules);
        output["process_resources"] = serialize_process_resources(analysis.process_resources);

        if (options.include_suggestions) {
            output["suggestions"] = json::array();
            for (const auto& suggestion : suggestions) {
                if (options.max_suggestions > 0 &&
                    output["suggestions"].size() >= options.max_suggestions) {
                    break;
                }
                output["suggestions"].push_back(serialize_suggestion(suggestion));
            }
        }
        return output;
    }
} // namespace bha::exporters
