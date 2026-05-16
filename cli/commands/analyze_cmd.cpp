//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"

#include "bha/bha.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/parsers/memory_parser.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/exporters/exporter.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <unordered_map>

namespace bha::cli
{
    namespace fs = std::filesystem;

    /**
     * Analyze command - analyzes build trace files.
     */
    class AnalyzeCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "analyze";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyze build trace files to identify compilation hotspots";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha analyze [OPTIONS] [trace-files...]\n"
                   "\n"
                   "If no trace files are specified, defaults to build/traces/\n"
                   "\n"
                   "Examples:\n"
                   "  bha analyze                           # Use build/traces/\n"
                   "  bha analyze build/*.json              # Analyze specific files\n"
                   "  bha analyze --top 20 trace.json\n"
                   "  bha analyze --json --output report.json traces/";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"output", 'o', "Output file for results", false, true, "", "FILE"},
                {"format", 'f', "Output format (text, json)", false, true, "text", "FORMAT"},
                {"top", 't', "Number of top items to show (0=all)", false, true, "10", "N"},
                {"list-files", 0, "List all analyzed files", false, false, "", ""},
                {"list-headers", 0, "List all headers with details", false, false, "", ""},
                {"list-templates", 0, "List all templates (no truncation)", false, false, "", ""},
                {"include-templates", 0, "Include template analysis", false, false, "", ""},
                {"include-includes", 0, "Include header analysis", false, false, "", ""},
                {"min-time", 0, "Minimum time threshold (ms)", false, true, "10", "MS"},
                {"parallel", 'j', "Number of parallel threads", false, true, "0", "N"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs&) const override {
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

            if (args.get_flag("json") || args.get_or("format", "text") == "json") {
                set_output_format(OutputFormat::JSON);
            }

            std::size_t top_count = static_cast<std::size_t>(args.get_int("top").value_or(10));
            std::size_t threads = static_cast<std::size_t>(args.get_int("parallel").value_or(0));
            Duration min_time = std::chrono::milliseconds(args.get_int("min-time").value_or(10));

            std::vector<fs::path> trace_files;
            std::vector<fs::path> memory_files;

            std::vector<std::string> paths_to_analyze;

            if (args.positional().empty()) {
                if (fs::path default_trace_dir = fs::current_path() / "build" / "traces"; fs::exists(default_trace_dir)) {
                    paths_to_analyze.push_back(default_trace_dir.string());
                    print_verbose("Using default trace directory: " + default_trace_dir.string());
                } else {
                    print_error("No trace files specified and default directory not found: " + default_trace_dir.string());
                    print_error("Use 'bha analyze <files...>' or ensure traces exist in build/traces/");
                    return 1;
                }
            } else {
                paths_to_analyze = args.positional();
            }

            for (const auto& path_str : paths_to_analyze) {
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
                            auto ext = entry.path().extension().string();
                            if (ext == ".su" || ext == ".map") {
                                memory_files.push_back(entry.path());
                            }
                        }
                    }
                } else if (auto ext = path.extension().string(); ext == ".su" || ext == ".map") {
                    memory_files.push_back(path);
                }
            }

            if (trace_files.empty()) {
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

                auto parse_results = parsers::parse_trace_files(trace_files, threads);
                for (std::size_t i = 0; i < parse_results.size(); ++i) {
                    auto& result = parse_results[i];

                    if (!trace_files.empty()) {
                        progress.set_message(format_path(trace_files[i], 40));
                    }

                    if (result.is_ok()) {
                        build_trace.total_time += result.value().metrics.total_time;
                        build_trace.units.push_back(std::move(result.value()));
                    } else {
                        print_warning("Failed to parse: " + trace_files[i].string() +
                                      " (" + result.error().message() + ")");
                    }

                    progress.tick();
                }
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

            if (build_trace.units.empty()) {
                print_error("No valid trace files parsed");
                return 1;
            }

            AnalysisOptions analysis_opts;
            analysis_opts.max_threads = threads;
            analysis_opts.min_duration_threshold = min_time;
            analysis_opts.verbose = is_verbose();

            print_verbose("Running analysis...");

            auto analysis_result = analyzers::run_full_analysis(build_trace, analysis_opts);
            if (!analysis_result.is_ok()) {
                print_error("Analysis failed: " + analysis_result.error().message());
                return 1;
            }

            const auto& result = analysis_result.value();

            bool list_files = args.get_flag("list-files");
            bool list_headers = args.get_flag("list-headers");
            bool list_templates = args.get_flag("list-templates");

            if (is_json()) {
                std::cout << json::to_json(result, true) << "\n";
            } else {
                SummaryPrinter printer(std::cout);
                printer.print_build_summary(result);

                std::size_t file_limit = list_files ? 0 : top_count;
                std::size_t header_limit = list_headers ? 0 : top_count;
                std::size_t template_limit = list_templates ? 0 : top_count;

                printer.print_file_summary(result.files, file_limit);
                printer.print_include_summary(result.dependencies, header_limit);
                printer.print_template_summary(result.templates, template_limit, list_templates);
            }

            if (auto output_file = args.get("output")) {
                std::ofstream out(*output_file);
                if (!out) {
                    print_error("Failed to open output file: " + *output_file);
                    return 1;
                }

                exporters::ExportOptions export_opts;
                export_opts.include_file_details = true;
                export_opts.include_dependencies = true;
                export_opts.include_templates = true;
                export_opts.include_symbols = true;
                export_opts.include_suggestions = true;
                export_opts.include_metadata = true;
                export_opts.pretty_print = true;
                export_opts.max_files = 0;  // Include all files
                export_opts.min_compile_time = Duration::zero();  // Include all files regardless of time

                exporters::JsonExporter exporter;

                if (auto export_result = exporter.export_to_stream(out, result, {}, export_opts, nullptr); export_result.is_err()) {
                    print_error("Failed to write output: " + export_result.error().message());
                    return 1;
                }

                print("Results written to " + *output_file);
            }

            return 0;
        }
    };

    namespace {
        struct AnalyzeCommandRegistrar {
            AnalyzeCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<AnalyzeCommand>()
                );
            }
        } analyze_registrar;
    }
}  // namespace bha::cli