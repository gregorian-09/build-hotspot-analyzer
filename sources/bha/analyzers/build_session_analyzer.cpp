// Created by gregorian-rayne on 8/22/26.

#include "bha/analyzers/build_session_analyzer.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace bha::analyzers {
    namespace {

        MetricCapability capability(
            const std::string_view metric,
            const EvidenceKind evidence,
            const std::string_view producer,
            const std::string_view scope,
            const std::string_view limitation = ""
        ) {
            MetricCapability result;
            result.metric = metric;
            result.provenance.evidence = evidence;
            result.provenance.producer = producer;
            result.provenance.scope = scope;
            result.provenance.limitation = limitation;
            return result;
        }

        void add_capability(
            std::vector<MetricCapability>& capabilities,
            MetricCapability value
        ) {
            const auto existing = std::ranges::find(
                capabilities,
                value.metric,
                &MetricCapability::metric
            );
            if (existing == capabilities.end()) {
                capabilities.push_back(std::move(value));
            }
        }

        struct TimedEvent {
            const BuildCommandEvent* event = nullptr;
            Timestamp end_time{};
        };

        struct Boundary {
            Timestamp time{};
            int delta = 0;
        };

        bool compute_critical_path(
            const std::vector<TimedEvent>& events,
            const bool dependency_graph_complete,
            Duration& critical_path_time,
            std::vector<std::string>& critical_path
        ) {
            if (!dependency_graph_complete || events.empty()) {
                return false;
            }

            std::unordered_map<std::string, std::size_t> indexes;
            indexes.reserve(events.size());
            for (std::size_t index = 0; index < events.size(); ++index) {
                const auto& id = events[index].event->id;
                if (id.empty() || !indexes.emplace(id, index).second) {
                    return false;
                }
            }

            std::vector<Duration> best_duration(events.size(), Duration::zero());
            std::vector<std::vector<std::string>> best_path(events.size());
            std::vector<std::uint8_t> state(events.size(), 0);

            const auto visit = [&](const auto& self, const std::size_t index) -> bool {
                if (state[index] == 1) {
                    return false;
                }
                if (state[index] == 2) {
                    return true;
                }

                state[index] = 1;
                Duration predecessor_duration = Duration::zero();
                std::vector<std::string> predecessor_path;

                for (const auto& dependency_id : events[index].event->dependency_ids) {
                    const auto dependency = indexes.find(dependency_id);
                    if (dependency == indexes.end() || !self(self, dependency->second)) {
                        return false;
                    }

                    if (best_duration[dependency->second] > predecessor_duration) {
                        predecessor_duration = best_duration[dependency->second];
                        predecessor_path = best_path[dependency->second];
                    }
                }

                best_duration[index] = predecessor_duration + events[index].event->duration;
                best_path[index] = std::move(predecessor_path);
                best_path[index].push_back(events[index].event->id);
                state[index] = 2;
                return true;
            };

            for (std::size_t index = 0; index < events.size(); ++index) {
                if (!visit(visit, index)) {
                    return false;
                }
                if (best_duration[index] > critical_path_time) {
                    critical_path_time = best_duration[index];
                    critical_path = best_path[index];
                }
            }

            return !critical_path.empty();
        }

    }  // namespace

    Result<AnalysisResult, Error> BuildSessionAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& /*options*/
    ) const {
        AnalysisResult result;
        if (!trace.build_session.has_value()) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        const auto& session = *trace.build_session;
        auto& analysis = result.build_session;
        analysis.total_commands = session.commands.size();
        analysis.metric_capabilities = session.metric_capabilities;
        analysis.host_system = session.host_system;
        for (const auto& command : session.commands) {
            if (command.role == BuildStepRole::Compile && command.trace_file.has_value()) {
                ++analysis.compile_trace_references;
            }
        }

        const bool has_host_system_value = session.host_system.has_value() && (
            session.host_system->os_name.has_value() ||
            session.host_system->os_platform.has_value() ||
            session.host_system->os_release.has_value() ||
            session.host_system->os_version.has_value() ||
            session.host_system->is_64_bits.has_value() ||
            session.host_system->logical_cpu_count.has_value() ||
            session.host_system->physical_cpu_count.has_value() ||
            session.host_system->total_physical_memory_mib.has_value() ||
            session.host_system->total_virtual_memory_mib.has_value() ||
            session.host_system->processor_name.has_value() ||
            session.host_system->vendor_string.has_value()
        );
        auto host_system_capability = capability(
            "build.host.system",
            has_host_system_value ? EvidenceKind::Observed : EvidenceKind::Unavailable,
            "cmake-instrumentation",
            "build-session",
            has_host_system_value
                ? ""
                : "CMake staticSystemInformation was not captured or contained no modeled fields"
        );
        host_system_capability.provenance.capture_mode = "api-v1-index";
        analysis.metric_capabilities.push_back(std::move(host_system_capability));

        auto compile_trace_capability = capability(
            "build.compile_trace.reference",
            analysis.compile_trace_references > 0
                ? EvidenceKind::Observed
                : EvidenceKind::Unavailable,
            "cmake-instrumentation",
            "compile",
            analysis.compile_trace_references > 0
                ? ""
                : "No compile snippet provided a CMake v1.1 traceFile reference"
        );
        compile_trace_capability.provenance.capture_mode = "api-v1.1-snippet";
        analysis.metric_capabilities.push_back(std::move(compile_trace_capability));

        for (const auto& command : session.commands) {
            auto step = std::ranges::find(
                analysis.step_metrics,
                command.role,
                &BuildStepAnalysis::role
            );
            if (step == analysis.step_metrics.end()) {
                analysis.step_metrics.push_back({});
                step = std::prev(analysis.step_metrics.end());
                step->role = command.role;
            }

            ++step->total_commands;
            if (command.has_exact_timing()) {
                ++step->timed_commands;
                step->wall_clock_time += command.duration;
            }
            if (command.result.has_value()) {
                ++step->result_observations;
                if (*command.result == 0) {
                    ++step->successful_commands;
                } else {
                    ++step->failed_commands;
                }
            }
        }

        auto& host = analysis.host_telemetry;
        const auto observe_memory = [&host](
            const std::optional<std::uint64_t> value
        ) {
            if (!value.has_value()) {
                return;
            }
            ++host.memory_samples;
            if (!host.peak_memory_used_kib.has_value() ||
                *value > *host.peak_memory_used_kib) {
                host.peak_memory_used_kib = value;
            }
        };
        const auto observe_before_cpu = [&host](
            const std::optional<double> value
        ) {
            if (!value.has_value()) {
                return;
            }
            ++host.cpu_load_samples;
            if (!host.peak_before_cpu_load_average.has_value() ||
                *value > *host.peak_before_cpu_load_average) {
                host.peak_before_cpu_load_average = value;
            }
        };
        const auto observe_after_cpu = [&host](
            const std::optional<double> value
        ) {
            if (!value.has_value()) {
                return;
            }
            ++host.cpu_load_samples;
            if (!host.peak_after_cpu_load_average.has_value() ||
                *value > *host.peak_after_cpu_load_average) {
                host.peak_after_cpu_load_average = value;
            }
        };
        for (const auto& command : session.commands) {
            observe_memory(command.before_host_memory_used_kib);
            observe_memory(command.after_host_memory_used_kib);
            observe_before_cpu(command.before_cpu_load_average);
            observe_after_cpu(command.after_cpu_load_average);
        }

        auto memory_capability = capability(
            "build.host.memory_used_peak",
            host.peak_memory_used_kib.has_value()
                ? EvidenceKind::Derived
                : EvidenceKind::Unavailable,
            "BuildSessionAnalyzer",
            "build-session",
            host.peak_memory_used_kib.has_value()
                ? ""
                : "CMake dynamicSystemInformation memory samples were not captured"
        );
        memory_capability.provenance.capture_mode = "dynamicSystemInformation";
        host.metric_capabilities.push_back(std::move(memory_capability));

        auto cpu_capability = capability(
            "build.host.cpu_load_average_peak",
            host.cpu_load_samples > 0
                ? EvidenceKind::Derived
                : EvidenceKind::Unavailable,
            "BuildSessionAnalyzer",
            "build-session",
            host.cpu_load_samples > 0
                ? ""
                : "CMake dynamicSystemInformation CPU load samples were not captured"
        );
        cpu_capability.provenance.capture_mode = "dynamicSystemInformation";
        host.metric_capabilities.push_back(std::move(cpu_capability));

        std::ranges::sort(
            analysis.step_metrics,
            [](const BuildStepAnalysis& left, const BuildStepAnalysis& right) {
                return static_cast<std::underlying_type_t<BuildStepRole>>(left.role) <
                    static_cast<std::underlying_type_t<BuildStepRole>>(right.role);
            }
        );

        for (const auto& step : analysis.step_metrics) {
            const std::string scope = std::string("role:") + to_string(step.role);
            auto wall_time = capability(
                "build.step.wall_time",
                step.timed_commands == step.total_commands
                    ? EvidenceKind::Derived
                    : EvidenceKind::Unavailable,
                "BuildSessionAnalyzer",
                scope,
                step.timed_commands == step.total_commands
                    ? ""
                    : "At least one command in this role has no exact producer timing"
            );
            wall_time.provenance.capture_mode = "producer-command-events";
            wall_time.provenance.timing_domain = TimingDomain::WallClock;
            wall_time.provenance.timing_aggregation = TimingAggregation::Exclusive;
            analysis.metric_capabilities.push_back(std::move(wall_time));

            auto result_status = capability(
                "build.step.result",
                step.result_observations == step.total_commands
                    ? EvidenceKind::Observed
                    : EvidenceKind::Unavailable,
                "BuildSessionAnalyzer",
                scope,
                step.result_observations == step.total_commands
                    ? ""
                    : "The producer did not provide an exit result for every command in this role"
            );
            result_status.provenance.capture_mode = "producer-command-events";
            analysis.metric_capabilities.push_back(std::move(result_status));
        }

        std::vector<TimedEvent> timed_events;
        timed_events.reserve(session.commands.size());
        for (const auto& command : session.commands) {
            if (!command.has_exact_timing()) {
                continue;
            }

            timed_events.push_back({
                &command,
                *command.start_time + command.duration
            });
        }

        analysis.timed_commands = timed_events.size();
        add_capability(
            analysis.metric_capabilities,
            capability(
                "build.command.wall_time",
                timed_events.empty() ? EvidenceKind::Unavailable : EvidenceKind::Observed,
                "build-session",
                "command",
                timed_events.empty() ? "No command has exact producer timing" : ""
            )
        );

        if (timed_events.empty()) {
            add_capability(
                analysis.metric_capabilities,
                capability(
                    "build.scheduler",
                    EvidenceKind::Unavailable,
                    "build-session",
                    "session",
                    "No complete-timing command events were captured"
                )
            );
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        const auto first_start = std::ranges::min_element(
            timed_events,
            {},
            [](const TimedEvent& event) { return *event.event->start_time; }
        )->event->start_time.value();
        const auto last_end = std::ranges::max_element(
            timed_events,
            {},
            [](const TimedEvent& event) { return event.end_time; }
        )->end_time;

        analysis.wall_clock_time = last_end - first_start;
        for (const auto& event : timed_events) {
            analysis.serial_time += event.event->duration;
        }

        std::vector<Boundary> boundaries;
        boundaries.reserve(timed_events.size() * 2);
        for (const auto& event : timed_events) {
            boundaries.push_back({*event.event->start_time, 1});
            boundaries.push_back({event.end_time, -1});
        }
        std::ranges::sort(boundaries, [](const Boundary& left, const Boundary& right) {
            if (left.time != right.time) {
                return left.time < right.time;
            }
            return left.delta < right.delta;
        });

        std::size_t active = 0;
        for (const auto& boundary : boundaries) {
            if (boundary.delta < 0) {
                active -= static_cast<std::size_t>(-boundary.delta);
            } else {
                active += static_cast<std::size_t>(boundary.delta);
                analysis.peak_parallelism = std::max(analysis.peak_parallelism, active);
            }
        }

        if (analysis.wall_clock_time > Duration::zero()) {
            analysis.average_parallelism =
                static_cast<double>(analysis.serial_time.count()) /
                static_cast<double>(analysis.wall_clock_time.count());
        }

        const bool all_commands_timed = analysis.timed_commands == analysis.total_commands;
        add_capability(
            analysis.metric_capabilities,
            capability(
                "build.scheduler.parallelism",
                all_commands_timed ? EvidenceKind::Derived : EvidenceKind::Unavailable,
                "build-session",
                "session",
                all_commands_timed ? "" : "Some commands do not have exact timing"
            )
        );

        if (all_commands_timed && compute_critical_path(
                timed_events,
                session.dependency_graph_complete,
                analysis.critical_path_time,
                analysis.critical_path)) {
            add_capability(
                analysis.metric_capabilities,
                capability(
                    "build.scheduler.critical_path",
                    EvidenceKind::Derived,
                    "build-session",
                    "session"
                )
            );
        } else {
            analysis.critical_path_time = Duration::zero();
            analysis.critical_path.clear();
            add_capability(
                analysis.metric_capabilities,
                capability(
                    "build.scheduler.critical_path",
                    EvidenceKind::Unavailable,
                    "build-session",
                    "session",
                    "Exact timing and a complete acyclic dependency graph are required"
                )
            );
        }

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_build_session_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<BuildSessionAnalyzer>()
        );
    }

}  // namespace bha::analyzers
