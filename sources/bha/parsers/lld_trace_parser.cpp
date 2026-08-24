// Created by gregorian-rayne on 8/22/26.

#include "bha/parsers/lld_trace_parser.hpp"

#include "bha/utils/file_utils.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace bha::parsers {
    namespace {

        using json = nlohmann::json;

        Result<double, Error> required_nonnegative_number(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || !object[name].is_number()) {
                return Result<double, Error>::failure(
                    Error::parse_error(
                        std::string("LLD time trace event is missing numeric field: ") + name,
                        source_hint.string()
                    )
                );
            }

            const double value = object[name].get<double>();
            const double max_duration_microseconds =
                static_cast<double>(Duration::max().count()) / 1000.0;
            if (!std::isfinite(value) || value < 0.0 ||
                value > max_duration_microseconds) {
                return Result<double, Error>::failure(
                    Error::parse_error(
                        std::string("LLD time trace event has invalid numeric field: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<double, Error>::success(value);
        }

        std::optional<Duration> microseconds_to_duration(const double microseconds) {
            return utils::checked_duration_cast<Duration>(
                std::chrono::duration<double, std::micro>(microseconds)
            );
        }

        MetricCapability observed_capability(
            const std::string_view metric,
            const std::string_view scope
        ) {
            MetricCapability capability;
            capability.metric = metric;
            capability.provenance.evidence = EvidenceKind::Observed;
            capability.provenance.producer = "lld";
            capability.provenance.capture_mode = "--time-trace";
            capability.provenance.scope = scope;
            capability.provenance.timing_domain = TimingDomain::WallClock;
            capability.provenance.timing_aggregation = TimingAggregation::Inclusive;
            return capability;
        }

    }  // namespace

    Result<LinkerTrace, Error> LLDTimeTraceParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        try {
            const auto document = json::parse(content);
            if (!document.is_object() || !document.contains("traceEvents") ||
                !document["traceEvents"].is_array()) {
                return Result<LinkerTrace, Error>::failure(
                    Error::parse_error(
                        "Invalid LLD time trace: missing traceEvents array",
                        source_hint.string()
                    )
                );
            }

            LinkerTrace trace;
            trace.id = source_hint.generic_string();
            bool has_execute_linker = false;
            bool has_total_execute_linker = false;
            bool has_total_lto = false;
            bool has_lto_event = false;

            for (const auto& event_json : document["traceEvents"]) {
                if (!event_json.is_object() || !event_json.contains("ph") ||
                    !event_json["ph"].is_string() || event_json["ph"] != "X") {
                    continue;
                }
                if (!event_json.contains("name") || !event_json["name"].is_string()) {
                    return Result<LinkerTrace, Error>::failure(
                        Error::parse_error(
                            "LLD time trace X event is missing a string name",
                            source_hint.string()
                        )
                    );
                }

                const auto timestamp = required_nonnegative_number(event_json, "ts", source_hint);
                if (timestamp.is_err()) {
                    return Result<LinkerTrace, Error>::failure(timestamp.error());
                }
                const auto duration = required_nonnegative_number(event_json, "dur", source_hint);
                if (duration.is_err()) {
                    return Result<LinkerTrace, Error>::failure(duration.error());
                }

                const auto start_offset = microseconds_to_duration(timestamp.value());
                const auto event_duration = microseconds_to_duration(duration.value());
                if (!start_offset.has_value() || !event_duration.has_value()) {
                    return Result<LinkerTrace, Error>::failure(
                        Error::parse_error(
                            "LLD time trace event exceeds the supported duration representation",
                            source_hint.string()
                        )
                    );
                }

                LinkerTraceEvent event;
                event.name = event_json["name"].get<std::string>();
                event.start_offset = *start_offset;
                event.duration = *event_duration;
                if (event_json.contains("args") && event_json["args"].is_object() &&
                    event_json["args"].contains("detail") &&
                    event_json["args"]["detail"].is_string()) {
                    event.detail = event_json["args"]["detail"].get<std::string>();
                }
                trace.events.push_back(event);

                if (event.name == "ExecuteLinker") {
                    if (has_execute_linker) {
                        return Result<LinkerTrace, Error>::failure(
                            Error::parse_error(
                                "LLD time trace contains duplicate ExecuteLinker events",
                                source_hint.string()
                            )
                        );
                    }
                    has_execute_linker = true;
                    trace.execute_linker_time = event.duration;
                } else if (event.name == "Total ExecuteLinker") {
                    if (has_total_execute_linker) {
                        return Result<LinkerTrace, Error>::failure(
                            Error::parse_error(
                                "LLD time trace contains duplicate Total ExecuteLinker events",
                                source_hint.string()
                            )
                        );
                    }
                    has_total_execute_linker = true;
                    if (!has_execute_linker) {
                        trace.execute_linker_time = event.duration;
                    }
                } else if (event.name == "Total LTO") {
                    if (has_total_lto) {
                        return Result<LinkerTrace, Error>::failure(
                            Error::parse_error(
                                "LLD time trace contains duplicate Total LTO events",
                                source_hint.string()
                            )
                        );
                    }
                    has_total_lto = true;
                    trace.lto_time = event.duration;
                } else if (event.name == "LTO") {
                    if (has_lto_event) {
                        return Result<LinkerTrace, Error>::failure(
                            Error::parse_error(
                                "LLD time trace contains duplicate LTO events",
                                source_hint.string()
                            )
                        );
                    }
                    has_lto_event = true;
                    if (!has_total_lto) {
                        trace.lto_time = event.duration;
                    }
                }
            }

            if (trace.events.empty()) {
                return Result<LinkerTrace, Error>::failure(
                    Error::parse_error("LLD time trace contains no timed events", source_hint.string())
                );
            }
            if (!trace.execute_linker_time.has_value() && !trace.lto_time.has_value()) {
                return Result<LinkerTrace, Error>::failure(
                    Error::parse_error(
                        "LLD time trace contains no supported linker or LTO summary event",
                        source_hint.string()
                    )
                );
            }

            if (trace.execute_linker_time.has_value()) {
                trace.metric_capabilities.push_back(
                    observed_capability(
                        "linker.trace.wall_time",
                        has_execute_linker ? "ExecuteLinker" : "Total ExecuteLinker"
                    )
                );
            }
            if (trace.lto_time.has_value()) {
                trace.metric_capabilities.push_back(
                    observed_capability("lto.wall_time", has_total_lto ? "Total LTO" : "LTO")
                );
            }
            return Result<LinkerTrace, Error>::success(std::move(trace));
        } catch (const json::exception& exception) {
            return Result<LinkerTrace, Error>::failure(
                Error::parse_error(
                    std::string("Failed to parse LLD time trace: ") + exception.what(),
                    source_hint.string()
                )
            );
        }
    }

    Result<LinkerTrace, Error> LLDTimeTraceParser::parse_file(
        const fs::path& path
    ) const {
        const auto content = utils::read_file(path);
        if (content.is_err()) {
            return Result<LinkerTrace, Error>::failure(content.error());
        }
        return parse_content(content.value(), path);
    }

}  // namespace bha::parsers
