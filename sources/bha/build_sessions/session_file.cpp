#include "bha/build_sessions/session_file.hpp"

#include "bha/utils/file_utils.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

namespace bha::build_sessions {
    namespace {
        using json = nlohmann::json;

        BuildStepRole role_from_string(const std::string_view role) {
            if (role == "configure") return BuildStepRole::Configure;
            if (role == "generate") return BuildStepRole::Generate;
            if (role == "build") return BuildStepRole::Build;
            if (role == "compile") return BuildStepRole::Compile;
            if (role == "link") return BuildStepRole::Link;
            if (role == "custom") return BuildStepRole::Custom;
            if (role == "test") return BuildStepRole::Test;
            if (role == "install") return BuildStepRole::Install;
            return BuildStepRole::Unknown;
        }

        BuildSystemType build_system_from_string(const std::string_view value) {
            if (value == "CMake") return BuildSystemType::CMake;
            if (value == "Ninja") return BuildSystemType::Ninja;
            if (value == "Make") return BuildSystemType::Make;
            if (value == "MSBuild") return BuildSystemType::MSBuild;
            if (value == "Bazel") return BuildSystemType::Bazel;
            if (value == "Buck2") return BuildSystemType::Buck2;
            if (value == "Meson") return BuildSystemType::Meson;
            if (value == "SCons") return BuildSystemType::SCons;
            if (value == "XCode") return BuildSystemType::XCode;
            return BuildSystemType::Unknown;
        }

        EvidenceKind evidence_from_string(const std::string_view value) {
            if (value == "observed") return EvidenceKind::Observed;
            if (value == "derived") return EvidenceKind::Derived;
            return EvidenceKind::Unavailable;
        }

        TimingDomain timing_domain_from_string(const std::string_view value) {
            if (value == "wall-clock") return TimingDomain::WallClock;
            if (value == "cpu") return TimingDomain::Cpu;
            return TimingDomain::None;
        }

        TimingAggregation timing_aggregation_from_string(const std::string_view value) {
            if (value == "exclusive") return TimingAggregation::Exclusive;
            if (value == "inclusive") return TimingAggregation::Inclusive;
            if (value == "wall-clock-responsibility") return TimingAggregation::WallClockResponsibility;
            return TimingAggregation::None;
        }

        MetricCapability parse_capability(const json& object) {
            MetricCapability capability;
            capability.metric = object.value("metric", "");
            capability.provenance.evidence = evidence_from_string(
                object.value("evidence", "unavailable")
            );
            capability.provenance.producer = object.value("producer", "");
            capability.provenance.producer_version = object.value("producer_version", "");
            capability.provenance.capture_mode = object.value("capture_mode", "");
            capability.provenance.scope = object.value("scope", "");
            capability.provenance.timing_domain = timing_domain_from_string(
                object.value("timing_domain", "none")
            );
            capability.provenance.timing_aggregation = timing_aggregation_from_string(
                object.value("timing_aggregation", "none")
            );
            capability.provenance.limitation = object.value("limitation", "");
            return capability;
        }

        json serialize_capability(const MetricCapability& capability) {
            return {
                {"metric", capability.metric},
                {"evidence", to_string(capability.provenance.evidence)},
                {"producer", capability.provenance.producer},
                {"producer_version", capability.provenance.producer_version},
                {"capture_mode", capability.provenance.capture_mode},
                {"scope", capability.provenance.scope},
                {"timing_domain", to_string(capability.provenance.timing_domain)},
                {"timing_aggregation", to_string(capability.provenance.timing_aggregation)},
                {"limitation", capability.provenance.limitation}
            };
        }

