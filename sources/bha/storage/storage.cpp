//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/storage.hpp"
#include "bha/utils/time_utils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace bha::storage
{

    namespace {

        /**
         * Parses ISO 8601 timestamp.
         */

        Timestamp parse_timestamp(const std::string& str) {
            std::tm tm = {};
            std::istringstream ss(str);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            if (ss.fail()) {
                return std::chrono::system_clock::now();
            }
            return std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }

        /**
         * Converts Duration to milliseconds for JSON.
         */
        double duration_to_ms(const Duration d) {
            return static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(d).count()
            ) / 1000.0;
        }

        /**
         * Converts milliseconds to Duration.
         */
        Duration ms_to_duration(const double ms) {
            return std::chrono::duration_cast<Duration>(
                std::chrono::microseconds(static_cast<int64_t>(ms * 1000))
            );
        }

        EvidenceKind evidence_kind_from_string(const std::string& value) {
            if (value == "observed") {
                return EvidenceKind::Observed;
            }
            if (value == "derived") {
                return EvidenceKind::Derived;
            }
            return EvidenceKind::Unavailable;
        }

        TimingDomain timing_domain_from_string(const std::string& value) {
            if (value == "wall-clock") {
                return TimingDomain::WallClock;
            }
            if (value == "cpu") {
                return TimingDomain::Cpu;
            }
            return TimingDomain::None;
        }

        TimingAggregation timing_aggregation_from_string(const std::string& value) {
            if (value == "exclusive") {
                return TimingAggregation::Exclusive;
            }
            if (value == "inclusive") {
                return TimingAggregation::Inclusive;
            }
            if (value == "wall-clock-responsibility") {
                return TimingAggregation::WallClockResponsibility;
            }
            return TimingAggregation::None;
        }

        nlohmann::json serialize_metric_capabilities(
            const std::vector<MetricCapability>& capabilities
        ) {
            nlohmann::json result = nlohmann::json::array();
            for (const auto& capability : capabilities) {
                const auto& provenance = capability.provenance;
                result.push_back({
                    {"metric", capability.metric},
                    {"evidence", to_string(provenance.evidence)},
                    {"producer", provenance.producer},
                    {"producer_version", provenance.producer_version},
                    {"capture_mode", provenance.capture_mode},
                    {"scope", provenance.scope},
                    {"timing_domain", to_string(provenance.timing_domain)},
                    {"timing_aggregation", to_string(provenance.timing_aggregation)},
                    {"limitation", provenance.limitation}
                });
            }
            return result;
        }

        std::vector<MetricCapability> deserialize_metric_capabilities(
            const nlohmann::json& value
        ) {
            std::vector<MetricCapability> capabilities;
            if (!value.is_array()) {
                return capabilities;
            }

            for (const auto& item : value) {
                MetricCapability capability;
                capability.metric = item.value("metric", "");
                capability.provenance.evidence = evidence_kind_from_string(
                    item.value("evidence", "unavailable")
                );
                capability.provenance.producer = item.value("producer", "");
                capability.provenance.producer_version = item.value("producer_version", "");
                capability.provenance.capture_mode = item.value("capture_mode", "");
                capability.provenance.scope = item.value("scope", "");
                capability.provenance.timing_domain = timing_domain_from_string(
                    item.value("timing_domain", "none")
                );
                capability.provenance.timing_aggregation = timing_aggregation_from_string(
                    item.value("timing_aggregation", "none")
                );
                capability.provenance.limitation = item.value("limitation", "");
                capabilities.push_back(std::move(capability));
            }
            return capabilities;
        }

        /**
         * Serializes a file analysis result to JSON.
         */
        nlohmann::json serialize_file_result(const analyzers::FileAnalysisResult& file) {
            nlohmann::json j;
            j["path"] = file.file.string();
            j["compile_time_ms"] = duration_to_ms(file.compile_time);
            j["frontend_time_ms"] = duration_to_ms(file.frontend_time);
            j["backend_time_ms"] = duration_to_ms(file.backend_time);
            j["time_percent"] = file.time_percent;
            j["rank"] = file.rank;
            j["include_count"] = file.include_count;
            j["template_count"] = file.template_count;
            return j;
        }

        /**
         * Deserializes a file analysis result from JSON.
         */
        analyzers::FileAnalysisResult deserialize_file_result(const nlohmann::json& j) {
            analyzers::FileAnalysisResult file;
            file.file = j.value("path", "");
            file.compile_time = ms_to_duration(j.value("compile_time_ms", 0.0));
            file.frontend_time = ms_to_duration(j.value("frontend_time_ms", 0.0));
            file.backend_time = ms_to_duration(j.value("backend_time_ms", 0.0));
            file.time_percent = j.value("time_percent", 0.0);
            file.rank = j.value("rank", std::size_t{0});
            file.include_count = j.value("include_count", std::size_t{0});
            file.template_count = j.value("template_count", std::size_t{0});
            return file;
        }

        /**
         * Serializes dependency analysis to JSON.
         */
        nlohmann::json serialize_dependencies(const analyzers::DependencyAnalysisResult& deps) {
            nlohmann::json j;
            j["total_includes"] = deps.total_includes;
            j["unique_headers"] = deps.unique_headers;
            j["max_include_depth"] = deps.max_include_depth;
            j["total_include_time_ms"] = duration_to_ms(deps.total_include_time);

            nlohmann::json headers = nlohmann::json::array();
            for (const auto& h : deps.headers) {
                nlohmann::json hj;
                hj["path"] = h.path.string();
                hj["total_parse_time_ms"] = duration_to_ms(h.total_parse_time);
                hj["self_parse_time_ms"] = nullptr;
                if (h.self_parse_time.has_value()) {
                    hj["self_parse_time_ms"] = duration_to_ms(*h.self_parse_time);
                }
                hj["inclusion_count"] = h.inclusion_count;
                hj["including_files"] = h.including_files;
                hj["impact_score"] = h.impact_score;
                headers.push_back(hj);
            }
            j["headers"] = headers;
            j["metric_capabilities"] = serialize_metric_capabilities(deps.metric_capabilities);

            return j;
        }

        /**
         * Deserializes dependency analysis from JSON.
         */
        analyzers::DependencyAnalysisResult deserialize_dependencies(const nlohmann::json& j) {
            analyzers::DependencyAnalysisResult deps;
            deps.total_includes = j.value("total_includes", std::size_t{0});
            deps.unique_headers = j.value("unique_headers", std::size_t{0});
            deps.max_include_depth = j.value("max_include_depth", std::size_t{0});
            deps.total_include_time = ms_to_duration(j.value("total_include_time_ms", 0.0));

            if (j.contains("headers")) {
                for (const auto& hj : j["headers"]) {
                    analyzers::DependencyAnalysisResult::HeaderInfo h;
                    h.path = hj.value("path", "");
                    h.total_parse_time = ms_to_duration(hj.value("total_parse_time_ms", 0.0));
                    if (hj.contains("self_parse_time_ms") && !hj["self_parse_time_ms"].is_null()) {
                        h.self_parse_time = ms_to_duration(hj["self_parse_time_ms"].get<double>());
                    }
                    h.inclusion_count = hj.value("inclusion_count", std::size_t{0});
                    h.including_files = hj.value("including_files", std::size_t{0});
                    h.impact_score = hj.value("impact_score", 0.0);
                    deps.headers.push_back(h);
                }
            }
            if (j.contains("metric_capabilities")) {
                deps.metric_capabilities = deserialize_metric_capabilities(j["metric_capabilities"]);
            }

            return deps;
        }

        /**
         * Serializes template analysis to JSON.
         */
        nlohmann::json serialize_templates(const analyzers::TemplateAnalysisResult& tmpl) {
            nlohmann::json j;
            j["total_template_time_ms"] = duration_to_ms(tmpl.total_template_time);
            j["template_time_percent"] = tmpl.template_time_percent;
            j["total_instantiations"] = tmpl.total_instantiations;

            nlohmann::json templates = nlohmann::json::array();
            for (const auto& t : tmpl.templates) {
                nlohmann::json tj;
                tj["name"] = t.name;
                tj["full_signature"] = t.full_signature;
                tj["total_time_ms"] = duration_to_ms(t.total_time);
                tj["instantiation_count"] = t.instantiation_count;
                tj["time_percent"] = t.time_percent;
                templates.push_back(tj);
            }
            j["templates"] = templates;

            return j;
        }

        /**
         * Deserializes template analysis from JSON.
         */
        analyzers::TemplateAnalysisResult deserialize_templates(const nlohmann::json& j) {
            analyzers::TemplateAnalysisResult tmpl;
            tmpl.total_template_time = ms_to_duration(j.value("total_template_time_ms", 0.0));
            tmpl.template_time_percent = j.value("template_time_percent", 0.0);
            tmpl.total_instantiations = j.value("total_instantiations", std::size_t{0});

            if (j.contains("templates")) {
                for (const auto& tj : j["templates"]) {
                    analyzers::TemplateAnalysisResult::TemplateInfo t;
                    t.name = tj.value("name", "");
                    t.full_signature = tj.value("full_signature", "");
                    t.total_time = ms_to_duration(tj.value("total_time_ms", 0.0));
                    t.instantiation_count = tj.value("instantiation_count", std::size_t{0});
                    t.time_percent = tj.value("time_percent", 0.0);
                    tmpl.templates.push_back(t);
                }
            }

            return tmpl;
        }

        /**
         * Serializes performance analysis to JSON.
         */
        nlohmann::json serialize_performance(const analyzers::PerformanceAnalysisResult& perf) {
            nlohmann::json j;
            j["total_build_time_ms"] = duration_to_ms(perf.total_build_time);
            j["sequential_time_ms"] = duration_to_ms(perf.sequential_time);
            j["parallel_time_ms"] = duration_to_ms(perf.parallel_time);
            j["parallelism_efficiency"] = perf.parallelism_efficiency;
            j["total_files"] = perf.total_files;
            j["avg_file_time_ms"] = duration_to_ms(perf.avg_file_time);
            j["median_file_time_ms"] = duration_to_ms(perf.median_file_time);
            j["p90_file_time_ms"] = duration_to_ms(perf.p90_file_time);
            j["p99_file_time_ms"] = duration_to_ms(perf.p99_file_time);
            return j;
        }

        /**
         * Deserializes performance analysis from JSON.
         */
        analyzers::PerformanceAnalysisResult deserialize_performance(const nlohmann::json& j) {
            analyzers::PerformanceAnalysisResult perf;
            perf.total_build_time = ms_to_duration(j.value("total_build_time_ms", 0.0));
            perf.sequential_time = ms_to_duration(j.value("sequential_time_ms", 0.0));
            perf.parallel_time = ms_to_duration(j.value("parallel_time_ms", 0.0));
            perf.parallelism_efficiency = j.value("parallelism_efficiency", 0.0);
            perf.total_files = j.value("total_files", std::size_t{0});
            perf.avg_file_time = ms_to_duration(j.value("avg_file_time_ms", 0.0));
            perf.median_file_time = ms_to_duration(j.value("median_file_time_ms", 0.0));
            perf.p90_file_time = ms_to_duration(j.value("p90_file_time_ms", 0.0));
            perf.p99_file_time = ms_to_duration(j.value("p99_file_time_ms", 0.0));
            return perf;
        }

        nlohmann::json serialize_cache_distribution(const analyzers::CacheDistributionAnalysisResult& cache) {
            nlohmann::json j;
            j["compile_requests"] = cache.compile_requests;
            j["executed_compilations"] = cache.executed_compilations;
            j["non_compilation_requests"] = cache.non_compilation_requests;
            j["unsupported_compiler_requests"] = cache.unsupported_compiler_requests;
            j["non_cacheable_requests"] = cache.non_cacheable_requests;
            j["compilations"] = cache.compilations;
            j["cache_hits"] = cache.cache_hits;
            j["cache_misses"] = cache.cache_misses;
            j["cache_errors"] = cache.cache_errors;
            j["cache_timeouts"] = cache.cache_timeouts;
            j["cache_read_errors"] = cache.cache_read_errors;
            j["non_cacheable_compilations"] = cache.non_cacheable_compilations;
            j["forced_recaches"] = cache.forced_recaches;
            j["cache_write_errors"] = cache.cache_write_errors;
            j["cache_writes"] = cache.cache_writes;
            j["compilation_failures"] = cache.compilation_failures;
            j["hit_rate_percent"] = nullptr;
            if (cache.hit_rate_percent.has_value()) {
                j["hit_rate_percent"] = *cache.hit_rate_percent;
            }
            j["metric_capabilities"] = serialize_metric_capabilities(cache.metric_capabilities);
            return j;
        }

        analyzers::CacheDistributionAnalysisResult deserialize_cache_distribution(const nlohmann::json& j) {
            analyzers::CacheDistributionAnalysisResult cache;
            cache.compile_requests = j.value("compile_requests", std::uint64_t{0});
            cache.executed_compilations = j.value("executed_compilations", std::uint64_t{0});
            cache.non_compilation_requests = j.value("non_compilation_requests", std::uint64_t{0});
            cache.unsupported_compiler_requests = j.value("unsupported_compiler_requests", std::uint64_t{0});
            cache.non_cacheable_requests = j.value("non_cacheable_requests", std::uint64_t{0});
            cache.compilations = j.value("compilations", std::uint64_t{0});
            cache.cache_hits = j.value("cache_hits", std::uint64_t{0});
            cache.cache_misses = j.value("cache_misses", std::uint64_t{0});
            cache.cache_errors = j.value("cache_errors", std::uint64_t{0});
            cache.cache_timeouts = j.value("cache_timeouts", std::uint64_t{0});
            cache.cache_read_errors = j.value("cache_read_errors", std::uint64_t{0});
            cache.non_cacheable_compilations = j.value("non_cacheable_compilations", std::uint64_t{0});
            cache.forced_recaches = j.value("forced_recaches", std::uint64_t{0});
            cache.cache_write_errors = j.value("cache_write_errors", std::uint64_t{0});
            cache.cache_writes = j.value("cache_writes", std::uint64_t{0});
            cache.compilation_failures = j.value("compilation_failures", std::uint64_t{0});
            if (j.contains("hit_rate_percent") && !j["hit_rate_percent"].is_null()) {
                cache.hit_rate_percent = j["hit_rate_percent"].get<double>();
            }
            if (j.contains("metric_capabilities")) {
                cache.metric_capabilities = deserialize_metric_capabilities(j["metric_capabilities"]);
            }
            return cache;
        }

        nlohmann::json serialize_build_session(
            const analyzers::BuildSessionAnalysisResult& session
        ) {
            return {
                {"timed_commands", session.timed_commands},
                {"total_commands", session.total_commands},
                {"wall_clock_time_ms", duration_to_ms(session.wall_clock_time)},
                {"serial_time_ms", duration_to_ms(session.serial_time)},
                {"peak_parallelism", session.peak_parallelism},
                {"average_parallelism", session.average_parallelism},
                {"critical_path_time_ms", duration_to_ms(session.critical_path_time)},
                {"critical_path", session.critical_path},
                {"metric_capabilities", serialize_metric_capabilities(session.metric_capabilities)}
            };
        }

        analyzers::BuildSessionAnalysisResult deserialize_build_session(
            const nlohmann::json& j
        ) {
            analyzers::BuildSessionAnalysisResult session;
            session.timed_commands = j.value("timed_commands", std::size_t{0});
            session.total_commands = j.value("total_commands", std::size_t{0});
            session.wall_clock_time = ms_to_duration(j.value("wall_clock_time_ms", 0.0));
            session.serial_time = ms_to_duration(j.value("serial_time_ms", 0.0));
            session.peak_parallelism = j.value("peak_parallelism", std::size_t{0});
            session.average_parallelism = j.value("average_parallelism", 0.0);
            session.critical_path_time = ms_to_duration(j.value("critical_path_time_ms", 0.0));
            if (j.contains("critical_path")) {
                session.critical_path = j["critical_path"].get<std::vector<std::string>>();
            }
            if (j.contains("metric_capabilities")) {
                session.metric_capabilities = deserialize_metric_capabilities(j["metric_capabilities"]);
            }
            return session;
        }

        nlohmann::json serialize_linker(
            const analyzers::LinkerAnalysisResult& linker
        ) {
            nlohmann::json result = {
                {"invocations", linker.invocations},
                {"timed_invocations", linker.timed_invocations},
                {"output_size_observations", linker.output_size_observations},
                {"wall_clock_time_ms", duration_to_ms(linker.wall_clock_time)},
                {"output_bytes", linker.output_bytes},
                {"trace_wall_clock_time_ms", nullptr},
                {"lto_time_ms", nullptr},
                {"metric_capabilities", serialize_metric_capabilities(linker.metric_capabilities)}
            };
            if (linker.trace_wall_clock_time.has_value()) {
                result["trace_wall_clock_time_ms"] = duration_to_ms(*linker.trace_wall_clock_time);
            }
            if (linker.lto_time.has_value()) {
                result["lto_time_ms"] = duration_to_ms(*linker.lto_time);
            }
            return result;
        }

        analyzers::LinkerAnalysisResult deserialize_linker(
            const nlohmann::json& j
        ) {
            analyzers::LinkerAnalysisResult linker;
            linker.invocations = j.value("invocations", std::size_t{0});
            linker.timed_invocations = j.value("timed_invocations", std::size_t{0});
            linker.output_size_observations = j.value("output_size_observations", std::size_t{0});
            linker.wall_clock_time = ms_to_duration(j.value("wall_clock_time_ms", 0.0));
            linker.output_bytes = j.value("output_bytes", std::uintmax_t{0});
            if (j.contains("trace_wall_clock_time_ms") && !j["trace_wall_clock_time_ms"].is_null()) {
                linker.trace_wall_clock_time = ms_to_duration(
                    j["trace_wall_clock_time_ms"].get<double>()
                );
            }
            if (j.contains("lto_time_ms") && !j["lto_time_ms"].is_null()) {
                linker.lto_time = ms_to_duration(j["lto_time_ms"].get<double>());
            }
            if (j.contains("metric_capabilities")) {
                linker.metric_capabilities = deserialize_metric_capabilities(j["metric_capabilities"]);
            }
            return linker;
        }

        nlohmann::json serialize_build_targets(
            const analyzers::BuildTargetAnalysisResult& targets
        ) {
            nlohmann::json result = {
                {"target_commands", targets.target_commands},
                {"matched_commands", targets.matched_commands},
                {"unmatched_commands", targets.unmatched_commands},
                {"targets", nlohmann::json::array()},
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
                    {"output_bytes", target.output_bytes}
                });
            }
            return result;
        }

        analyzers::BuildTargetAnalysisResult deserialize_build_targets(
            const nlohmann::json& j
        ) {
            analyzers::BuildTargetAnalysisResult targets;
            targets.target_commands = j.value("target_commands", std::size_t{0});
            targets.matched_commands = j.value("matched_commands", std::size_t{0});
            targets.unmatched_commands = j.value("unmatched_commands", std::size_t{0});
            if (j.contains("targets")) {
                for (const auto& target_json : j["targets"]) {
                    analyzers::BuildTargetAnalysisResult::TargetInfo target;
                    target.id = target_json.value("id", "");
                    target.name = target_json.value("name", "");
                    target.type = target_json.value("type", "");
                    if (target_json.contains("dependencies")) {
                        target.dependencies = target_json["dependencies"].get<std::vector<std::string>>();
                    }
                    target.compile_commands = target_json.value("compile_commands", std::size_t{0});
                    target.timed_compile_commands = target_json.value(
                        "timed_compile_commands",
                        std::size_t{0}
                    );
                    target.compile_wall_clock_time = ms_to_duration(
                        target_json.value("compile_wall_clock_time_ms", 0.0)
                    );
                    target.link_commands = target_json.value("link_commands", std::size_t{0});
                    target.timed_link_commands = target_json.value(
                        "timed_link_commands",
                        std::size_t{0}
                    );
                    target.link_wall_clock_time = ms_to_duration(
                        target_json.value("link_wall_clock_time_ms", 0.0)
                    );
                    target.output_size_observations = target_json.value(
                        "output_size_observations",
                        std::size_t{0}
                    );
                    target.output_bytes = target_json.value("output_bytes", std::uintmax_t{0});
                    targets.targets.push_back(std::move(target));
                }
            }
            if (j.contains("metric_capabilities")) {
                targets.metric_capabilities = deserialize_metric_capabilities(j["metric_capabilities"]);
            }
            return targets;
        }

        nlohmann::json serialize_modules(const analyzers::ModuleAnalysisResult& modules) {
            nlohmann::json result = {
                {"rules", modules.rules},
                {"provided_modules", modules.provided_modules},
                {"required_modules", modules.required_modules},
                {"resolved_dependencies", modules.resolved_dependencies},
                {"unresolved_dependencies", modules.unresolved_dependencies},
                {"unowned_dependencies", modules.unowned_dependencies},
                {"dependencies", nlohmann::json::array()},
                {"metric_capabilities", serialize_metric_capabilities(modules.metric_capabilities)}
            };
            for (const auto& [required, owner] : modules.dependencies) {
                result["dependencies"].push_back({
                    {"required", required},
                    {"owner", owner}
                });
            }
            return result;
        }

        analyzers::ModuleAnalysisResult deserialize_modules(const nlohmann::json& j) {
            analyzers::ModuleAnalysisResult modules;
            modules.rules = j.value("rules", std::size_t{0});
            modules.provided_modules = j.value("provided_modules", std::size_t{0});
            modules.required_modules = j.value("required_modules", std::size_t{0});
            modules.resolved_dependencies = j.value("resolved_dependencies", std::size_t{0});
            modules.unresolved_dependencies = j.value("unresolved_dependencies", std::size_t{0});
            modules.unowned_dependencies = j.value("unowned_dependencies", std::size_t{0});
            if (j.contains("dependencies") && j["dependencies"].is_array()) {
                for (const auto& dependency : j["dependencies"]) {
                    modules.dependencies.emplace_back(
                        dependency.value("required", ""),
                        dependency.value("owner", "")
                    );
                }
            }
            if (j.contains("metric_capabilities")) {
                modules.metric_capabilities = deserialize_metric_capabilities(j["metric_capabilities"]);
            }
            return modules;
        }

        /**
         * Serializes a suggestion to JSON.
         */
        nlohmann::json serialize_suggestion(const Suggestion& sugg) {
            nlohmann::json j;
            j["type"] = sugg.type;
            j["title"] = sugg.title;
            j["description"] = sugg.description;
            j["target_file"] = sugg.target_file.path.string();
            j["target_line"] = sugg.target_file.line_start;
            j["confidence"] = sugg.confidence;
            j["priority"] = static_cast<int>(sugg.priority);
            j["estimated_savings_ms"] = duration_to_ms(sugg.estimated_savings);
            j["is_safe"] = sugg.is_safe;
            j["application_mode"] = to_string(resolve_application_mode(sugg));
            if (sugg.refactor_class_name) {
                j["refactor_class_name"] = *sugg.refactor_class_name;
            }
            if (sugg.refactor_compile_commands_path) {
                j["refactor_compile_commands_path"] = sugg.refactor_compile_commands_path->string();
            }
            if (sugg.application_summary) {
                j["application_summary"] = *sugg.application_summary;
            }
            if (sugg.application_guidance) {
                j["application_guidance"] = *sugg.application_guidance;
            }
            if (sugg.auto_apply_blocked_reason) {
                j["auto_apply_blocked_reason"] = *sugg.auto_apply_blocked_reason;
            }
            if (!sugg.hotspot_origins.empty()) {
                nlohmann::json origins = nlohmann::json::array();
                for (const auto& origin : sugg.hotspot_origins) {
                    nlohmann::json oj;
                    oj["kind"] = origin.kind;
                    oj["source"] = origin.source.string();
                    oj["target"] = origin.target.string();
                    oj["estimated_cost_ms"] = duration_to_ms(origin.estimated_cost);
                    oj["chain"] = origin.chain;
                    oj["note"] = origin.note;
                    origins.push_back(std::move(oj));
                }
                j["hotspot_origins"] = std::move(origins);
            }
            return j;
        }

        /**
         * Deserializes a suggestion from JSON.
         */
        Suggestion deserialize_suggestion(const nlohmann::json& j) {
            Suggestion sugg;
            sugg.type = static_cast<SuggestionType>(j.value("type", 0));
            sugg.title = j.value("title", "");
            sugg.description = j.value("description", "");
            sugg.target_file.path = j.value("target_file", "");
            sugg.target_file.line_start = j.value("target_line", std::size_t{0});
            sugg.confidence = j.value("confidence", 0.0);
            sugg.priority = static_cast<Priority>(j.value("priority", 0));
            sugg.estimated_savings = ms_to_duration(j.value("estimated_savings_ms", 0.0));
            sugg.is_safe = j.value("is_safe", false);
            sugg.application_mode = suggestion_application_mode_from_string(
                j.value("application_mode", std::string(to_string(SuggestionApplicationMode::Advisory)))
            );
            if (j.contains("refactor_class_name")) {
                sugg.refactor_class_name = j.value("refactor_class_name", "");
            }
            if (j.contains("refactor_compile_commands_path")) {
                sugg.refactor_compile_commands_path = fs::path(
                    j.value("refactor_compile_commands_path", "")
                );
            }
            if (j.contains("application_summary")) {
                sugg.application_summary = j.value("application_summary", "");
            }
            if (j.contains("application_guidance")) {
                sugg.application_guidance = j.value("application_guidance", "");
            }
            if (j.contains("auto_apply_blocked_reason")) {
                sugg.auto_apply_blocked_reason = j.value("auto_apply_blocked_reason", "");
            }
            if (j.contains("hotspot_origins")) {
                for (const auto& oj : j["hotspot_origins"]) {
                    HotspotOrigin origin;
                    origin.kind = oj.value("kind", "");
                    origin.source = oj.value("source", "");
                    origin.target = oj.value("target", "");
                    origin.estimated_cost = ms_to_duration(oj.value("estimated_cost_ms", 0.0));
                    if (oj.contains("chain")) {
                        for (const auto& item : oj["chain"]) {
                            origin.chain.push_back(item.get<std::string>());
                        }
                    }
                    origin.note = oj.value("note", "");
                    sugg.hotspot_origins.push_back(std::move(origin));
                }
            }
            return sugg;
        }

    }  // namespace

    // =============================================================================
    // SnapshotStore Implementation
    // =============================================================================

    SnapshotStore::SnapshotStore(const fs::path& root)
        : root_(root) {}

    Result<void, Error> SnapshotStore::ensure_directory() const {
        try {
            if (!fs::exists(root_)) {
                fs::create_directories(root_);
            }
            return Result<void, Error>::success();
        } catch (const std::exception& e) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to create storage directory: ") + e.what())
            );
        }
    }

    Result<void, Error> SnapshotStore::save(
        const std::string& name,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const std::string& description,
        const std::vector<std::string>& tags
    ) const
    {
        if (auto dir_result = ensure_directory(); dir_result.is_err()) {
            return dir_result;
        }

        nlohmann::json j;

        // Metadata
        j["version"] = "2.0";
        j["name"] = name;
        j["description"] = description;
        j["created_at"] = utils::format_timestamp_iso8601(std::chrono::system_clock::now());
        j["file_count"] = analysis.files.size();
        j["total_build_time_ms"] = duration_to_ms(analysis.performance.total_build_time);
        j["tags"] = tags;

        j["performance"] = serialize_performance(analysis.performance);

        nlohmann::json files = nlohmann::json::array();
        for (const auto& file : analysis.files) {
            files.push_back(serialize_file_result(file));
        }
        j["files"] = files;

        j["dependencies"] = serialize_dependencies(analysis.dependencies);

        j["templates"] = serialize_templates(analysis.templates);
        j["cache_distribution"] = serialize_cache_distribution(analysis.cache_distribution);
        j["build_session"] = serialize_build_session(analysis.build_session);
        j["linker"] = serialize_linker(analysis.linker);
        j["targets"] = serialize_build_targets(analysis.targets);
        j["modules"] = serialize_modules(analysis.modules);
        j["metric_capabilities"] = serialize_metric_capabilities(analysis.metric_capabilities);

        nlohmann::json sugg_array = nlohmann::json::array();
        for (const auto& sugg : suggestions) {
            sugg_array.push_back(serialize_suggestion(sugg));
        }
        j["suggestions"] = sugg_array;

        const fs::path path = snapshot_path(name);
        try {
            std::ofstream file(path);
            if (!file.is_open()) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::IoError, "Failed to open snapshot file for writing: " + path.string())
                );
            }
            file << std::setw(2) << j << std::endl;
            return Result<void, Error>::success();
        } catch (const std::exception& e) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to write snapshot: ") + e.what())
            );
        }
    }

    Result<Snapshot, Error> SnapshotStore::load(const std::string& name) const {
        const fs::path path = snapshot_path(name);

        if (!fs::exists(path)) {
            return Result<Snapshot, Error>::failure(
                Error(ErrorCode::NotFound, "Snapshot not found: " + name)
            );
        }

        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                return Result<Snapshot, Error>::failure(
                    Error(ErrorCode::IoError, "Failed to open snapshot file: " + path.string())
                );
            }

            nlohmann::json j;
            file >> j;

            Snapshot snapshot;

            snapshot.metadata.name = j.value("name", name);
            snapshot.metadata.description = j.value("description", "");
            snapshot.metadata.created_at = parse_timestamp(j.value("created_at", ""));
            snapshot.metadata.file_count = j.value("file_count", std::size_t{0});
            snapshot.metadata.total_build_time = ms_to_duration(j.value("total_build_time_ms", 0.0));

            if (j.contains("tags")) {
                for (const auto& tag : j["tags"]) {
                    snapshot.metadata.tags.push_back(tag.get<std::string>());
                }
            }

            if (j.contains("performance")) {
                snapshot.analysis.performance = deserialize_performance(j["performance"]);
            }

            if (j.contains("files")) {
                for (const auto& fj : j["files"]) {
                    snapshot.analysis.files.push_back(deserialize_file_result(fj));
                }
            }

            if (j.contains("dependencies")) {
                snapshot.analysis.dependencies = deserialize_dependencies(j["dependencies"]);
            }

            if (j.contains("templates")) {
                snapshot.analysis.templates = deserialize_templates(j["templates"]);
            }

            if (j.contains("cache_distribution")) {
                snapshot.analysis.cache_distribution = deserialize_cache_distribution(j["cache_distribution"]);
            }

            if (j.contains("build_session")) {
                snapshot.analysis.build_session = deserialize_build_session(j["build_session"]);
            }

            if (j.contains("linker")) {
                snapshot.analysis.linker = deserialize_linker(j["linker"]);
            }

            if (j.contains("targets")) {
                snapshot.analysis.targets = deserialize_build_targets(j["targets"]);
            }

            if (j.contains("modules")) {
                snapshot.analysis.modules = deserialize_modules(j["modules"]);
            }

            if (j.contains("metric_capabilities")) {
                snapshot.analysis.metric_capabilities = deserialize_metric_capabilities(
                    j["metric_capabilities"]
                );
            }

            if (j.contains("suggestions")) {
                for (const auto& sj : j["suggestions"]) {
                    snapshot.suggestions.push_back(deserialize_suggestion(sj));
                }
            }

            return Result<Snapshot, Error>::success(std::move(snapshot));
        } catch (const nlohmann::json::exception& e) {
            return Result<Snapshot, Error>::failure(
                Error(ErrorCode::ParseError, std::string("Failed to parse snapshot JSON: ") + e.what())
            );
        } catch (const std::exception& e) {
            return Result<Snapshot, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to load snapshot: ") + e.what())
            );
        }
    }

    Result<std::vector<SnapshotMetadata>, Error> SnapshotStore::list() const {
        std::vector<SnapshotMetadata> snapshots;

        if (!fs::exists(root_)) {
            return Result<std::vector<SnapshotMetadata>, Error>::success(snapshots);
        }

        try {
            for (const auto& entry : fs::directory_iterator(root_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    const std::string name = entry.path().stem().string();

                    if (name == ".baseline") {
                        continue;
                    }

                    if (auto result = load(name); result.is_ok()) {
                        snapshots.push_back(result.value().metadata);
                    }
                }
            }

            std::ranges::sort(snapshots,
                              [](const SnapshotMetadata& a, const SnapshotMetadata& b) {
                                  return a.created_at > b.created_at;
                              });

            return Result<std::vector<SnapshotMetadata>, Error>::success(std::move(snapshots));
        } catch (const std::exception& e) {
            return Result<std::vector<SnapshotMetadata>, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to list snapshots: ") + e.what())
            );
        }
    }

    Result<void, Error> SnapshotStore::remove(const std::string& name) const
    {
        const fs::path path = snapshot_path(name);

        if (!fs::exists(path)) {
            return Result<void, Error>::failure(
                Error(ErrorCode::NotFound, "Snapshot not found: " + name)
            );
        }

        try {
            fs::remove(path);

            // Clear baseline if this was the baseline
            auto baseline = get_baseline();
            if (baseline && *baseline == name) {
                (void)clear_baseline();
            }

            return Result<void, Error>::success();
        } catch (const std::exception& e) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to remove snapshot: ") + e.what())
            );
        }
    }

    bool SnapshotStore::exists(const std::string& name) const {
        return fs::exists(snapshot_path(name));
    }

    fs::path SnapshotStore::snapshot_path(const std::string& name) const {
        return root_ / (name + ".json");
    }

    Result<void, Error> SnapshotStore::set_baseline(const std::string& name) const
    {
        if (!exists(name)) {
            return Result<void, Error>::failure(
                Error(ErrorCode::NotFound, "Snapshot not found: " + name)
            );
        }

        if (auto dir_result = ensure_directory(); dir_result.is_err()) {
            return dir_result;
        }

        try {
            std::ofstream file(baseline_file());
            file << name;
            return Result<void, Error>::success();
        } catch (const std::exception& e) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to set baseline: ") + e.what())
            );
        }
    }

    std::optional<std::string> SnapshotStore::get_baseline() const {
        if (!fs::exists(baseline_file())) {
            return std::nullopt;
        }

        try {
            std::ifstream file(baseline_file());
            std::string name;
            std::getline(file, name);
            if (!name.empty() && exists(name)) {
                return name;
            }
            return std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }

    Result<void, Error> SnapshotStore::clear_baseline() const
    {
        if (fs::exists(baseline_file())) {
            try {
                fs::remove(baseline_file());
            } catch (const std::exception& e) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::IoError, std::string("Failed to clear baseline: ") + e.what())
                );
            }
        }
        return Result<void, Error>::success();
    }

    Result<ComparisonResult, Error> SnapshotStore::compare(
        const std::string& old_name,
        const std::string& new_name,
        const double significance_threshold
    ) const {
        auto old_result = load(old_name);
        if (old_result.is_err()) {
            return Result<ComparisonResult, Error>::failure(old_result.error());
        }

        auto new_result = load(new_name);
        if (new_result.is_err()) {
            return Result<ComparisonResult, Error>::failure(new_result.error());
        }

        return Result<ComparisonResult, Error>::success(
            compare_analyses(old_result.value().analysis, new_result.value().analysis, significance_threshold)
        );
    }

    Result<ComparisonResult, Error> SnapshotStore::compare_with_analysis(
        const std::string& snapshot_name,
        const analyzers::AnalysisResult& current,
        const double significance_threshold
    ) const {
        auto snapshot_result = load(snapshot_name);
        if (snapshot_result.is_err()) {
            return Result<ComparisonResult, Error>::failure(snapshot_result.error());
        }

        return Result<ComparisonResult, Error>::success(
            compare_analyses(snapshot_result.value().analysis, current, significance_threshold)
        );
    }

    // =============================================================================
    // Comparison Functions
    // =============================================================================

    ComparisonResult compare_analyses(
        const analyzers::AnalysisResult& old_result,
        const analyzers::AnalysisResult& new_result,
        const double significance_threshold
    ) {
        auto percent_change = [](const Duration old_time, const Duration delta) -> double {
            if (old_time.count() <= 0) {
                return 0.0;
            }
            return 100.0 * static_cast<double>(delta.count()) / static_cast<double>(old_time.count());
        };

        auto sum_file_compile_time = [](const std::vector<analyzers::FileAnalysisResult>& files) -> Duration {
            Duration total = Duration::zero();
            for (const auto& file : files) {
                total += file.compile_time;
            }
            return total;
        };

        ComparisonResult result;
        result.significance_threshold_percent = significance_threshold * 100.0;

        // Overall build time change
        auto old_time = old_result.performance.total_build_time;
        auto new_time = new_result.performance.total_build_time;
        result.build_time_delta = new_time - old_time;

        result.build_time_percent_change = percent_change(old_time, result.build_time_delta);

        result.translation_unit.old_time = sum_file_compile_time(old_result.files);
        result.translation_unit.new_time = sum_file_compile_time(new_result.files);
        result.translation_unit.delta = result.translation_unit.new_time - result.translation_unit.old_time;
        result.translation_unit.percent_change =
            percent_change(result.translation_unit.old_time, result.translation_unit.delta);

        result.headers.old_time = old_result.dependencies.total_include_time;
        result.headers.new_time = new_result.dependencies.total_include_time;
        result.headers.delta = result.headers.new_time - result.headers.old_time;
        result.headers.percent_change = percent_change(result.headers.old_time, result.headers.delta);

        result.templates.old_time = old_result.templates.total_template_time;
        result.templates.new_time = new_result.templates.total_template_time;
        result.templates.delta = result.templates.new_time - result.templates.old_time;
        result.templates.percent_change = percent_change(result.templates.old_time, result.templates.delta);

        // File count change
        result.file_count_delta =
            static_cast<int64_t>(new_result.files.size()) -
            static_cast<int64_t>(old_result.files.size());

        std::unordered_map<std::string, const analyzers::FileAnalysisResult*> old_files;
        std::unordered_map<std::string, const analyzers::FileAnalysisResult*> new_files;

        for (const auto& file : old_result.files) {
            old_files[file.file.string()] = &file;
        }
        for (const auto& file : new_result.files) {
            new_files[file.file.string()] = &file;
        }

        // Find regressions, improvements, new files, removed files
        for (const auto& [path, old_file] : old_files) {
            if (auto it = new_files.find(path); it == new_files.end()) {
                result.removed_files.emplace_back(path);
            } else {
                const auto* new_file = it->second;
                auto delta = new_file->compile_time - old_file->compile_time;
                const double percent = percent_change(old_file->compile_time, delta);

                if (std::abs(percent) > significance_threshold * 100.0) {
                    ComparisonResult::FileChange change;
                    change.file = path;
                    change.old_time = old_file->compile_time;
                    change.new_time = new_file->compile_time;
                    change.delta = delta;
                    change.percent_change = percent;

                    if (delta.count() > 0) {
                        result.regressions.push_back(change);
                    } else {
                        result.improvements.push_back(change);
                    }
                }
            }
        }

        for (const auto& path : new_files | std::views::keys) {
            if (!old_files.contains(path)) {
                result.new_files.emplace_back(path);
            }
        }

        auto sort_by_magnitude = [](const ComparisonResult::FileChange& a,
                                    const ComparisonResult::FileChange& b) {
            return std::abs(a.delta.count()) > std::abs(b.delta.count());
        };
        std::ranges::sort(result.regressions, sort_by_magnitude);
        std::ranges::sort(result.improvements, sort_by_magnitude);

        std::unordered_map<std::string, const analyzers::DependencyAnalysisResult::HeaderInfo*> old_headers;
        std::unordered_map<std::string, const analyzers::DependencyAnalysisResult::HeaderInfo*> new_headers;

        for (const auto& h : old_result.dependencies.headers) {
            old_headers[h.path.string()] = &h;
        }
        for (const auto& h : new_result.dependencies.headers) {
            new_headers[h.path.string()] = &h;
        }

        for (const auto& [path, old_h] : old_headers) {
            if (auto it = new_headers.find(path); it != new_headers.end()) {
                if (const auto* new_h = it->second; old_h->inclusion_count != new_h->inclusion_count ||
                    old_h->total_parse_time != new_h->total_parse_time) {
                    ComparisonResult::HeaderChange change;
                    change.header = path;
                    change.old_inclusions = old_h->inclusion_count;
                    change.new_inclusions = new_h->inclusion_count;
                    change.old_time = old_h->total_parse_time;
                    change.new_time = new_h->total_parse_time;

                    if (new_h->inclusion_count > old_h->inclusion_count ||
                        new_h->total_parse_time > old_h->total_parse_time) {
                        result.header_regressions.push_back(change);
                    } else {
                        result.header_improvements.push_back(change);
                    }
                }
            }
        }

        std::unordered_map<std::string, const analyzers::TemplateAnalysisResult::TemplateInfo*> old_templates;
        std::unordered_map<std::string, const analyzers::TemplateAnalysisResult::TemplateInfo*> new_templates;

        for (const auto& t : old_result.templates.templates) {
            old_templates[t.name] = &t;
        }
        for (const auto& t : new_result.templates.templates) {
            new_templates[t.name] = &t;
        }

        for (const auto& [name, old_t] : old_templates) {
            if (auto it = new_templates.find(name); it != new_templates.end()) {
                if (const auto* new_t = it->second; old_t->instantiation_count != new_t->instantiation_count ||
                    old_t->total_time != new_t->total_time) {
                    ComparisonResult::TemplateChange change;
                    change.name = name;
                    change.old_count = old_t->instantiation_count;
                    change.new_count = new_t->instantiation_count;
                    change.old_time = old_t->total_time;
                    change.new_time = new_t->total_time;

                    if (new_t->instantiation_count > old_t->instantiation_count ||
                        new_t->total_time > old_t->total_time) {
                        result.template_regressions.push_back(change);
                    } else {
                        result.template_improvements.push_back(change);
                    }
                }
            }
        }

        return result;
    }
}
