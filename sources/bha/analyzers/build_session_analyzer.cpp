// Created by gregorian-rayne on 8/22/26.

#include "bha/analyzers/build_session_analyzer.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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

        void set_capability(
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
            } else {
                *existing = std::move(value);
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

                const auto sum = utils::checked_add_duration(
                    predecessor_duration,
                    events[index].event->duration
                );
                if (!sum.has_value()) {
                    return false;
                }
                best_duration[index] = *sum;
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

        const auto add_output_bytes = [](
            std::optional<std::uint64_t>& total,
            const std::optional<std::string>& output,
            const std::underlying_type_t<BuildStepRole> role,
            std::unordered_set<std::underlying_type_t<BuildStepRole>>& overflowed_roles
        ) {
            if (!output.has_value() || overflowed_roles.contains(role)) {
                return;
            }
            const auto size = static_cast<std::uint64_t>(output->size());
            const auto sum = utils::checked_add(total.value_or(0), size);
            if (!sum.has_value()) {
                total.reset();
                overflowed_roles.insert(role);
                return;
            }
            total = *sum;
        };

        std::unordered_set<std::underlying_type_t<BuildStepRole>> stdout_overflowed_roles;
        std::unordered_set<std::underlying_type_t<BuildStepRole>> stderr_overflowed_roles;
        std::unordered_set<std::underlying_type_t<BuildStepRole>> duration_overflowed_roles;
        bool serial_time_overflow = false;

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
                const auto role = static_cast<std::underlying_type_t<BuildStepRole>>(command.role);
                if (!duration_overflowed_roles.contains(role)) {
                    const auto sum = utils::checked_add_duration(
                        step->wall_clock_time,
                        command.duration
                    );
                    if (sum.has_value()) {
                        step->wall_clock_time = *sum;
                    } else {
                        duration_overflowed_roles.insert(role);
                        step->wall_clock_time = Duration::zero();
                    }
                }
            }
            if (command.result.has_value()) {
                ++step->result_observations;
                if (*command.result == 0) {
                    ++step->successful_commands;
                } else {
                    ++step->failed_commands;
                }
            }
            if (command.standard_output.has_value() || command.standard_error.has_value()) {
                ++step->output_observations;
            }
            const auto role = static_cast<std::underlying_type_t<BuildStepRole>>(command.role);
            add_output_bytes(
                step->stdout_bytes,
                command.standard_output,
                role,
                stdout_overflowed_roles
            );
            add_output_bytes(
                step->stderr_bytes,
                command.standard_error,
                role,
                stderr_overflowed_roles
            );
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
            if (!value.has_value() || !std::isfinite(*value) || *value < 0.0) {
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
            if (!value.has_value() || !std::isfinite(*value) || *value < 0.0) {
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
            const auto role = static_cast<std::underlying_type_t<BuildStepRole>>(step.role);
            const bool output_overflowed =
                stdout_overflowed_roles.contains(role) || stderr_overflowed_roles.contains(role);
            const bool duration_overflowed = duration_overflowed_roles.contains(role);
            auto wall_time = capability(
                "build.step.wall_time",
                !duration_overflowed && step.timed_commands == step.total_commands
                    ? EvidenceKind::Derived
                    : EvidenceKind::Unavailable,
                "BuildSessionAnalyzer",
                scope,
                duration_overflowed
                    ? "Exact command durations overflowed the aggregate representation"
                    : step.timed_commands == step.total_commands
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

            auto output_bytes = capability(
                "build.step.output_bytes",
                !output_overflowed &&
                        step.output_observations > 0 &&
                        (step.stdout_bytes.has_value() || step.stderr_bytes.has_value())
                    ? EvidenceKind::Derived
                    : EvidenceKind::Unavailable,
                "BuildSessionAnalyzer",
                scope,
                output_overflowed
                    ? "Producer-captured output exceeded the aggregate representation"
                    : step.output_observations > 0
                        ? "Only streams present in producer snippets are summed"
                        : "CMake captureOutput was not captured for this role"
            );
            output_bytes.provenance.capture_mode = "captureOutput";
            output_bytes.provenance.timing_domain = TimingDomain::None;
            output_bytes.provenance.timing_aggregation = TimingAggregation::None;
            analysis.metric_capabilities.push_back(std::move(output_bytes));
        }

        std::vector<TimedEvent> timed_events;
        timed_events.reserve(session.commands.size());
        std::size_t exact_timed_commands = 0;
        bool end_time_overflow = false;
        for (const auto& command : session.commands) {
            if (!command.has_exact_timing()) {
                continue;
            }

            ++exact_timed_commands;
            const auto duration = std::chrono::duration_cast<Timestamp::duration>(command.duration);
            const auto end = utils::checked_add_duration(
                command.start_time->time_since_epoch(),
                duration
            );
            if (!end.has_value()) {
                end_time_overflow = true;
                continue;
            }

            timed_events.push_back({
                &command,
                Timestamp(*end)
            });
        }

        analysis.timed_commands = exact_timed_commands;
        set_capability(
            analysis.metric_capabilities,
            capability(
                "build.command.wall_time",
                !end_time_overflow && !timed_events.empty()
                    ? EvidenceKind::Observed
                    : EvidenceKind::Unavailable,
                "build-session",
                "command",
                end_time_overflow
                    ? "A command timestamp plus duration exceeded the platform timestamp representation"
                    : timed_events.empty() ? "No command has exact producer timing" : ""
            )
        );

        if (timed_events.empty() || end_time_overflow) {
            analysis.wall_clock_time = Duration::zero();
            analysis.serial_time = Duration::zero();
            add_capability(
                analysis.metric_capabilities,
                capability(
                    "build.scheduler",
                    EvidenceKind::Unavailable,
                    "build-session",
                    "session",
                    end_time_overflow
                        ? "A command timestamp plus duration exceeded the platform timestamp representation"
                        : "No complete-timing command events were captured"
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
            if (serial_time_overflow) {
                break;
            }
            const auto sum = utils::checked_add_duration(
                analysis.serial_time,
                event.event->duration
            );
            if (sum.has_value()) {
                analysis.serial_time = *sum;
            } else {
                serial_time_overflow = true;
                analysis.serial_time = Duration::zero();
            }
        }

        std::vector<Boundary> boundaries;
        boundaries.reserve(timed_events.size() * 2);
        for (const auto& event : timed_events) {
            // A zero-length event contributes no occupied interval. Adding an
            // end boundary before its start boundary would underflow `active`
            // during the half-open interval sweep.
            if (event.event->duration == Duration::zero()) {
                continue;
            }
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

        if (!serial_time_overflow && analysis.wall_clock_time > Duration::zero()) {
            analysis.average_parallelism =
                static_cast<double>(analysis.serial_time.count()) /
                static_cast<double>(analysis.wall_clock_time.count());
        }

        const bool all_commands_timed = !serial_time_overflow && !end_time_overflow &&
            analysis.timed_commands == analysis.total_commands;
        add_capability(
            analysis.metric_capabilities,
            capability(
                "build.scheduler.parallelism",
                all_commands_timed ? EvidenceKind::Derived : EvidenceKind::Unavailable,
                "build-session",
                "session",
                serial_time_overflow
                    ? "Exact command durations overflowed the aggregate representation"
                    : all_commands_timed ? "" : "Some commands do not have exact timing"
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
