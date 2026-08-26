//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"
#include "bha/utils/suggestion_path_utils.hpp"

#include "bha/bha.hpp"
#include "bha/build_sessions/cmake_instrumentation.hpp"
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
                   "  bha export --format csv -o data.csv trace.json\n"
                   "  bha export --format json -o report.json traces/";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"output", 'o', "Output file (required)", true, true, "", "FILE"},
                {"format", 'f', "Output format (json, html, csv, md)", false, true, "", "FORMAT"},
                {"include-suggestions", 's', "Include optimization suggestions", false, false, "", ""},
                {"pretty", 0, "Pretty-print output", false, false, "", ""},
                {"compress", 'z', "Compress output (gzip)", false, false, "", ""},
                {"dark-mode", 0, "Use dark mode for HTML", false, false, "", ""},
                {"title", 0, "Report title for HTML", false, true, "Build Analysis Report", "TITLE"},
                {"max-files", 0, "Maximum files to include (0=unlimited)", false, true, "0", "N"},
                {"max-suggestions", 0, "Maximum suggestions to include (0=unlimited)", false, true, "0", "N"},
                {"no-file-details", 0, "Exclude per-file analysis details", false, false, "", ""},
                {"no-dependencies", 0, "Exclude dependency graph", false, false, "", ""},
                {"no-templates", 0, "Exclude template instantiation data", false, false, "", ""},
                {"no-symbols", 0, "Exclude symbol information", false, false, "", ""},
                {"no-timing", 0, "Exclude timing breakdown", false, false, "", ""},
                {"no-interactive", 0, "Disable interactive visualizations (HTML)", false, false, "", ""},
                {"use-cdn", 0, "Use CDN for assets instead of bundling (HTML)", false, false, "", ""},
                {"cache-stats", 0, "Structured sccache JSON statistics file", false, true, "", "FILE"},
                {"module-deps", 0, "Clang P1689 module dependency JSON file", false, true, "", "FILE"},
                {"resource-stats", 0, "Clang -fproc-stat-report CSV file", false, true, "", "FILE"},
                {"cmake-index", 0, "CMake Instrumentation API v1 index file", false, true, "", "FILE"},
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

            if (trace_files.empty() && !args.get("cmake-index").has_value()) {
                print_error("No trace files found");
                return 1;
            }

            print_verbose("Found " + std::to_string(trace_files.size()) + " trace files");
            if (!memory_files.empty()) {
                print_verbose("Found " + std::to_string(memory_files.size()) + " memory files");
            }

            BuildTrace build_trace;
            build_trace.timestamp = std::chrono::system_clock::now();

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

            if (const auto cmake_index_path = args.get("cmake-index")) {
                build_sessions::CMakeInstrumentationParser parser;
                if (const auto result = parser.attach_to_trace(build_trace, *cmake_index_path);
                    result.is_err()) {
                    print_error("Failed to attach CMake instrumentation index: " + result.error().message());
                    return 1;
                }
                print_verbose("Attached CMake instrumentation index: " + *cmake_index_path);
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
            export_opts.compress = args.get_flag("compress");
            export_opts.html_dark_mode = args.get_flag("dark-mode");
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

            // HTML options
            export_opts.html_interactive = !args.get_flag("no-interactive");
            export_opts.html_offline = !args.get_flag("use-cdn");

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
                auto size = fs::file_size(output_path);
                std::cout << "Exported " << exporters::format_to_string(format)
                          << " report to " << output_path.string()
                          << " (" << format_size(size) << ")\n";
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