        json serialize_provenance(const MetricProvenance& provenance) {
            return {
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

        MetricProvenance parse_provenance(const json& object) {
            MetricProvenance provenance;
            provenance.evidence = evidence_from_string(
                object.value("evidence", "unavailable")
            );
            provenance.producer = object.value("producer", "");
            provenance.producer_version = object.value("producer_version", "");
            provenance.capture_mode = object.value("capture_mode", "");
            provenance.scope = object.value("scope", "");
            provenance.timing_domain = timing_domain_from_string(
                object.value("timing_domain", "none")
            );
            provenance.timing_aggregation = timing_aggregation_from_string(
                object.value("timing_aggregation", "none")
            );
            provenance.limitation = object.value("limitation", "");
            return provenance;
        }

        json serialize_optional_uint64(const std::optional<std::uint64_t>& value) {
            return value.has_value() ? json(*value) : json(nullptr);
        }

        json serialize_optional_double(const std::optional<double>& value) {
            return value.has_value() ? json(*value) : json(nullptr);
        }

        json serialize_paths(const std::vector<fs::path>& paths) {
            json result = json::array();
            for (const auto& path : paths) {
                result.push_back(path.generic_string());
            }
            return result;
        }

        Result<BuildCommandEvent, Error> parse_command(
            const json& object,
            const fs::path& source_hint
        ) {
            if (!object.is_object()) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error("BHA build-session command is not an object", source_hint.string())
                );
            }

            BuildCommandEvent event;
            event.id = object.value("id", "");
            event.role = role_from_string(object.value("role", "unknown"));
            event.command = object.value("command", "");
            event.working_directory = object.value("working_directory", "");
            event.target = object.value("target", "");
            event.language = object.value("language", "");
            event.source = object.value("source", "");

            if (object.contains("trace_file") && !object["trace_file"].is_null()) {
                event.trace_file = object["trace_file"].get<std::string>();
            }
            if (object.contains("outputs")) {
                if (!object["outputs"].is_array()) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error("BHA build-session outputs is not an array", source_hint.string())
                    );
                }
                for (const auto& output : object["outputs"]) {
                    if (!output.is_string()) {
                        return Result<BuildCommandEvent, Error>::failure(
                            Error::parse_error("BHA build-session outputs contains a non-string", source_hint.string())
                        );
                    }
                    event.outputs.emplace_back(output.get<std::string>());
                }
            }
            if (object.contains("output_sizes")) {
                if (!object["output_sizes"].is_array()) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error("BHA build-session output_sizes is not an array", source_hint.string())
                    );
                }
                for (const auto& output_size : object["output_sizes"]) {
                    if (!output_size.is_number_unsigned()) {
                        return Result<BuildCommandEvent, Error>::failure(
                            Error::parse_error("BHA build-session output_sizes contains a non-negative integer violation", source_hint.string())
                        );
                    }
                    event.output_sizes.push_back(output_size.get<std::uintmax_t>());
                }
            }
            event.test_name = object.value("test_name", "");

            const auto read_optional_uint64 = [&](const char* name, std::optional<std::uint64_t>& value) {
                if (!object.contains(name) || object[name].is_null()) {
                    return true;
                }
                if (!object[name].is_number_unsigned()) {
                    return false;
                }
                value = object[name].get<std::uint64_t>();
                return true;
            };
            if (!read_optional_uint64("before_host_memory_used_kib", event.before_host_memory_used_kib) ||
                !read_optional_uint64("after_host_memory_used_kib", event.after_host_memory_used_kib)) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error("BHA build-session host memory value is not a non-negative integer", source_hint.string())
                );
            }
            const auto read_optional_double = [&](const char* name, std::optional<double>& value) {
                if (!object.contains(name) || object[name].is_null()) {
                    return true;
                }
                if (!object[name].is_number()) {
                    return false;
                }
                const double parsed = object[name].get<double>();
                if (!std::isfinite(parsed) || parsed < 0.0) {
                    return false;
                }
                value = parsed;
                return true;
            };
            if (!read_optional_double("before_cpu_load_average", event.before_cpu_load_average) ||
                !read_optional_double("after_cpu_load_average", event.after_cpu_load_average)) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error("BHA build-session CPU load value is invalid", source_hint.string())
                );
            }
            if (object.contains("standard_output") && !object["standard_output"].is_null()) {
                if (!object["standard_output"].is_string()) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error("BHA build-session standard_output is not a string", source_hint.string())
                    );
                }
                event.standard_output = object["standard_output"].get<std::string>();
            }
            if (object.contains("standard_error") && !object["standard_error"].is_null()) {
                if (!object["standard_error"].is_string()) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error("BHA build-session standard_error is not a string", source_hint.string())
                    );
                }
                event.standard_error = object["standard_error"].get<std::string>();
            }
            if (object.contains("dependency_ids")) {
                if (!object["dependency_ids"].is_array()) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error("BHA build-session dependency_ids is not an array", source_hint.string())
                    );
                }
                for (const auto& dependency : object["dependency_ids"]) {
                    if (!dependency.is_string()) {
                        return Result<BuildCommandEvent, Error>::failure(
                            Error::parse_error("BHA build-session dependency_ids contains a non-string", source_hint.string())
                        );
                    }
                    event.dependency_ids.push_back(dependency.get<std::string>());
                }
            }
            event.configuration = object.value("configuration", "");

            if (object.contains("time_start_ms") && !object["time_start_ms"].is_number_integer()) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error("BHA build-session time_start_ms is not an integer", source_hint.string())
                );
            }
            if (object.contains("duration_ns") && !object["duration_ns"].is_number_integer()) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error("BHA build-session duration_ns is not an integer", source_hint.string())
                );
            }
            if (object.contains("time_start_ms")) {
                event.start_time = Timestamp(std::chrono::milliseconds(
                    object["time_start_ms"].get<std::int64_t>()
                ));
            }
            const auto duration_ns = object.value("duration_ns", std::int64_t{0});
            if (duration_ns < 0) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error("BHA build-session duration_ns is negative", source_hint.string())
                );
            }
            event.duration = Duration(duration_ns);
            if (object.contains("result") && !object["result"].is_null()) {
                if (!object["result"].is_number_integer()) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error("BHA build-session result is not an integer", source_hint.string())
                    );
                }
                event.result = object["result"].get<int>();
            }

            event.timing_provenance.evidence = EvidenceKind::Observed;
            event.timing_provenance.producer = "bha-session-file";
            event.timing_provenance.capture_mode = "portable-build-session-v1";
            event.timing_provenance.scope = to_string(event.role);
            event.timing_provenance.timing_domain = TimingDomain::WallClock;
            event.timing_provenance.timing_aggregation = TimingAggregation::Exclusive;
            if (object.contains("timing_provenance")) {
                if (!object["timing_provenance"].is_object()) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error("BHA build-session timing_provenance is not an object", source_hint.string())
                    );
                }
                event.timing_provenance = parse_provenance(object["timing_provenance"]);
            }
            return Result<BuildCommandEvent, Error>::success(std::move(event));
        }

        json serialize_command(const BuildCommandEvent& event) {
            json result = {
                {"id", event.id},
                {"role", to_string(event.role)},
                {"command", event.command},
                {"working_directory", event.working_directory.generic_string()},
                {"target", event.target},
                {"language", event.language},
                {"source", event.source.generic_string()},
                {"configuration", event.configuration},
                {"duration_ns", event.duration.count()},
                {"trace_file", event.trace_file.has_value() ? json(event.trace_file->generic_string()) : json(nullptr)},
                {"outputs", serialize_paths(event.outputs)},
                {"output_sizes", event.output_sizes},
                {"test_name", event.test_name},
                {"before_host_memory_used_kib", serialize_optional_uint64(event.before_host_memory_used_kib)},
                {"after_host_memory_used_kib", serialize_optional_uint64(event.after_host_memory_used_kib)},
                {"before_cpu_load_average", serialize_optional_double(event.before_cpu_load_average)},
                {"after_cpu_load_average", serialize_optional_double(event.after_cpu_load_average)},
                {"standard_output", event.standard_output.has_value() ? json(*event.standard_output) : json(nullptr)},
                {"standard_error", event.standard_error.has_value() ? json(*event.standard_error) : json(nullptr)},
                {"dependency_ids", event.dependency_ids},
                {"timing_provenance", serialize_provenance(event.timing_provenance)}
            };
            if (event.start_time.has_value()) {
                result["time_start_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    event.start_time->time_since_epoch()
                ).count();
            } else {
                result["time_start_ms"] = nullptr;
            }
            result["result"] = event.result.has_value() ? json(*event.result) : json(nullptr);
            return result;
        }
    }

    Result<BuildSession, Error> BuildSessionFileParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        try {
            const auto document = json::parse(content);
            if (!document.is_object() || document.value("schema", "") != "bha.build-session" ||
                document.value("version", 0) != 1 || !document.contains("commands") ||
                !document["commands"].is_array()) {
                return Result<BuildSession, Error>::failure(
                    Error::parse_error("Not a BHA build-session v1 file", source_hint.string())
                );
            }

            BuildSession session;
            session.id = document.value("id", "");
            session.build_system = build_system_from_string(document.value("build_system", "Unknown"));
            session.build_system_version = document.value("build_system_version", "");
            session.configuration = document.value("configuration", "");
            session.platform = document.value("platform", "");
            session.instrumentation_hook = document.value("instrumentation_hook", "");
            session.dependency_graph_complete = document.value("dependency_graph_complete", false);

            if (document.contains("host_system") && !document["host_system"].is_null()) {
                if (!document["host_system"].is_object()) {
                    return Result<BuildSession, Error>::failure(
                        Error::parse_error("BHA build-session host_system is not an object", source_hint.string())
                    );
                }
                BuildHostSystemInfo host;
                const auto& object = document["host_system"];
                const auto read_string = [&](const char* name, std::optional<std::string>& value) {
                    if (!object.contains(name) || object[name].is_null()) return true;
                    if (!object[name].is_string()) return false;
                    value = object[name].get<std::string>();
                    return true;
                };
                const auto read_bool = [&](const char* name, std::optional<bool>& value) {
                    if (!object.contains(name) || object[name].is_null()) return true;
                    if (!object[name].is_boolean()) return false;
                    value = object[name].get<bool>();
                    return true;
                };
                const auto read_uint64 = [&](const char* name, std::optional<std::uint64_t>& value) {
                    if (!object.contains(name) || object[name].is_null()) return true;
                    if (!object[name].is_number_unsigned()) return false;
                    value = object[name].get<std::uint64_t>();
                    return true;
                };
                if (!read_string("os_name", host.os_name) ||
                    !read_string("os_platform", host.os_platform) ||
                    !read_string("os_release", host.os_release) ||
                    !read_string("os_version", host.os_version) ||
                    !read_string("processor_name", host.processor_name) ||
                    !read_string("vendor_string", host.vendor_string) ||
                    !read_bool("is_64_bits", host.is_64_bits) ||
                    !read_uint64("logical_cpu_count", host.logical_cpu_count) ||
                    !read_uint64("physical_cpu_count", host.physical_cpu_count) ||
                    !read_uint64("total_physical_memory_mib", host.total_physical_memory_mib) ||
                    !read_uint64("total_virtual_memory_mib", host.total_virtual_memory_mib)) {
                    return Result<BuildSession, Error>::failure(
                        Error::parse_error("BHA build-session host_system contains an invalid field", source_hint.string())
                    );
                }
                session.host_system = std::move(host);
            }
            for (const auto& command : document["commands"]) {
                const auto parsed = parse_command(command, source_hint);
                if (parsed.is_err()) {
                    return Result<BuildSession, Error>::failure(parsed.error());
                }
                session.commands.push_back(parsed.value());
            }
            if (document.contains("metric_capabilities")) {
                if (!document["metric_capabilities"].is_array()) {
                    return Result<BuildSession, Error>::failure(
                        Error::parse_error("BHA build-session metric_capabilities is not an array", source_hint.string())
                    );
                }
                for (const auto& capability : document["metric_capabilities"]) {
                    session.metric_capabilities.push_back(parse_capability(capability));
                }
            }
            return Result<BuildSession, Error>::success(std::move(session));
        } catch (const json::exception& exception) {
            return Result<BuildSession, Error>::failure(
                Error::parse_error(
                    std::string("Failed to parse BHA build-session file: ") + exception.what(),
                    source_hint.string()
                )
            );
        }
    }

    Result<BuildSession, Error> BuildSessionFileParser::parse_file(const fs::path& path) const {
        const auto content = utils::read_file(path);
        if (content.is_err()) {
            return Result<BuildSession, Error>::failure(content.error());
        }
        return parse_content(content.value(), path);
    }

    Result<void, Error> BuildSessionFileParser::attach_to_trace(
        BuildTrace& trace,
        const fs::path& path
    ) const {
        if (trace.build_session.has_value()) {
            return Result<void, Error>::failure(
                Error::invalid_argument("A build session is already attached", path.string())
            );
        }
        const auto session = parse_file(path);
        if (session.is_err()) {
            return Result<void, Error>::failure(session.error());
        }
        if (trace.build_system != BuildSystemType::Unknown &&
            trace.build_system != session.value().build_system) {
            return Result<void, Error>::failure(
                Error::invalid_argument("Build-session file targets a different build system", path.string())
            );
        }
        trace.build_system = session.value().build_system;
        trace.build_session = session.value();
        return Result<void, Error>::success();
    }

    Result<void, Error> BuildSessionFileParser::write_file(
        const BuildSession& session,
        const fs::path& path
    ) const {
        json document = {
            {"schema", "bha.build-session"},
            {"version", 1},
            {"id", session.id},
            {"build_system", to_string(session.build_system)},
            {"build_system_version", session.build_system_version},
            {"configuration", session.configuration},
            {"platform", session.platform},
            {"instrumentation_hook", session.instrumentation_hook},
            {"dependency_graph_complete", session.dependency_graph_complete},
            {"host_system", nullptr},
            {"commands", json::array()},
            {"metric_capabilities", json::array()}
        };
        if (session.host_system.has_value()) {
            const auto& host = *session.host_system;
            document["host_system"] = {
                {"os_name", host.os_name.has_value() ? json(*host.os_name) : json(nullptr)},
                {"os_platform", host.os_platform.has_value() ? json(*host.os_platform) : json(nullptr)},
                {"os_release", host.os_release.has_value() ? json(*host.os_release) : json(nullptr)},
                {"os_version", host.os_version.has_value() ? json(*host.os_version) : json(nullptr)},
                {"is_64_bits", host.is_64_bits.has_value() ? json(*host.is_64_bits) : json(nullptr)},
                {"logical_cpu_count", serialize_optional_uint64(host.logical_cpu_count)},
                {"physical_cpu_count", serialize_optional_uint64(host.physical_cpu_count)},
                {"total_physical_memory_mib", serialize_optional_uint64(host.total_physical_memory_mib)},
                {"total_virtual_memory_mib", serialize_optional_uint64(host.total_virtual_memory_mib)},
                {"processor_name", host.processor_name.has_value() ? json(*host.processor_name) : json(nullptr)},
                {"vendor_string", host.vendor_string.has_value() ? json(*host.vendor_string) : json(nullptr)}
            };
        }
        for (const auto& command : session.commands) {
            document["commands"].push_back(serialize_command(command));
        }
        for (const auto& capability : session.metric_capabilities) {
            document["metric_capabilities"].push_back(serialize_capability(capability));
        }
        return utils::write_file(path, document.dump(2));
    }

}  // namespace bha::build_sessions
