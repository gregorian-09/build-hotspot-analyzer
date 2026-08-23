//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/clang_parser.hpp"
#include "bha/utils/file_utils.hpp"

#include <nlohmann/json.hpp>

#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <cstdint>

namespace bha::parsers {

    using json = nlohmann::json;

    namespace {

        constexpr std::string_view CLANG_TRACE_MARKER = "traceEvents";

        Duration microseconds_to_duration(const double us) {
            return std::chrono::duration_cast<Duration>(
                std::chrono::duration<double, std::micro>(us)
            );
        }

        struct TraceEvent {
            std::string name;
            std::string category;
            std::string phase;
            double timestamp = 0.0;
            double duration = 0.0;
            std::string detail;
            std::string file;
            int line = 0;
            std::int64_t process_id = 0;
            std::int64_t thread_id = 0;
            bool has_thread_identity = false;
        };

        struct IncludeProcessingResult {
            bool has_source_events = false;
            bool all_self_times_available = false;
        };

        Result<TraceEvent, Error> parse_event(
            const json& event_json,
            const fs::path& source_hint
        ) {
            if (!event_json.is_object()) {
                return Result<TraceEvent, Error>::failure(
                    Error::parse_error(
                        "Clang time trace event must be an object",
                        source_hint.string()
                    )
                );
            }

            TraceEvent event;

            if (!event_json.contains("name") || !event_json["name"].is_string()) {
                return Result<TraceEvent, Error>::failure(
                    Error::parse_error(
                        "Clang time trace event is missing a string name",
                        source_hint.string()
                    )
                );
            }
            event.name = event_json["name"].get<std::string>();

            if (!event_json.contains("ph") || !event_json["ph"].is_string()) {
                return Result<TraceEvent, Error>::failure(
                    Error::parse_error(
                        "Clang time trace event is missing a string phase",
                        source_hint.string()
                    )
                );
            }
            event.phase = event_json["ph"].get<std::string>();

            const auto parse_nonnegative_number = [&](
                const char* field_name,
                double& target,
                const bool required
            ) -> Result<bool, Error> {
                if (!event_json.contains(field_name)) {
                    if (required) {
                        return Result<bool, Error>::failure(
                            Error::parse_error(
                                std::string("Clang time trace complete event is missing ") + field_name,
                                source_hint.string()
                            )
                        );
                    }
                    return Result<bool, Error>::success(false);
                }
                if (!event_json[field_name].is_number()) {
                    return Result<bool, Error>::failure(
                        Error::parse_error(
                            std::string("Clang time trace field must be numeric: ") + field_name,
                            source_hint.string()
                        )
                    );
                }
                const double value = event_json[field_name].get<double>();
                if (!std::isfinite(value) || value < 0.0) {
                    return Result<bool, Error>::failure(
                        Error::parse_error(
                            std::string("Clang time trace field must be finite and non-negative: ") + field_name,
                            source_hint.string()
                        )
                    );
                }
                if (std::string_view(field_name) == "dur" &&
                    value > static_cast<double>(Duration::max().count()) / 1000.0) {
                    return Result<bool, Error>::failure(
                        Error::parse_error(
                            "Clang time trace duration exceeds the supported range",
                            source_hint.string()
                        )
                    );
                }
                target = value;
                return Result<bool, Error>::success(true);
            };

            const bool complete_event = event.phase == "X";
            const auto timestamp = parse_nonnegative_number(
                "ts",
                event.timestamp,
                complete_event
            );
            if (timestamp.is_err()) {
                return Result<TraceEvent, Error>::failure(timestamp.error());
            }
            const auto duration = parse_nonnegative_number(
                "dur",
                event.duration,
                complete_event
            );
            if (duration.is_err()) {
                return Result<TraceEvent, Error>::failure(duration.error());
            }

            if (event_json.contains("cat")) {
                if (!event_json["cat"].is_string()) {
                    return Result<TraceEvent, Error>::failure(
                        Error::parse_error(
                            "Clang time trace category must be a string",
                            source_hint.string()
                        )
                    );
                }
                event.category = event_json["cat"].get<std::string>();
            }

            if (event_json.contains("args")) {
                const auto& args = event_json["args"];
                if (!args.is_object()) {
                    return Result<TraceEvent, Error>::failure(
                        Error::parse_error(
                            "Clang time trace args must be an object",
                            source_hint.string()
                        )
                    );
                }
                const auto integer_fits_int64 = [](const json& value) {
                    return value.is_number_integer() &&
                        (!value.is_number_unsigned() ||
                         value.get<std::uint64_t>() <=
                             static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
                };
                if (args.contains("detail")) {
                    if (!args["detail"].is_string()) {
                        return Result<TraceEvent, Error>::failure(
                            Error::parse_error(
                                "Clang time trace detail must be a string",
                                source_hint.string()
                            )
                        );
                    }
                    event.detail = args["detail"].get<std::string>();
                }
                if (args.contains("file")) {
                    if (!args["file"].is_string()) {
                        return Result<TraceEvent, Error>::failure(
                            Error::parse_error(
                                "Clang time trace file must be a string",
                                source_hint.string()
                            )
                        );
                    }
                    event.file = args["file"].get<std::string>();
                }
                if (args.contains("line")) {
                    if (!integer_fits_int64(args["line"]) ||
                        args["line"].get<std::int64_t>() < 0 ||
                        args["line"].get<std::int64_t>() > std::numeric_limits<int>::max()) {
                        return Result<TraceEvent, Error>::failure(
                            Error::parse_error(
                                "Clang time trace line must be a non-negative integer",
                                source_hint.string()
                            )
                        );
                    }
                    event.line = args["line"].get<int>();
                }
            }

            const auto integer_fits_int64 = [](const json& value) {
                return value.is_number_integer() &&
                    (!value.is_number_unsigned() ||
                     value.get<std::uint64_t>() <=
                         static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
            };
            if (event_json.contains("pid") && !integer_fits_int64(event_json["pid"])) {
                return Result<TraceEvent, Error>::failure(
                    Error::parse_error(
                        "Clang time trace pid must be an integer",
                        source_hint.string()
                    )
                );
            }
            if (event_json.contains("tid") && !integer_fits_int64(event_json["tid"])) {
                return Result<TraceEvent, Error>::failure(
                    Error::parse_error(
                        "Clang time trace tid must be an integer",
                        source_hint.string()
                    )
                );
            }
            if (event_json.contains("pid") && event_json.contains("tid")) {
                event.process_id = event_json["pid"].get<std::int64_t>();
                event.thread_id = event_json["tid"].get<std::int64_t>();
                event.has_thread_identity = true;
            }

            return Result<TraceEvent, Error>::success(std::move(event));
        }

        bool is_source_file(const std::string& path) {
            // Check if path looks like a C/C++ source file (not a header)
            const auto pos = path.rfind('.');
            if (pos == std::string::npos) {
                return false;
            }

            const std::string ext = path.substr(pos);
            return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" ||
                   ext == ".C" || ext == ".CC" || ext == ".CPP" || ext == ".CXX";
        }

        std::optional<fs::path> extract_source_from_detail(const std::string& detail) {
            if (detail.empty()) {
                return std::nullopt;
            }

            if (is_source_file(detail)) {
                return fs::path(detail);
            }

            if (const auto colon_pos = detail.find(':'); colon_pos != std::string::npos) {
                const std::string prefix = detail.substr(0, colon_pos);
                if (is_source_file(prefix)) {
                    return fs::path(prefix);
                }
            }

            std::string tokenized = detail;
            for (char& ch : tokenized) {
                if (ch == '(' || ch == ')' || ch == ';' || ch == ',' || ch == '\t') {
                    ch = ' ';
                }
            }

            std::istringstream in(tokenized);
            std::string token;
            while (in >> token) {
                while (!token.empty() && (token.back() == ':' || token.back() == ']')) {
                    token.pop_back();
                }
                while (!token.empty() && (token.front() == '[' || token.front() == '\'' || token.front() == '"')) {
                    token.erase(token.begin());
                }
                if (is_source_file(token)) {
                    return fs::path(token);
                }
            }

            return std::nullopt;
        }

        fs::path extract_source_file(const std::vector<TraceEvent>& events) {
            // First try ExecuteCompiler event (as this is most reliable)
            for (const auto& event : events) {
                if (event.name == "ExecuteCompiler" || event.name == "Total ExecuteCompiler") {
                    if (!event.detail.empty()) {
                        return fs::path(event.detail);
                    }
                }
            }

            // Look for ParseDeclarationOrFunctionDefinition in a source file
            for (const auto& event : events) {
                if (event.name == "ParseDeclarationOrFunctionDefinition" && !event.detail.empty()) {
                    // Detail format: "/path/to/file.cc:line:col" or with spelling info
                    if (const auto colon_pos = event.detail.find(':'); colon_pos != std::string::npos) {
                        if (const std::string file_path = event.detail.substr(0, colon_pos);
                            is_source_file(file_path)) {
                            return fs::path(file_path);
                        }
                    }
                }
            }

            // Generic fallback for events that carry absolute source paths
            for (const auto& event : events) {
                if (auto source = extract_source_from_detail(event.detail); source.has_value()) {
                    return *source;
                }
            }

            // Fallback: first Source event that's a source file (not header)
            for (const auto& event : events) {
                if (event.name == "Source" && !event.detail.empty()) {
                    if (is_source_file(event.detail)) {
                        return fs::path(event.detail);
                    }
                }
            }

            return {};
        }

        void process_template_events(
            const std::vector<TraceEvent>& events,
            std::vector<TemplateInstantiation>& templates
        ) {
            std::unordered_map<std::string, TemplateInstantiation> template_map;

            for (const auto& event : events) {
                if (event.name == "InstantiateClass" ||
                    event.name == "InstantiateFunction") {

                    auto& tmpl = template_map[event.detail];
                    if (tmpl.name.empty()) {
                        tmpl.name = event.name;
                        tmpl.full_signature = event.detail;
                        tmpl.count = 0;

                        if (!event.file.empty()) {
                            tmpl.location.file = event.file;
                            tmpl.location.line = static_cast<std::size_t>(event.line);
                        }
                    }

                    tmpl.time += microseconds_to_duration(event.duration);
                    ++tmpl.count;
                }
            }

            templates.reserve(template_map.size());
            for (auto& tmpl : template_map | std::views::values) {
                templates.push_back(std::move(tmpl));
            }

            std::ranges::sort(templates,
                              [](const auto& a, const auto& b) {
                                  return a.time > b.time;
                              });
        }

        void process_include_events(
            const std::vector<TraceEvent>& events,
            std::vector<IncludeInfo>& includes,
            IncludeProcessingResult& processing_result
        ) {
            struct SourceEventInfo {
                std::string detail;
                double start_time;
                double end_time;
                double duration;
                std::size_t depth;
                std::int64_t process_id;
                std::int64_t thread_id;
                bool has_thread_identity;
                std::optional<Duration> self_parse_time;
                double child_duration = 0.0;
            };

            std::vector<SourceEventInfo> source_events;

            for (const auto& event : events) {
                if (event.name == "Source" && !event.detail.empty()) {
                    SourceEventInfo info;
                    info.detail = event.detail;
                    info.start_time = event.timestamp;
                    info.end_time = event.timestamp + event.duration;
                    info.duration = event.duration;
                    info.depth = 0;
                    info.process_id = event.process_id;
                    info.thread_id = event.thread_id;
                    info.has_thread_identity = event.has_thread_identity;
                    source_events.push_back(info);
                }
            }

            processing_result.has_source_events = !source_events.empty();
            processing_result.all_self_times_available = processing_result.has_source_events;

            // Source durations are inclusive. Subtract only validated,
            // same-thread nested intervals so self-time never double-counts
            // overlapping or cross-thread work.
            std::ranges::sort(source_events, [](const auto& lhs, const auto& rhs) {
                if (lhs.has_thread_identity != rhs.has_thread_identity) {
                    return lhs.has_thread_identity > rhs.has_thread_identity;
                }
                if (lhs.has_thread_identity && lhs.process_id != rhs.process_id) {
                    return lhs.process_id < rhs.process_id;
                }
                if (lhs.has_thread_identity && lhs.thread_id != rhs.thread_id) {
                    return lhs.thread_id < rhs.thread_id;
                }
                if (lhs.start_time != rhs.start_time) {
                    return lhs.start_time < rhs.start_time;
                }
                return lhs.end_time > rhs.end_time;
            });

            for (std::size_t group_start = 0; group_start < source_events.size();) {
                const auto& first = source_events[group_start];
                if (!first.has_thread_identity) {
                    processing_result.all_self_times_available = false;
                    ++group_start;
                    continue;
                }

                std::size_t group_end = group_start + 1;
                while (group_end < source_events.size() &&
                       source_events[group_end].has_thread_identity &&
                       source_events[group_end].process_id == first.process_id &&
                       source_events[group_end].thread_id == first.thread_id) {
                    ++group_end;
                }

                std::vector<std::size_t> stack;
                bool valid_nesting = true;
                for (std::size_t index = group_start; index < group_end; ++index) {
                    auto& current = source_events[index];
                    while (!stack.empty() &&
                           source_events[stack.back()].end_time <= current.start_time) {
                        stack.pop_back();
                    }

                    if (!stack.empty()) {
                        auto& parent = source_events[stack.back()];
                        if (current.start_time <= parent.start_time || current.end_time > parent.end_time) {
                            valid_nesting = false;
                            break;
                        }
                        parent.child_duration += current.duration;
                    }
                    stack.push_back(index);
                }

                if (!valid_nesting) {
                    processing_result.all_self_times_available = false;
                    group_start = group_end;
                    continue;
                }

                for (std::size_t index = group_start; index < group_end; ++index) {
                    const auto& current = source_events[index];
                    const double self_duration = current.duration - current.child_duration;
                    if (self_duration < 0.0) {
                        processing_result.all_self_times_available = false;
                        continue;
                    }
                    source_events[index].self_parse_time = microseconds_to_duration(self_duration);
                }
                group_start = group_end;
            }

            std::ranges::sort(source_events, [](const auto& a, const auto& b) {
                return a.start_time < b.start_time;
            });

            // Sweep-line algorithm for O(N log N) include depth calculation.
            // Replaces the O(N^2) nested-loop approach.
            // Maintains a min-heap of end times; for each event (processed in start_time order),
            // pop intervals that ended before or at this start, then remaining heap size = depth.
            std::priority_queue<double, std::vector<double>, std::greater<>> active_end_times;
            for (auto& ev : source_events) {
                while (!active_end_times.empty() && active_end_times.top() <= ev.start_time) {
                    active_end_times.pop();
                }
                ev.depth = active_end_times.size();
                active_end_times.push(ev.end_time);
            }

            struct IncludeStats {
                IncludeInfo info;
                bool self_time_available = true;
                Duration self_parse_time = Duration::zero();
            };
            std::unordered_map<std::string, IncludeStats> include_map;

            for (const auto& event : source_events) {
                auto& stats = include_map[event.detail];
                stats.info.header = event.detail;
                stats.info.parse_time += microseconds_to_duration(event.duration);
                stats.info.depth = std::max(stats.info.depth, event.depth);
                if (event.self_parse_time.has_value()) {
                    if (stats.self_time_available) {
                        stats.self_parse_time += *event.self_parse_time;
                    }
                } else {
                    stats.self_time_available = false;
                }
            }

            includes.reserve(include_map.size());
            for (auto& stats : include_map | std::views::values) {
                if (stats.self_time_available) {
                    stats.info.self_parse_time = stats.self_parse_time;
                }
                includes.push_back(std::move(stats.info));
            }

            std::ranges::sort(includes,
                              [](const auto& a, const auto& b) {
                                  return a.parse_time > b.parse_time;
                              });
        }

        void calculate_metrics(
            const std::vector<TraceEvent>& events,
            FileMetrics& metrics
        ) {
            Duration frontend_time = Duration::zero();
            Duration backend_time = Duration::zero();

            for (const auto& event : events) {
                const auto dur = microseconds_to_duration(event.duration);

                if (event.name == "Total ExecuteCompiler" || event.name == "ExecuteCompiler") {
                    metrics.total_time = dur;
                }
                else if (event.name == "Total Frontend") {
                    frontend_time = dur;
                }
                else if (event.name == "Total Backend") {
                    backend_time = dur;
                }
                else if (event.name == "Total Source") {
                    metrics.breakdown.preprocessing += dur;
                }
                else if (event.name == "Total ParseClass" || event.name == "ParseClass") {
                    metrics.breakdown.parsing += dur;
                }
                else if (event.name == "Total PerformPendingInstantiations" ||
                         event.name == "Total InstantiateClass" ||
                         event.name == "Total InstantiateFunction") {
                    metrics.breakdown.template_instantiation += dur;
                }
                else if (event.name == "Total CodeGen Function" ||
                         event.name == "Total PerFunctionPasses") {
                    metrics.breakdown.code_generation += dur;
                }
                else if (event.name == "Total OptModule" ||
                         event.name == "Total RunLoopPass" ||
                         event.name == "Total OptFunction") {
                    metrics.breakdown.optimization += dur;
                }
            }

            metrics.frontend_time = frontend_time;
            metrics.backend_time = backend_time;

            if (metrics.total_time == Duration::zero() && frontend_time != Duration::zero()) {
                metrics.total_time = frontend_time + backend_time;
            }
        }

    }  // namespace

    bool ClangTraceParser::can_parse(const fs::path& path) const {
        if (const auto ext = path.extension().string(); ext != ".json") {
            return false;
        }

        auto result = utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool ClangTraceParser::can_parse_content(std::string_view content) const {
        try {
            const auto trace_json = json::parse(content);
            return trace_json.is_object() &&
                trace_json.contains(CLANG_TRACE_MARKER) &&
                trace_json[CLANG_TRACE_MARKER].is_array();
        } catch (const json::parse_error&) {
            return false;
        }
    }

    Result<CompilationUnit, Error> ClangTraceParser::parse_file(
        const fs::path& path
    ) const {
        auto content_result = utils::read_file(path);
        if (content_result.is_err()) {
            return Result<CompilationUnit, Error>::failure(content_result.error());
        }

        // Extract source file hint from trace filename
        // Trace files are typically named: source.cc.json or source.cpp.json
        auto source_file = path;
        auto filename = path.filename().string();

        // Remove .json extension
        if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".json") {
            filename = filename.substr(0, filename.size() - 5);
        }
        source_file = path.parent_path() / filename;

        return parse_content(content_result.value(), source_file);
    }

    Result<CompilationUnit, Error> ClangTraceParser::parse_content(
        std::string_view content,
        const fs::path& source_hint
    ) const {
        json trace_json;

        try {
            trace_json = json::parse(content);
        } catch (const json::parse_error& e) {
            return Result<CompilationUnit, Error>::failure(
                Error::parse_error("Failed to parse JSON", e.what())
            );
        }

        if (!trace_json.contains("traceEvents") || !trace_json["traceEvents"].is_array()) {
            return Result<CompilationUnit, Error>::failure(
                Error::parse_error("Invalid Clang trace format", "missing traceEvents array")
            );
        }

        std::vector<TraceEvent> events;
        events.reserve(trace_json["traceEvents"].size());

        for (const auto& event_json : trace_json["traceEvents"]) {
            const auto event = parse_event(event_json, source_hint);
            if (event.is_err()) {
                return Result<CompilationUnit, Error>::failure(event.error());
            }
            events.push_back(std::move(event).value());
        }

        CompilationUnit unit;

        const auto detected_source = extract_source_file(events);
        unit.source_file = detected_source.empty() ? source_hint : detected_source;

        unit.metrics.path = unit.source_file;

        process_template_events(events, unit.templates);
        if (!unit.templates.empty()) {
            const bool has_location = std::ranges::any_of(
                unit.templates,
                [](const auto& tmpl) { return tmpl.location.has_location(); }
            );
            unit.template_evidence = has_location
                ? TemplateEvidence::PerSpecializationTimingWithLocations
                : TemplateEvidence::PerSpecializationTiming;
        }
        IncludeProcessingResult include_processing;
        process_include_events(events, unit.includes, include_processing);
        if (include_processing.has_source_events) {
            MetricCapability capability;
            capability.metric = "frontend.source_self_time";
            capability.provenance.evidence = EvidenceKind::Derived;
            capability.provenance.producer = "clang";
            capability.provenance.capture_mode = "-ftime-trace";
            capability.provenance.scope = "translation-unit";
            capability.provenance.timing_domain = TimingDomain::WallClock;
            capability.provenance.timing_aggregation = TimingAggregation::Exclusive;
            if (!include_processing.all_self_times_available) {
                capability.provenance.limitation =
                    "Some Source intervals lacked thread identity or valid nesting; their self-time is unavailable";
            }
            unit.metric_capabilities.push_back(std::move(capability));
        }
        calculate_metrics(events, unit.metrics);

        unit.metrics.direct_includes = unit.includes.size();

        return Result<CompilationUnit, Error>::success(std::move(unit));
    }

    void register_clang_parser() {
        ParserRegistry::instance().register_parser(
            std::make_unique<ClangTraceParser>()
        );
    }

}  // namespace bha::parsers
