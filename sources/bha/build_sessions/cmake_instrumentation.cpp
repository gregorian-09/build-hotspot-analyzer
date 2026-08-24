// Created by gregorian-rayne on 8/22/26.

#include "bha/build_sessions/cmake_instrumentation.hpp"

#include "bha/parsers/clang_parser.hpp"
#include "bha/utils/file_utils.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <chrono>
#include <limits>
#include <set>
#include <utility>

namespace bha::build_sessions {
    namespace {

        using json = nlohmann::json;

        BuildStepRole role_from_string(const std::string_view role) {
            if (role == "configure") return BuildStepRole::Configure;
            if (role == "generate") return BuildStepRole::Generate;
            if (role == "build" || role == "cmakeBuild") return BuildStepRole::Build;
            if (role == "compile") return BuildStepRole::Compile;
            if (role == "link") return BuildStepRole::Link;
            if (role == "custom") return BuildStepRole::Custom;
            if (role == "test" || role == "ctest") return BuildStepRole::Test;
            if (role == "install" || role == "cmakeInstall") return BuildStepRole::Install;
            return BuildStepRole::Unknown;
        }

        bool role_supports_captured_output(const BuildStepRole role) {
            return role == BuildStepRole::Compile ||
                role == BuildStepRole::Link ||
                role == BuildStepRole::Custom ||
                role == BuildStepRole::Test ||
                role == BuildStepRole::Install;
        }

