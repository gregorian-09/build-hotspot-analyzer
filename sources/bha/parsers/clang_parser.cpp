//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/clang_parser.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <nlohmann/json.hpp>

#include <unordered_map>
#include <algorithm>

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
        };

        TraceEvent parse_event(const json& event_json) {
            TraceEvent event;

            if (event_json.contains("name")) {
                event.name = event_json["name"].get<std::string>();
            }
            if (event_json.contains("cat")) {
                event.category = event_json["cat"].get<std::string>();
            }
            if (event_json.contains("ph")) {
                event.phase = event_json["ph"].get<std::string>();
            }
            if (event_json.contains("ts")) {
                event.timestamp = event_json["ts"].get<double>();
            }
            if (event_json.contains("dur")) {
                event.duration = event_json["dur"].get<double>();
            }

            if (event_json.contains("args")) {
                const auto& args = event_json["args"];
                if (args.contains("detail")) {
                    event.detail = args["detail"].get<std::string>();
                }
                if (args.contains("file")) {
                    event.file = args["file"].get<std::string>();
                }
                if (args.contains("line")) {
                    event.line = args["line"].get<int>();
                }
            }

            return event;
        }

        bool is_source_file(const std::string& path) {
            // Check if path looks like a C/C++ source file (not a header)
            const auto pos = path.rfind('.');
            if (pos == std::string::npos) return false;

            const std::string ext = path.substr(pos);
            return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" ||
                   ext == ".C" || ext == ".CC" || ext == ".CPP" || ext == ".CXX";
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
                        if (std::string file_path = event.detail.substr(0, colon_pos); is_source_file(file_path)) {
                            return fs::path(file_path);
                        }
                    }
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
                    event.name == "InstantiateFunction" ||
                    event.name == "CodeGen Function" ||
                    string_utils::starts_with(event.name, "Instantiate")) {

                    auto& tmpl = template_map[event.detail];
                    if (tmpl.name.empty()) {
                        tmpl.name = event.name;
                        tmpl.full_signature = event.detail;

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
            std::vector<IncludeInfo>& includes
        ) {
            std::unordered_map<std::string, IncludeInfo> include_map;

            for (const auto& event : events) {
                if (event.name == "Source" && !event.detail.empty()) {
                    auto& info = include_map[event.detail];
                    info.header = event.detail;
                    info.parse_time += microseconds_to_duration(event.duration);
                }
            }

            includes.reserve(include_map.size());
            for (auto& info : include_map | std::views::values) {
                includes.push_back(std::move(info));
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
                         string_utils::starts_with(event.name, "Total Instantiate")) {
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

        auto result = file_utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool ClangTraceParser::can_parse_content(std::string_view content) const {
        return content.find(CLANG_TRACE_MARKER) != std::string_view::npos;
    }

    Result<CompilationUnit, Error> ClangTraceParser::parse_file(
        const fs::path& path
    ) const {
        auto content_result = file_utils::read_file(path);
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
            if (!event_json.is_object()) {
                continue;
            }
            events.push_back(parse_event(event_json));
        }

        CompilationUnit unit;

        const auto detected_source = extract_source_file(events);
        unit.source_file = detected_source.empty() ? source_hint : detected_source;

        unit.metrics.path = unit.source_file;

        process_template_events(events, unit.templates);
        process_include_events(events, unit.includes);
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