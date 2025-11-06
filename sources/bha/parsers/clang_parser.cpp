//
// Created by gregorian on 15/10/2025.
//

#include "bha/parsers/clang_parser.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/path_utils.h"
#include "bha/utils/string_utils.h"
#include "bha/utils/hash_utils.h"
#include <unordered_map>
#include <algorithm>

namespace bha::parsers {

    core::Result<std::vector<core::CompilationUnit>> ClangTimeTraceParser::parse(
        const std::string_view file_path
    ) {
        const auto content = utils::read_file(file_path);
        if (!content) {
            return core::Result<std::vector<core::CompilationUnit>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Failed to read trace file: " + std::string(file_path)
            );
        }

        return parse_string(*content);
    }

    core::Result<std::vector<core::CompilationUnit>> ClangTimeTraceParser::parse_string(
        std::string_view content
    ) {
        try {
            simdjson::ondemand::parser parser;
            simdjson::padded_string json_data{std::string(content)};
            auto doc = parser.iterate(json_data);

            auto events_result = parse_trace_events(doc.value());
            if (!events_result.is_success()) {
                return core::Result<std::vector<core::CompilationUnit>>::failure(
                    events_result.error()
                );
            }

            const auto& events = events_result.value();

            std::string file_path = extract_file_path_from_events(events);

            auto unit_result = build_compilation_unit(events, file_path);
            if (!unit_result.is_success()) {
                return core::Result<std::vector<core::CompilationUnit>>::failure(
                    unit_result.error()
                );
            }

            std::vector<core::CompilationUnit> units;
            units.push_back(std::move(unit_result.value()));

            return core::Result<std::vector<core::CompilationUnit>>::success(std::move(units));

        } catch (const simdjson::simdjson_error& e) {
            return core::Result<std::vector<core::CompilationUnit>>::failure(
                core::ErrorCode::JSON_PARSE_ERROR,
                "Failed to parse Clang time trace JSON: " + std::string(e.what())
            );
        }
    }

    std::string ClangTimeTraceParser::get_format_name() const {
        return "clang-time-trace";
    }

    CompilerType ClangTimeTraceParser::get_compiler_type() const {
        return CompilerType::CLANG;
    }

    bool ClangTimeTraceParser::can_parse(const std::string_view file_path) const {
        if (const std::string ext = utils::get_extension(file_path); ext != ".json") {
            return false;
        }

        const auto content = utils::read_file(file_path);
        if (!content) {
            return false;
        }

        return utils::contains(*content, "traceEvents");
    }

    ParserCapabilities ClangTimeTraceParser::get_capabilities() const {
        ParserCapabilities caps;
        caps.supports_timing = true;
        caps.supports_templates = true;
        caps.supports_preprocessing = true;
        caps.supports_optimization = true;
        caps.supports_dependencies = false;
        return caps;
    }

    std::vector<std::string> ClangTimeTraceParser::get_supported_extensions() const {
        return {".json"};
    }

    core::Result<std::vector<ClangTimeTraceParser::TraceEvent>>
    ClangTimeTraceParser::parse_trace_events(simdjson::ondemand::document& doc) {
        std::vector<TraceEvent> events;

        try {
            for (auto trace_events = doc["traceEvents"]; auto event_value : trace_events) {
                TraceEvent event;

                if (auto name_result = event_value["name"].get_string(); name_result.error() == simdjson::SUCCESS) {
                    event.name = std::string(name_result.value());
                }

                if (auto ph_result = event_value["ph"].get_string(); ph_result.error() == simdjson::SUCCESS) {
                    event.phase = std::string(ph_result.value());
                }

                if (auto ts_result = event_value["ts"].get_uint64(); ts_result.error() == simdjson::SUCCESS) {
                    event.timestamp_us = ts_result.value();
                }

                if (auto dur_result = event_value["dur"].get_uint64(); dur_result.error() == simdjson::SUCCESS) {
                    event.duration_us = dur_result.value();
                }

                if (auto pid_result = event_value["pid"].get_int64(); pid_result.error() == simdjson::SUCCESS) {
                    event.pid = static_cast<int>(pid_result.value());
                }

                if (auto tid_result = event_value["tid"].get_int64(); tid_result.error() == simdjson::SUCCESS) {
                    event.tid = static_cast<int>(tid_result.value());
                }

                if (auto args_result = event_value["args"]; args_result.error() == simdjson::SUCCESS) {
                    auto args = args_result.value();
                    if (auto detail_result = args["detail"].get_string(); detail_result.error() == simdjson::SUCCESS) {
                        event.detail = std::string(detail_result.value());
                    }
                }

                events.push_back(std::move(event));
            }

        } catch (const simdjson::simdjson_error& e) {
            return core::Result<std::vector<TraceEvent>>::failure(
                core::ErrorCode::JSON_PARSE_ERROR,
                "Failed to parse trace events: " + std::string(e.what())
            );
        }

        return core::Result<std::vector<TraceEvent>>::success(std::move(events));
    }

    core::Result<core::CompilationUnit> ClangTimeTraceParser::build_compilation_unit(
        const std::vector<TraceEvent>& events,
        const std::string_view file_path
    ) {
        core::CompilationUnit unit;

        unit.file_path = file_path;
        unit.compiler_type = "clang";
        unit.id = utils::compute_hash_hex(file_path);
        unit.build_timestamp = std::chrono::system_clock::now();

        extract_timing_from_events(events, unit);
        extract_template_instantiations(events, unit);

        return core::Result<core::CompilationUnit>::success(std::move(unit));
    }

    void ClangTimeTraceParser::extract_timing_from_events(
        const std::vector<TraceEvent>& events,
        core::CompilationUnit& unit
    )
    {
        std::unordered_map<std::string, uint64_t> timing_map;

        for (const auto& event : events) {
            if (event.phase != "X") continue;

            timing_map[event.name] += event.duration_us;
        }

        if (timing_map.contains("ExecuteCompiler")) {
            unit.total_time_ms = microseconds_to_milliseconds(timing_map["ExecuteCompiler"]);
        } else if (timing_map.contains("Total ExecuteCompiler")) {
            unit.total_time_ms = microseconds_to_milliseconds(timing_map["Total ExecuteCompiler"]);
        }

        if (timing_map.contains("Source")) {
            unit.preprocessing_time_ms = microseconds_to_milliseconds(timing_map["Source"]);
        }

        if (timing_map.contains("Frontend")) {
            unit.parsing_time_ms = microseconds_to_milliseconds(timing_map["Frontend"]);
        }

        if (timing_map.contains("Backend")) {
            unit.codegen_time_ms = microseconds_to_milliseconds(timing_map["Backend"]);
        }

        if (timing_map.contains("OptModule")) {
            unit.optimization_time_ms = microseconds_to_milliseconds(timing_map["OptModule"]);
        } else if (timing_map.contains("Optimizer")) {
            unit.optimization_time_ms = microseconds_to_milliseconds(timing_map["Optimizer"]);
        }

        if (unit.total_time_ms == 0.0) {
            uint64_t total = 0;
            for (const auto& duration : timing_map | std::views::values) {
                total += duration;
            }
            unit.total_time_ms = microseconds_to_milliseconds(total);
        }
    }

    void ClangTimeTraceParser::extract_template_instantiations(
        const std::vector<TraceEvent>& events,
        core::CompilationUnit& unit
    )
    {
        for (const auto& event : events) {
            if (event.phase != "X") continue;

            if (utils::starts_with(event.name, "InstantiateClass") ||
                utils::starts_with(event.name, "InstantiateFunction") ||
                utils::starts_with(event.name, "ParseTemplate")) {

                core::TemplateInstantiation inst;
                inst.template_name = event.detail.empty() ? event.name : event.detail;
                inst.instantiation_context = event.name;
                inst.time_ms = microseconds_to_milliseconds(event.duration_us);
                inst.instantiation_depth = 0;

                unit.template_instantiations.push_back(std::move(inst));
            }
        }

        std::ranges::sort(unit.template_instantiations,
                          [](const auto& a, const auto& b) {
                              return a.time_ms > b.time_ms;
                          });
    }

    std::string ClangTimeTraceParser::extract_file_path_from_events(
        const std::vector<TraceEvent>& events
    ) {
        for (const auto& event : events) {
            if (event.name == "Source" && !event.detail.empty()) {
                return event.detail;
            }
        }

        for (const auto& event : events) {
            if (!event.detail.empty() &&
                (utils::ends_with(event.detail, ".cpp") ||
                 utils::ends_with(event.detail, ".cc") ||
                 utils::ends_with(event.detail, ".cxx") ||
                 utils::ends_with(event.detail, ".c"))) {
                return event.detail;
            }
        }

        return "unknown";
    }

    double ClangTimeTraceParser::microseconds_to_milliseconds(uint64_t us) {
        return static_cast<double>(us) / 1000.0;
    }

} // namespace bha::parsers