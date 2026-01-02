//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"

#include "bha/bha.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/suggestions/suggester.hpp"
#include "bha/exporters/exporter.hpp"

#include <iostream>
#include <filesystem>

namespace bha::cli
{
    namespace fs = std::filesystem;

    /**
     * Export command - exports analysis results to various formats.
     */
    class ExportCommand : public Command {
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
                   "  bha export --format csv -o data.csv trace.json";
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
                {"max-files", 0, "Maximum files to include", false, true, "0", "N"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.positional().empty()) {
                return "No trace files specified";
            }

            if (auto output = args.get("output"); !output || output->empty()) {
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

            // Collect trace files
            std::vector<fs::path> trace_files;
            for (const auto& path_str : args.positional()) {
                fs::path path(path_str);

                if (!fs::exists(path)) {
                    print_error("File not found: " + path_str);
                    return 1;
                }

                if (fs::is_directory(path)) {
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

            BuildTrace build_trace;
            build_trace.timestamp = std::chrono::system_clock::now();

            {
                ScopedProgress progress(trace_files.size(), "Parsing traces");

                for (const auto& file : trace_files) {
                    if (auto result = parsers::parse_trace_file(file); result.is_ok()) {
                        build_trace.units.push_back(std::move(result.value()));
                    }
                    progress.tick();
                }
            }

            if (build_trace.units.empty()) {
                print_error("No valid trace files parsed");
                return 1;
            }

            print_verbose("Running analysis...");

            AnalysisOptions analysis_opts;
            auto analysis_result = analyzers::run_full_analysis(build_trace, analysis_opts);
            if (!analysis_result.is_ok()) {
                print_error("Analysis failed: " + analysis_result.error().message());
                return 1;
            }

            // Generate suggestions if requested
            std::vector<Suggestion> suggestions;
            if (args.get_flag("include-suggestions")) {
                print_verbose("Generating suggestions...");

                SuggesterOptions suggester_opts;
                auto suggestions_result = suggestions::generate_all_suggestions(
                    build_trace, analysis_result.value(), suggester_opts
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
            export_opts.include_suggestions = !suggestions.empty();

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

    /**
     * Report command - shorthand for common export operations.
     */
    class ReportCommand : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "report";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Generate an HTML analysis report (alias for 'export --format html')";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha report [OPTIONS] <trace-files...>\n"
                   "\n"
                   "Examples:\n"
                   "  bha report traces/\n"
                   "  bha report -o custom-report.html build/*.json\n"
                   "  bha report --dark-mode --title 'My Project' traces/";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"output", 'o', "Output file", false, true, "bha-report.html", "FILE"},
                {"dark-mode", 0, "Use dark mode theme", false, false, "", ""},
                {"title", 0, "Report title", false, true, "Build Analysis Report", "TITLE"},
                {"open", 0, "Open report in browser after generation", false, false, "", ""},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.positional().empty()) {
                return "No trace files specified";
            }
            return "";
        }

        [[nodiscard]] int execute(const ParsedArgs& parsed_args) override {
            if (parsed_args.get_flag("help")) {
                print_help();
                return 0;
            }

            // Build export command args
            std::vector<std::string> export_args;
            export_args.push_back("--format");
            export_args.push_back("html");
            export_args.push_back("--include-suggestions");

            export_args.push_back("-o");
            export_args.push_back(parsed_args.get_or("output", "bha-report.html"));

            if (parsed_args.get_flag("dark-mode")) {
                export_args.push_back("--dark-mode");
            }

            if (const auto title = parsed_args.get("title")) {
                export_args.push_back("--title");
                export_args.push_back(*title);
            }

            if (parsed_args.get_flag("verbose")) {
                export_args.push_back("--verbose");
            }
            if (parsed_args.get_flag("quiet")) {
                export_args.push_back("--quiet");
            }

            for (const auto& pos : parsed_args.positional()) {
                export_args.push_back(pos);
            }

            // Parse and execute export command
            auto* export_cmd = CommandRegistry::instance().find("export");
            if (!export_cmd) {
                print_error("Export command not found");
                return 1;
            }

            auto [args, error, success] = parse_arguments(export_args, export_cmd->arguments());
            if (!success) {
                print_error(error);
                return 1;
            }

            const int result = export_cmd->execute(args);

            // Open in browser if requested
            if (result == 0 && parsed_args.get_flag("open")) {
                const auto output_file = parsed_args.get_or("output", "bha-report.html");
                open_in_browser(output_file);
            }

            return result;
        }

    private:
        static void open_in_browser(const std::string& path) {
#ifdef _WIN32
            std::string cmd = "start \"\" \"" + path + "\"";
#elif defined(__APPLE__)
            std::string cmd = "open \"" + path + "\"";
#else
            const std::string cmd = "xdg-open \"" + path + "\" 2>/dev/null || sensible-browser \"" + path + "\"";
#endif
            std::system(cmd.c_str());
        }
    };

    namespace {
        struct ExportCommandRegistrar {
            ExportCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<ExportCommand>()
                );
                CommandRegistry::instance().register_command(
                    std::make_unique<ReportCommand>()
                );
            }
        } export_registrar;
    }
}  // namespace bha::cli