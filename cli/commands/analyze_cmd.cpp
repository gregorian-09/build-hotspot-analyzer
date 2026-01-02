//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"

#include "bha/bha.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>

namespace bha::cli
{
    namespace fs = std::filesystem;

    /**
     * Analyze command - analyzes build trace files.
     */
    class AnalyzeCommand : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "analyze";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Analyze build trace files to identify compilation hotspots";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha analyze [OPTIONS] <trace-files...>\n"
                   "\n"
                   "Examples:\n"
                   "  bha analyze build/*.json\n"
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

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.positional().empty()) {
                return "No trace files specified. Use 'bha analyze <files...>'";
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

            if (args.get_flag("json") || args.get_or("format", "text") == "json") {
                set_output_format(OutputFormat::JSON);
            }

            // Get options
            std::size_t top_count = static_cast<std::size_t>(args.get_int("top").value_or(10));
            std::size_t threads = static_cast<std::size_t>(args.get_int("parallel").value_or(0));
            Duration min_time = std::chrono::milliseconds(args.get_int("min-time").value_or(10));

            // Collect trace files
            std::vector<fs::path> trace_files;
            for (const auto& path_str : args.positional()) {
                fs::path path(path_str);

                if (!fs::exists(path)) {
                    print_error("File not found: " + path_str);
                    return 1;
                }

                if (fs::is_directory(path)) {
                    // Scan directory for trace files
                    for (const auto& entry : fs::recursive_directory_iterator(path)) {
                        if (entry.is_regular_file()) {
                            if (auto ext = entry.path().extension().string(); ext == ".json") {
                                trace_files.push_back(entry.path());
                            }
                        }
                    }
                } else {
                    trace_files.push_back(path);
                }
            }

            if (trace_files.empty()) {
                print_error("No trace files found");
                return 1;
            }

            print_verbose("Found " + std::to_string(trace_files.size()) + " trace files");

            BuildTrace build_trace;
            build_trace.timestamp = std::chrono::system_clock::now();

            {
                ScopedProgress progress(trace_files.size(), "Parsing traces");

                for (const auto& file : trace_files) {
                    progress.set_message(format_path(file, 40));

                    if (auto result = parsers::parse_trace_file(file); result.is_ok()) {
                        build_trace.total_time += result.value().metrics.total_time;
                        build_trace.units.push_back(std::move(result.value()));
                    } else {
                        print_warning("Failed to parse: " + file.string() +
                                      " (" + result.error().message() + ")");
                    }

                    progress.tick();
                }
            }

            if (build_trace.units.empty()) {
                print_error("No valid trace files parsed");
                return 1;
            }

            AnalysisOptions analysis_opts;
            analysis_opts.max_threads = threads;
            analysis_opts.min_duration_threshold = min_time;
            analysis_opts.analyze_templates = args.get_flag("include-templates") || true;  // Default on
            analysis_opts.analyze_includes = args.get_flag("include-includes") || true;    // Default on
            analysis_opts.verbose = is_verbose();

            print_verbose("Running analysis...");

            auto analysis_result = analyzers::run_full_analysis(build_trace, analysis_opts);
            if (!analysis_result.is_ok()) {
                print_error("Analysis failed: " + analysis_result.error().message());
                return 1;
            }

            const auto& result = analysis_result.value();

            // Output results
            bool list_files = args.get_flag("list-files");
            bool list_headers = args.get_flag("list-headers");
            bool list_templates = args.get_flag("list-templates");

            if (is_json()) {
                std::cout << json::to_json(result, true) << "\n";
            } else {
                SummaryPrinter printer(std::cout);
                printer.print_build_summary(result);

                // If --top 0 or any --list flag, show all items
                std::size_t file_limit = list_files ? 0 : top_count;
                std::size_t header_limit = list_headers ? 0 : top_count;
                std::size_t template_limit = list_templates ? 0 : top_count;

                printer.print_file_summary(result.files, file_limit);
                printer.print_include_summary(result.dependencies, header_limit);
                printer.print_template_summary(result.templates, template_limit, list_templates);
            }

            // Write to output file if specified
            if (auto output_file = args.get("output")) {
                std::ofstream out(*output_file);
                if (!out) {
                    print_error("Failed to open output file: " + *output_file);
                    return 1;
                }
                out << json::to_json(result, true);
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