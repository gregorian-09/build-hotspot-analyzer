//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"
#include "bha/utils/suggestion_path_utils.hpp"

#include "bha/bha.hpp"
#include "bha/build_systems/adapter_support.hpp"
#include "bha/build_sessions/cmake_instrumentation.hpp"
#include "bha/build_sessions/cmake_file_api.hpp"
#include "bha/build_sessions/session_file.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/parsers/memory_parser.hpp"
#include "bha/parsers/sccache_stats_parser.hpp"
#include "bha/parsers/p1689_module_parser.hpp"
#include "bha/parsers/process_resource_parser.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/suggestions/suggester.hpp"
#include "bha/exporters/exporter.hpp"

#include <iostream>
#include <filesystem>
#include <unordered_map>

namespace bha::cli
{
    namespace fs = std::filesystem;

    /**
     * Export command - exports analysis results to various formats.
     */
    class ExportCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "export";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Export analysis results to JSON, HTML, CSV, or Markdown";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha export [OPTIONS] <trace-files...> -o <output-file>\n"
                   "\n"
                   "Examples:\n"
                   "  bha export --format json -o report.json traces/\n"
                   "  bha export --format html -o report.html build/*.json\n"
                   "  bha export --format csv -o data/ trace.json\n"
                   "  bha export --format json -o report.json traces/";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"output", 'o', "Output file (required)", true, true, "", "FILE"},
                {"format", 'f', "Output format (json, html, csv, md)", false, true, "", "FORMAT"},
                {"include-suggestions", 's', "Include optimization suggestions", false, false, "", ""},
                {"pretty", 0, "Pretty-print output", false, false, "", ""},
                {"title", 0, "Report title for HTML", false, true, "Build Analysis Report", "TITLE"},
                {"max-files", 0, "Maximum files to include (0=unlimited)", false, true, "0", "N"},
                {"max-suggestions", 0, "Maximum suggestions to include (0=unlimited)", false, true, "0", "N"},
                {"no-file-details", 0, "Exclude per-file analysis details", false, false, "", ""},
                {"no-dependencies", 0, "Exclude dependency graph", false, false, "", ""},
                {"no-templates", 0, "Exclude template instantiation data", false, false, "", ""},
                {"no-symbols", 0, "Exclude symbol information", false, false, "", ""},
                {"no-timing", 0, "Exclude timing breakdown", false, false, "", ""},
                {"cache-stats", 0, "Structured sccache JSON statistics file", false, true, "", "FILE"},
                {"module-deps", 0, "Clang P1689 module dependency JSON file", false, true, "", "FILE"},
                {"resource-stats", 0, "Clang -fproc-stat-report CSV file", false, true, "", "FILE"},
                {"cmake-index", 0, "CMake Instrumentation API v1 index file", false, true, "", "FILE"},
                {"cmake-file-api", 0, "CMake File API reply index file", false, true, "", "FILE"},
                {"config", 0, "CMake configuration to select from the File API model", false, true, "", "CONFIG"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.positional().empty() && !args.get("cmake-index").has_value()) {
                return "No trace files specified";
            }

            if (const auto output = args.get("output"); !output || output->empty()) {
                return "Output file is required (-o FILE)";
            }

            return "";
        }

        [[nodiscard]] int execute(const ParsedArgs& args) override {
            if (args.get_flag("help")) {
                print_help();
                return 0;
            }

            if (args.get_flag("verbose")) {
                set_verbosity(Verbosity::Verbose);
            } else if (args.get_flag("quiet")) {
                set_verbosity(Verbosity::Quiet);
            }

            // Get output file and determine format
            fs::path output_path(*args.get("output"));
            auto format_str = args.get("format");

            exporters::ExportFormat format;
            if (format_str) {
                auto parsed = exporters::string_to_format(*format_str);
                if (!parsed) {
                    print_error("Unknown format: " + *format_str);
                    print_error("Supported formats: json, html, csv, md");
                    return 1;
                }
                format = *parsed;
            } else {
                if (auto ext = output_path.extension().string(); ext == ".json") format = exporters::ExportFormat::JSON;
                else if (ext == ".html" || ext == ".htm") format = exporters::ExportFormat::HTML;
                else if (ext == ".csv") format = exporters::ExportFormat::CSV;
                else if (ext == ".md") format = exporters::ExportFormat::Markdown;
                else {
                    print_error("Cannot determine format from extension: " + ext);
                    print_error("Use --format to specify the output format");
                    return 1;
                }
            }

            // Collect trace files AND memory files
            std::vector<fs::path> trace_files;
            std::vector<fs::path> memory_files;
            std::optional<fs::path> session_file_path;

            for (const auto& path_str : args.positional()) {
                fs::path path(path_str);

                if (!fs::exists(path)) {
                    print_error("File not found: " + path_str);
                    return 1;
                }

                auto files = parsers::collect_trace_files(path);
                trace_files.insert(trace_files.end(), files.begin(), files.end());

                if (fs::is_directory(path)) {
                    for (const auto& entry : fs::recursive_directory_iterator(path)) {
                        if (entry.is_regular_file()) {
                            if (auto ext = entry.path().extension().string(); ext == ".su" || ext == ".map") {
                                memory_files.push_back(entry.path());
                            }
                        }
                    }
                } else if (auto ext = path.extension().string(); ext == ".su" || ext == ".map") {
                    memory_files.push_back(path);
                }
            }

            for (const auto& path_str : args.positional()) {
                const fs::path path(path_str);
                const fs::path candidate = fs::is_directory(path)
                    ? path / std::string(build_sessions::kBuildSessionFileName)
                    : path.filename() == build_sessions::kBuildSessionFileName
                        ? path
                        : path.parent_path() / std::string(build_sessions::kBuildSessionFileName);
                if (fs::is_regular_file(candidate)) {
                    session_file_path = candidate;
                    break;
                }
            }

            if (trace_files.empty() && !args.get("cmake-index").has_value() &&
                !session_file_path.has_value()) {
                print_error("No trace files found");
                return 1;
            }

            print_verbose("Found " + std::to_string(trace_files.size()) + " trace files");
            if (!memory_files.empty()) {
                print_verbose("Found " + std::to_string(memory_files.size()) + " memory files");
            }

            BuildTrace build_trace;
            build_trace.timestamp = std::chrono::system_clock::now();

            std::optional<fs::path> cmake_instrumentation_index;
            if (const auto requested_cmake_index = args.get("cmake-index")) {
                cmake_instrumentation_index = fs::path(*requested_cmake_index);
            } else if (session_file_path.has_value()) {
                // A persisted CMake session records the build tree that owns
                // the producer index. Prefer that exact session boundary over
                // scanning unrelated JSON files in the trace directory.
                build_sessions::BuildSessionFileParser session_parser;
                if (const auto session = session_parser.parse_file(*session_file_path);
                    session.is_ok() &&
                    session.value().build_system == BuildSystemType::CMake &&
                    !session.value().build_directory.empty()) {
                    cmake_instrumentation_index = build_systems::detail::find_cmake_instrumentation_index(
                        session.value().build_directory
                    );
                }
            }
            if (cmake_instrumentation_index.has_value()) {
                // The CMake index owns the command-to-trace relationship. Do
                // not parse a second, potentially stale scan of the same build
                // tree before the authoritative references are attached.
                trace_files.clear();
            }

            {
                ScopedProgress progress(trace_files.size(), "Parsing traces");

                for (const auto& file : trace_files) {
                    if (auto result = parsers::parse_trace_file(file); result.is_ok()) {
                        build_trace.total_time += result.value().metrics.total_time;
                        build_trace.units.push_back(std::move(result.value()));
                    }
                    progress.tick();
                }
            }

            if (cmake_instrumentation_index.has_value()) {
                build_sessions::CMakeInstrumentationParser parser;
                if (const auto result = parser.attach_to_trace(build_trace, *cmake_instrumentation_index);
                    result.is_err()) {
                    print_error("Failed to attach CMake instrumentation index: " + result.error().message());
                    return 1;
                }
                print_verbose("Attached CMake instrumentation index: " + cmake_instrumentation_index->string());
            }

            if (!build_trace.build_session.has_value() && session_file_path.has_value()) {
                build_sessions::BuildSessionFileParser parser;
                if (const auto result = parser.attach_to_trace(build_trace, *session_file_path);
                    result.is_err()) {
                    print_error("Failed to attach BHA build session: " + result.error().message());
                    return 1;
                }
                print_verbose("Attached BHA build session: " + session_file_path->string());
            }

            std::optional<fs::path> cmake_file_api_path;
            if (const auto requested_file_api = args.get("cmake-file-api")) {
                cmake_file_api_path = fs::path(*requested_file_api);
            } else if (build_trace.build_session.has_value() &&
                       build_trace.build_session->build_system == BuildSystemType::CMake &&
                       !build_trace.build_session->build_directory.empty()) {
                cmake_file_api_path = build_systems::detail::find_cmake_file_api_index(
                    build_trace.build_session->build_directory
                );
            }
            if (cmake_file_api_path.has_value()) {
                build_sessions::CMakeFileApiParser parser;
                const auto result = parser.parse_reply_index(
                    *cmake_file_api_path,
                    args.get_or("config", "")
                );
                if (result.is_err()) {
                    print_error("Failed to attach CMake File API target graph: " + result.error().message());
                    return 1;
                }
                build_trace.target_graph = result.value();
                print_verbose("Attached CMake File API target graph: " + cmake_file_api_path->string());
            }

            if (!memory_files.empty()) {
                std::unordered_map<std::string, MemoryMetrics> memory_map;

                ScopedProgress progress(memory_files.size(), "Parsing stack usage files");
                for (const auto& file : memory_files) {
                    if (file.extension() != ".su") {
                        progress.tick();
                        continue;
                    }

                    progress.set_message(format_path(file, 40));

                    if (auto result = parsers::parse_stack_usage_file(file); result.is_ok()) {
                        std::string filename = file.filename().string();
                        if (filename.size() > 3) {
                            std::string key = filename.substr(0, filename.size() - 3);
                            memory_map[key] = result.value();
                        }
                    }

                    progress.tick();
                }

                std::size_t matched = 0;
                for (auto& unit : build_trace.units) {
                    std::string source_name = unit.source_file.filename().string();

                    if (auto it = memory_map.find(source_name); it != memory_map.end()) {
                        unit.metrics.memory = it->second;
                        matched++;
                    }
                }

                print_verbose("Matched " + std::to_string(matched) + "/" +
                              std::to_string(build_trace.units.size()) + " files with memory data");
            }

            if (build_trace.units.empty() && !build_trace.build_session.has_value()) {
                print_error("No valid trace files parsed");
                return 1;
            }

            if (const auto cache_stats_path = args.get("cache-stats")) {
                parsers::SccacheStatsParser parser;
                if (const auto result = parser.attach_to_trace(build_trace, *cache_stats_path); result.is_err()) {
                    print_error("Failed to parse cache statistics: " + result.error().message());
                    return 1;
                }
                print_verbose("Attached cache statistics: " + *cache_stats_path);
            }

            if (const auto module_deps_path = args.get("module-deps")) {
                parsers::P1689ModuleParser parser;
                if (const auto result = parser.attach_to_trace(build_trace, *module_deps_path); result.is_err()) {
                    print_error("Failed to parse module dependencies: " + result.error().message());
                    return 1;
                }
                print_verbose("Attached module dependencies: " + *module_deps_path);
            }

            if (const auto resource_stats_path = args.get("resource-stats")) {
                parsers::ProcessResourceParser parser;
                if (const auto result = parser.attach_to_trace(build_trace, *resource_stats_path); result.is_err()) {
                    print_error("Failed to parse process resource statistics: " + result.error().message());
                    return 1;
                }
                print_verbose("Attached process resource statistics: " + *resource_stats_path);
            }

            print_verbose("Running analysis...");

            AnalysisOptions analysis_opts;
            analysis_opts.verbose = is_verbose() && !is_json();
            auto analysis_result = analyzers::run_full_analysis(build_trace, analysis_opts);
            if (!analysis_result.is_ok()) {
                print_error("Analysis failed: " + analysis_result.error().message());
                return 1;
            }

            const bool include_suggestions = args.get_flag("include-suggestions");
            const bool needs_suggestions = include_suggestions;

            // Generate suggestions only when explicitly requested. This keeps
            // ordinary exports focused on observed analysis data.
            std::vector<Suggestion> suggestions;
            fs::path project_root;
            if (needs_suggestions) {
                print_verbose("Generating suggestions...");

                SuggesterOptions suggester_opts;
                std::vector<fs::path> input_paths;
                input_paths.reserve(args.positional().size());
                for (const auto& path : args.positional()) {
                    input_paths.emplace_back(path);
                }
                project_root = utils::resolve_project_root_for_suggestions(
                    input_paths,
                    build_trace,
                    analysis_result.value()
                );
                if (build_trace.build_session.has_value() &&
                    !build_trace.build_session->build_directory.empty()) {
                    const fs::path compile_commands =
                        build_trace.build_session->build_directory / "compile_commands.json";
                    if (fs::is_regular_file(compile_commands)) {
                        suggester_opts.compile_commands_path = compile_commands;
                    }
                }
                if (!suggester_opts.compile_commands_path.has_value()) {
                    if (const auto compile_commands_path = utils::find_compile_commands_path(project_root)) {
                        suggester_opts.compile_commands_path = *compile_commands_path;
                    }
                }
                print_verbose("Resolved project root: " + project_root.generic_string());
                auto suggestions_result = suggestions::generate_all_suggestions(
                    build_trace, analysis_result.value(), suggester_opts, project_root
                );

                if (suggestions_result.is_ok()) {
                    suggestions = std::move(suggestions_result.value());
                }
            }

            auto exporter_result = exporters::ExporterFactory::create(format);
            if (!exporter_result.is_ok()) {
                print_error("Failed to create exporter: " + exporter_result.error().message());
                return 1;
            }

            auto& exporter = exporter_result.value();
            exporters::ExportOptions export_opts;
            export_opts.pretty_print = args.get_flag("pretty") || format == exporters::ExportFormat::JSON;
            export_opts.html_title = args.get_or("title", "Build Analysis Report");
            export_opts.max_files = static_cast<std::size_t>(args.get_int("max-files").value_or(0));
            export_opts.max_suggestions = static_cast<std::size_t>(args.get_int("max-suggestions").value_or(0));
            export_opts.include_suggestions = include_suggestions;

            // Content control options (inverted logic - flags disable features)
            export_opts.include_file_details = !args.get_flag("no-file-details");
            export_opts.include_dependencies = !args.get_flag("no-dependencies");
            export_opts.include_templates = !args.get_flag("no-templates");
            export_opts.include_symbols = !args.get_flag("no-symbols");
            export_opts.include_timing = !args.get_flag("no-timing");

            print_verbose("Exporting to " + output_path.string() + "...");

            exporters::ExportProgressCallback progress_cb = nullptr;
            if (is_verbose()) {
                progress_cb = [](const std::size_t current, const std::size_t total, const std::string_view stage) {
                    std::cout << stage << ": " << current << "/" << total << "\n";
                };
            }

            auto export_result = exporter->export_to_file(
                output_path,
                analysis_result.value(),
                suggestions,
                export_opts,
                progress_cb
            );

            if (!export_result.is_ok()) {
                print_error("Export failed: " + export_result.error().message());
                return 1;
            }

            if (!is_quiet()) {
                if (format == exporters::ExportFormat::CSV && fs::is_directory(output_path)) {
                    std::size_t table_count = 0;
                    for (const auto& entry : fs::directory_iterator(output_path)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".csv") {
                            ++table_count;
                        }
                    }
                    std::cout << "Exported CSV bundle to " << output_path.string()
                              << " (" << table_count << " tables)\n";
                } else {
                    const auto size = fs::file_size(output_path);
                    std::cout << "Exported " << exporters::format_to_string(format)
                              << " report to " << output_path.string()
                              << " (" << format_size(size) << ")\n";
                }
            }

            return 0;
        }
    };

    namespace {
        struct ExportCommandRegistrar {
            ExportCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<ExportCommand>()
                );
            }
        } export_registrar;
    }
}  // namespace bha::cli