        Result<std::optional<std::string>, Error> optional_command_output(
            const json& object,
            const char* name,
            const BuildStepRole role,
            const fs::path& source_hint
        ) {
            if (!object.contains(name)) {
                return Result<std::optional<std::string>, Error>::success(std::nullopt);
            }
            if (object["version"]["minor"].get<int>() < 1 ||
                !role_supports_captured_output(role)) {
                return Result<std::optional<std::string>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake instrumentation ") + name +
                            " is only valid for version 1.1 compile, link, custom, test, or install snippets",
                        source_hint.string()
                    )
                );
            }
            if (!object[name].is_string()) {
                return Result<std::optional<std::string>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake instrumentation ") + name + " must be a string",
                        source_hint.string()
                    )
                );
            }
            return Result<std::optional<std::string>, Error>::success(
                object[name].get<std::string>()
            );
        }

        Timestamp timestamp_from_milliseconds(const double milliseconds) {
            return Timestamp(std::chrono::duration_cast<Timestamp::duration>(
                std::chrono::duration<double, std::milli>(milliseconds)
            ));
        }

        Duration duration_from_milliseconds(const double milliseconds) {
            return std::chrono::duration_cast<Duration>(
                std::chrono::duration<double, std::milli>(milliseconds)
            );
        }

        Result<double, Error> required_number(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || !object[name].is_number()) {
                return Result<double, Error>::failure(
                    Error::parse_error(
                        std::string("CMake instrumentation snippet is missing numeric field: ") + name,
                        source_hint.string()
                    )
                );
            }

            const double value = object[name].get<double>();
            if (!std::isfinite(value) || value < 0.0) {
                return Result<double, Error>::failure(
                    Error::parse_error(
                        std::string("CMake instrumentation snippet has invalid numeric field: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<double, Error>::success(value);
        }

        Result<std::optional<std::uint64_t>, Error> optional_memory_value(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || object[name].is_null()) {
                return Result<std::optional<std::uint64_t>, Error>::success(std::nullopt);
            }
            if (!object[name].is_number()) {
                return Result<std::optional<std::uint64_t>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake dynamic system information has an invalid memory field: ") + name,
                        source_hint.string()
                    )
                );
            }

            const long double value = object[name].get<long double>();
            if (!std::isfinite(value) || value < 0.0L ||
                std::floor(value) != value ||
                value > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
                return Result<std::optional<std::uint64_t>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake dynamic system information has an invalid memory field: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<std::optional<std::uint64_t>, Error>::success(
                static_cast<std::uint64_t>(value)
            );
        }

        Result<std::optional<double>, Error> optional_load_value(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || object[name].is_null()) {
                return Result<std::optional<double>, Error>::success(std::nullopt);
            }
            if (!object[name].is_number()) {
                return Result<std::optional<double>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake dynamic system information has an invalid CPU load field: ") + name,
                        source_hint.string()
                    )
                );
            }

            const double value = object[name].get<double>();
            if (!std::isfinite(value) || value < 0.0) {
                return Result<std::optional<double>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake dynamic system information has an invalid CPU load field: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<std::optional<double>, Error>::success(value);
        }

        Result<std::optional<std::string>, Error> optional_static_string(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || object[name].is_null()) {
                return Result<std::optional<std::string>, Error>::success(std::nullopt);
            }
            if (!object[name].is_string() || object[name].get<std::string>().empty()) {
                return Result<std::optional<std::string>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake static system information has an invalid string field: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<std::optional<std::string>, Error>::success(
                object[name].get<std::string>()
            );
        }

        Result<std::optional<bool>, Error> optional_static_bool(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || object[name].is_null()) {
                return Result<std::optional<bool>, Error>::success(std::nullopt);
            }
            if (!object[name].is_boolean()) {
                return Result<std::optional<bool>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake static system information has an invalid boolean field: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<std::optional<bool>, Error>::success(object[name].get<bool>());
        }

        Result<std::optional<std::uint64_t>, Error> optional_static_integer(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || object[name].is_null()) {
                return Result<std::optional<std::uint64_t>, Error>::success(std::nullopt);
            }
            if (!object[name].is_number()) {
                return Result<std::optional<std::uint64_t>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake static system information has an invalid integer field: ") + name,
                        source_hint.string()
                    )
                );
            }

            const long double value = object[name].get<long double>();
            if (!std::isfinite(value) || value <= 0.0L || std::floor(value) != value ||
                value > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
                return Result<std::optional<std::uint64_t>, Error>::failure(
                    Error::parse_error(
                        std::string("CMake static system information has an invalid integer field: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<std::optional<std::uint64_t>, Error>::success(
                static_cast<std::uint64_t>(value)
            );
        }

        Result<BuildHostSystemInfo, Error> parse_static_host_info(
            const json& object,
            const fs::path& source_hint
        ) {
            BuildHostSystemInfo result;
            const auto read_string = [&](
                const char* name,
                std::optional<std::string>& destination
            ) -> Result<void, Error> {
                const auto value = optional_static_string(object, name, source_hint);
                if (value.is_err()) {
                    return Result<void, Error>::failure(value.error());
                }
                destination = value.value();
                return Result<void, Error>::success();
            };
            const auto read_integer = [&](
                const char* name,
                std::optional<std::uint64_t>& destination
            ) -> Result<void, Error> {
                const auto value = optional_static_integer(object, name, source_hint);
                if (value.is_err()) {
                    return Result<void, Error>::failure(value.error());
                }
                destination = value.value();
                return Result<void, Error>::success();
            };

            for (const auto& field : {
                std::pair{"OSName", &result.os_name},
                std::pair{"OSPlatform", &result.os_platform},
                std::pair{"OSRelease", &result.os_release},
                std::pair{"OSVersion", &result.os_version},
                std::pair{"processorName", &result.processor_name},
                std::pair{"vendorString", &result.vendor_string}
            }) {
                const auto status = read_string(field.first, *field.second);
                if (status.is_err()) {
                    return Result<BuildHostSystemInfo, Error>::failure(status.error());
                }
            }

            const auto is_64_bits = optional_static_bool(object, "is64Bits", source_hint);
            if (is_64_bits.is_err()) {
                return Result<BuildHostSystemInfo, Error>::failure(is_64_bits.error());
            }
            result.is_64_bits = is_64_bits.value();

            for (const auto& field : {
                std::pair{"numberOfLogicalCPU", &result.logical_cpu_count},
                std::pair{"numberOfPhysicalCPU", &result.physical_cpu_count},
                std::pair{"totalPhysicalMemory", &result.total_physical_memory_mib},
                std::pair{"totalVirtualMemory", &result.total_virtual_memory_mib}
            }) {
                const auto status = read_integer(field.first, *field.second);
                if (status.is_err()) {
                    return Result<BuildHostSystemInfo, Error>::failure(status.error());
                }
            }
            return Result<BuildHostSystemInfo, Error>::success(std::move(result));
        }

        bool has_supported_data_version(const json& object) {
            if (!object.contains("version") || !object["version"].is_object()) {
                return false;
            }
            const auto& version = object["version"];
            return version.contains("major") && version["major"].is_number_integer() &&
                   version.contains("minor") && version["minor"].is_number_integer() &&
                   version["major"].get<int>() == 1 &&
                   version["minor"].get<int>() >= 0 &&
                   version["minor"].get<int>() <= 1;
        }

    }  // namespace

    Result<BuildCommandEvent, Error> CMakeInstrumentationParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        try {
            const auto object = json::parse(content);
            if (!object.is_object() || !object.contains("role") ||
                !object["role"].is_string()) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error("Not a CMake instrumentation snippet", source_hint.string())
                );
            }
            if (!has_supported_data_version(object)) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error(
                        "Unsupported CMake instrumentation data version",
                        source_hint.string()
                    )
                );
            }

            const auto start_result = required_number(object, "timeStart", source_hint);
            if (start_result.is_err()) {
                return Result<BuildCommandEvent, Error>::failure(start_result.error());
            }
            const auto duration_result = required_number(object, "duration", source_hint);
            if (duration_result.is_err()) {
                return Result<BuildCommandEvent, Error>::failure(duration_result.error());
            }

            BuildCommandEvent event;
            event.id = source_hint.empty()
                ? object.value("command", std::string{})
                : source_hint.generic_string();
            if (event.id.empty()) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error("CMake instrumentation snippet has no stable event identity", source_hint.string())
                );
            }

            event.role = role_from_string(object.value("role", "unknown"));
            if (event.role == BuildStepRole::Unknown) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error("CMake instrumentation snippet has an unsupported role", source_hint.string())
                );
            }
            event.command = object.value("command", "");
            event.working_directory = object.value("workingDir", "");
            event.target = object.value("target", "");
            event.language = object.value("language", "");
            event.source = object.value("source", "");

            const auto standard_output = optional_command_output(
                object,
                "stdout",
                event.role,
                source_hint
            );
            if (standard_output.is_err()) {
                return Result<BuildCommandEvent, Error>::failure(standard_output.error());
            }
            const auto standard_error = optional_command_output(
                object,
                "stderr",
                event.role,
                source_hint
            );
            if (standard_error.is_err()) {
                return Result<BuildCommandEvent, Error>::failure(standard_error.error());
            }
            if (event.role == BuildStepRole::Test && standard_error.value().has_value() &&
                !standard_error.value()->empty()) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error(
                        "CMake instrumentation test stderr must be merged into stdout",
                        source_hint.string()
                    )
                );
            }
            event.standard_output = standard_output.value();
            event.standard_error = standard_error.value();

            if (object.contains("traceFile")) {
                if (object["version"]["minor"].get<int>() < 1 ||
                    event.role != BuildStepRole::Compile) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error(
                            "CMake instrumentation traceFile is only valid for version 1.1 compile snippets",
                            source_hint.string()
                        )
                    );
                }
                if (!object["traceFile"].is_null()) {
                    if (!object["traceFile"].is_string() ||
                        object["traceFile"].get<std::string>().empty()) {
                        return Result<BuildCommandEvent, Error>::failure(
                            Error::parse_error(
                                "CMake instrumentation traceFile must be a non-empty string or null",
                                source_hint.string()
                            )
                        );
                    }
                    event.trace_file = object["traceFile"].get<std::string>();
                }
            }
            event.start_time = timestamp_from_milliseconds(start_result.value());
            event.duration = duration_from_milliseconds(duration_result.value());

            if (object.contains("result") && !object["result"].is_null()) {
                if (!object["result"].is_number_integer()) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error(
                            "CMake instrumentation snippet has a non-integer result",
                            source_hint.string()
                        )
                    );
                }
                event.result = object["result"].get<int>();
            } else if (event.role != BuildStepRole::Build) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error(
                        "CMake instrumentation snippet is missing result for a command role",
                        source_hint.string()
                    )
                );
            }

            if (object.contains("outputs") && !object["outputs"].is_array()) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error(
                        "CMake instrumentation outputs must be an array",
                        source_hint.string()
                    )
                );
            }
            if (object.contains("outputs")) {
                for (const auto& output : object["outputs"]) {
                    if (!output.is_string()) {
                        return Result<BuildCommandEvent, Error>::failure(
                            Error::parse_error(
                                "CMake instrumentation outputs must contain only strings",
                                source_hint.string()
                            )
                        );
                    }
                    event.outputs.emplace_back(output.get<std::string>());
                }
            }
            if (object.contains("outputSizes") && !object["outputSizes"].is_array()) {
                return Result<BuildCommandEvent, Error>::failure(
                    Error::parse_error(
                        "CMake instrumentation outputSizes must be an array",
                        source_hint.string()
                    )
                );
            }
            if (object.contains("outputSizes")) {
                for (const auto& output_size : object["outputSizes"]) {
                    if (!output_size.is_number_unsigned()) {
                        return Result<BuildCommandEvent, Error>::failure(
                            Error::parse_error(
                                "CMake instrumentation snippet has a non-negative output size that is not an integer",
                                source_hint.string()
                            )
                        );
                    }
                    event.output_sizes.push_back(output_size.get<std::uintmax_t>());
                }
            }
            event.test_name = object.value("testName", "");
            event.configuration = object.value("config", "");

            if (object.contains("dynamicSystemInformation")) {
                if (!object["dynamicSystemInformation"].is_object()) {
                    return Result<BuildCommandEvent, Error>::failure(
                        Error::parse_error(
                            "CMake instrumentation dynamic system information is not an object",
                            source_hint.string()
                        )
                    );
                }
                const auto& dynamic = object["dynamicSystemInformation"];
                const auto before_memory = optional_memory_value(
                    dynamic,
                    "beforeHostMemoryUsed",
                    source_hint
                );
                if (before_memory.is_err()) {
                    return Result<BuildCommandEvent, Error>::failure(before_memory.error());
                }
                const auto after_memory = optional_memory_value(
                    dynamic,
                    "afterHostMemoryUsed",
                    source_hint
                );
                if (after_memory.is_err()) {
                    return Result<BuildCommandEvent, Error>::failure(after_memory.error());
                }
                const auto before_cpu = optional_load_value(
                    dynamic,
                    "beforeCPULoadAverage",
                    source_hint
                );
                if (before_cpu.is_err()) {
                    return Result<BuildCommandEvent, Error>::failure(before_cpu.error());
                }
                const auto after_cpu = optional_load_value(
                    dynamic,
                    "afterCPULoadAverage",
                    source_hint
                );
                if (after_cpu.is_err()) {
                    return Result<BuildCommandEvent, Error>::failure(after_cpu.error());
                }
                event.before_host_memory_used_kib = before_memory.value();
                event.after_host_memory_used_kib = after_memory.value();
                event.before_cpu_load_average = before_cpu.value();
                event.after_cpu_load_average = after_cpu.value();
            }

            event.timing_provenance.evidence = EvidenceKind::Observed;
            event.timing_provenance.producer = "cmake-instrumentation";
            event.timing_provenance.capture_mode = "api-v1";
            event.timing_provenance.scope = to_string(event.role);
            event.timing_provenance.timing_domain = TimingDomain::WallClock;
            event.timing_provenance.timing_aggregation = TimingAggregation::Exclusive;

            return Result<BuildCommandEvent, Error>::success(std::move(event));
        } catch (const json::exception& exception) {
            return Result<BuildCommandEvent, Error>::failure(
                Error::parse_error(
                    std::string("Failed to parse CMake instrumentation snippet: ") + exception.what(),
                    source_hint.string()
                )
            );
        }
    }

    Result<BuildCommandEvent, Error> CMakeInstrumentationParser::parse_file(
        const fs::path& path
    ) const {
        const auto content = utils::read_file(path);
        if (content.is_err()) {
            return Result<BuildCommandEvent, Error>::failure(content.error());
        }
        return parse_content(content.value(), path);
    }

    Result<BuildSession, Error> CMakeInstrumentationParser::parse_index_file(
        const fs::path& path
    ) const {
        const auto content = utils::read_file(path);
        if (content.is_err()) {
            return Result<BuildSession, Error>::failure(content.error());
        }

        try {
            const auto index = json::parse(content.value());
            if (!index.is_object() || !index.contains("snippets") ||
                !index["snippets"].is_array()) {
                return Result<BuildSession, Error>::failure(
                    Error::parse_error("Not a CMake instrumentation index file", path.string())
                );
            }
            if (!has_supported_data_version(index)) {
                return Result<BuildSession, Error>::failure(
                    Error::parse_error(
                        "Unsupported CMake instrumentation index data version",
                        path.string()
                    )
                );
            }

            fs::path data_directory;
            if (index.contains("dataDir") && index["dataDir"].is_string()) {
                data_directory = index["dataDir"].get<std::string>();
            } else {
                data_directory = path.parent_path().parent_path();
            }

            BuildSession session;
            session.id = path.generic_string();
            session.build_system = BuildSystemType::CMake;
            if (index.contains("hook") && index["hook"].is_string()) {
                session.instrumentation_hook = index["hook"].get<std::string>();
            }
            if (index.contains("staticSystemInformation")) {
                if (!index["staticSystemInformation"].is_object()) {
                    return Result<BuildSession, Error>::failure(
                        Error::parse_error(
                            "CMake instrumentation static system information is not an object",
                            path.string()
                        )
                    );
                }
                const auto host = parse_static_host_info(
                    index["staticSystemInformation"],
                    path
                );
                if (host.is_err()) {
                    return Result<BuildSession, Error>::failure(host.error());
                }
                session.host_system = host.value();
            }

            for (const auto& snippet : index["snippets"]) {
                if (!snippet.is_string()) {
                    return Result<BuildSession, Error>::failure(
                        Error::parse_error(
                            "CMake instrumentation index contains a non-string snippet path",
                            path.string()
                        )
                    );
                }

                fs::path snippet_path = snippet.get<std::string>();
                if (snippet_path.is_relative()) {
                    snippet_path = data_directory / snippet_path;
                }
                const auto event = parse_file(snippet_path);
                if (event.is_err()) {
                    return Result<BuildSession, Error>::failure(event.error());
                }
                auto parsed_event = event.value();
                if (parsed_event.trace_file.has_value() &&
                    parsed_event.trace_file->is_relative()) {
                    parsed_event.trace_file = data_directory / *parsed_event.trace_file;
                }
                session.commands.push_back(std::move(parsed_event));
            }

            MetricCapability timing;
            timing.metric = "build.command.wall_time";
            timing.provenance.evidence = EvidenceKind::Observed;
            timing.provenance.producer = "cmake-instrumentation";
            timing.provenance.capture_mode = "api-v1-index";
            timing.provenance.scope = "command";
            timing.provenance.timing_domain = TimingDomain::WallClock;
            timing.provenance.timing_aggregation = TimingAggregation::Exclusive;
            session.metric_capabilities.push_back(std::move(timing));
            return Result<BuildSession, Error>::success(std::move(session));
        } catch (const json::exception& exception) {
            return Result<BuildSession, Error>::failure(
                Error::parse_error(
                    std::string("Failed to parse CMake instrumentation index: ") + exception.what(),
                    path.string()
                )
            );
        }
    }

    Result<void, Error> CMakeInstrumentationParser::attach_to_trace(
        BuildTrace& trace,
        const fs::path& path
    ) const {
        const auto session_result = parse_index_file(path);
        if (session_result.is_err()) {
            return Result<void, Error>::failure(session_result.error());
        }

        const auto& session = session_result.value();
        if (trace.build_session.has_value()) {
            return Result<void, Error>::failure(
                Error::invalid_argument(
                    "A CMake instrumentation session is already attached",
                    path.string()
                )
            );
        }
        if (trace.build_system != BuildSystemType::Unknown &&
            trace.build_system != BuildSystemType::CMake) {
            return Result<void, Error>::failure(
                Error::invalid_argument(
                    "CMake instrumentation cannot be attached to a different build system",
                    path.string()
                )
            );
        }
        if (trace.compiler != CompilerType::Unknown && trace.compiler != CompilerType::Clang) {
            return Result<void, Error>::failure(
                Error::invalid_argument(
                    "CMake compileTrace references are Clang traces, but another compiler is already attached",
                    path.string()
                )
            );
        }

        const BuildCommandEvent* whole_build_event = nullptr;
        std::size_t whole_build_event_count = 0;
        for (const auto& command : session.commands) {
            if (command.role != BuildStepRole::Build) {
                continue;
            }
            ++whole_build_event_count;
            whole_build_event = &command;
        }

        parsers::ClangTraceParser clang_parser;
        std::set<fs::path> referenced_files;
        std::vector<CompilationUnit> referenced_units;
        for (const auto& command : session.commands) {
            if (!command.trace_file.has_value()) {
                continue;
            }
            const auto& trace_file = *command.trace_file;
            if (!referenced_files.insert(trace_file).second) {
                return Result<void, Error>::failure(
                    Error::parse_error(
                        "CMake instrumentation references the same compile trace more than once",
                        trace_file.string()
                    )
                );
            }

            const auto unit_result = clang_parser.parse_file(trace_file);
            if (unit_result.is_err()) {
                return Result<void, Error>::failure(
                    Error::parse_error(
                        "Failed to parse CMake-referenced Clang trace: " + unit_result.error().message(),
                        trace_file.string()
                    )
                );
            }
            referenced_units.push_back(unit_result.value());
        }

        trace.build_session = session;
        trace.build_system = BuildSystemType::CMake;
        trace.compiler = CompilerType::Clang;
        trace.id = path.generic_string();
        if (trace.total_time == Duration::zero() &&
            whole_build_event_count == 1 &&
            whole_build_event != nullptr &&
            whole_build_event->has_exact_timing()) {
            trace.total_time = whole_build_event->duration;
        }
        for (auto& unit : referenced_units) {
            if (static_cast<std::uint8_t>(unit.template_evidence) >
                static_cast<std::uint8_t>(trace.template_evidence)) {
                trace.template_evidence = unit.template_evidence;
            }
            trace.units.push_back(std::move(unit));
        }
        return Result<void, Error>::success();
    }

    Result<BuildSession, Error> CMakeInstrumentationParser::parse_directory(
        const fs::path& directory
    ) const {
        if (!fs::is_directory(directory)) {
            return Result<BuildSession, Error>::failure(
                Error::not_found("CMake instrumentation directory not found", directory.string())
            );
        }

        BuildSession session;
        session.build_system = BuildSystemType::CMake;
        session.id = directory.generic_string();

        try {
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                    continue;
                }

                const auto content = utils::read_file(entry.path());
                if (content.is_err()) {
                    return Result<BuildSession, Error>::failure(content.error());
                }

                const auto object = json::parse(content.value());
                if (!object.is_object() || !object.contains("role")) {
                    continue;
                }

                const auto event = parse_content(content.value(), entry.path());
                if (event.is_err()) {
                    return Result<BuildSession, Error>::failure(event.error());
                }
                session.commands.push_back(event.value());
            }
        } catch (const json::exception& exception) {
            return Result<BuildSession, Error>::failure(
                Error::parse_error(
                    std::string("Failed to parse CMake instrumentation directory: ") + exception.what(),
                    directory.string()
                )
            );
        } catch (const std::exception& exception) {
            return Result<BuildSession, Error>::failure(
                Error::io_error(
                    std::string("Failed to read CMake instrumentation directory: ") + exception.what(),
                    directory.string()
                )
            );
        }

        if (session.commands.empty()) {
            return Result<BuildSession, Error>::failure(
                Error::parse_error("No CMake instrumentation snippets found", directory.string())
            );
        }

        MetricCapability timing;
        timing.metric = "build.command.wall_time";
        timing.provenance.evidence = EvidenceKind::Observed;
        timing.provenance.producer = "cmake-instrumentation";
        timing.provenance.capture_mode = "api-v1";
        timing.provenance.scope = "command";
        timing.provenance.timing_domain = TimingDomain::WallClock;
        timing.provenance.timing_aggregation = TimingAggregation::Exclusive;
        session.metric_capabilities.push_back(std::move(timing));
        return Result<BuildSession, Error>::success(std::move(session));
    }

}  // namespace bha::build_sessions
