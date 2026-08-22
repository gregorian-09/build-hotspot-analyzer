// Created by gregorian-rayne on 8/22/26.

#include "bha/build_sessions/cmake_instrumentation.hpp"

#include "bha/utils/file_utils.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <chrono>
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

            if (object.contains("outputs") && object["outputs"].is_array()) {
                for (const auto& output : object["outputs"]) {
                    if (output.is_string()) {
                        event.outputs.emplace_back(output.get<std::string>());
                    }
                }
            }
            if (object.contains("outputSizes") && object["outputSizes"].is_array()) {
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
                session.commands.push_back(event.value());
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
