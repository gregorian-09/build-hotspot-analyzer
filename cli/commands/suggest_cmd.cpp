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

#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace bha::cli
{
    namespace fs = std::filesystem;

    /**
     * Suggest command - generates optimization suggestions.
     */
    class SuggestCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "suggest";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Generate optimization suggestions from build analysis";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha suggest [OPTIONS] <trace-files...>\n"
                   "\n"
                   "Examples:\n"
                   "  bha suggest build/*.json\n"
                   "  bha suggest --min-priority high trace.json\n"
                   "  bha suggest --type pch --type forward-decl traces/";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"output", 'o', "Output file for suggestions", false, true, "", "FILE"},
                {"format", 'f', "Output format (text, json)", false, true, "text", "FORMAT"},
                {"limit", 'n', "Maximum number of suggestions", false, true, "20", "N"},
                {"min-priority", 'p', "Minimum priority (low, medium, high, critical)", false, true, "low", "LEVEL"},
                {"min-confidence", 'c', "Minimum confidence (0.0-1.0)", false, true, "0.5", "VALUE"},
                {"type", 0, "Filter by suggestion type (can be repeated)", false, true, "", "TYPE"},
                {"include-unsafe", 0, "Include potentially unsafe suggestions", false, false, "", ""},
                {"detailed", 'd', "Show detailed suggestion info", false, false, "", ""},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.positional().empty()) {
                return "No trace files specified. Use 'bha suggest <files...>'";
            }

            if (const auto min_conf = args.get_double("min-confidence"); min_conf && (*min_conf < 0.0 || *min_conf > 1.0)) {
                return "Confidence must be between 0.0 and 1.0";
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

            std::size_t limit = static_cast<std::size_t>(args.get_int("limit").value_or(20));
            double min_confidence = args.get_double("min-confidence").value_or(0.5);
            bool include_unsafe = args.get_flag("include-unsafe");
            bool detailed = args.get_flag("detailed");

            auto min_priority = Priority::Low;
            if (auto priority_str = args.get_or("min-priority", "low"); priority_str == "critical") min_priority = Priority::Critical;
            else if (priority_str == "high") min_priority = Priority::High;
            else if (priority_str == "medium") min_priority = Priority::Medium;

            std::vector<fs::path> trace_files;
            for (const auto& path_str : args.positional()) {
                fs::path path(path_str);

                if (!fs::exists(path)) {
                    print_error("File not found: " + path_str);
                    return 1;
                }

                auto files = parsers::collect_trace_files(path);
                trace_files.insert(trace_files.end(), files.begin(), files.end());
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
                        build_trace.total_time += result.value().metrics.total_time;
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

            print_verbose("Generating suggestions...");

            SuggesterOptions suggester_opts;
            suggester_opts.max_suggestions = limit;
            suggester_opts.min_priority = min_priority;
            suggester_opts.min_confidence = min_confidence;
            suggester_opts.include_unsafe = include_unsafe;

            auto suggestions_result = suggestions::generate_all_suggestions(
                build_trace, analysis_result.value(), suggester_opts
            );

            if (!suggestions_result.is_ok()) {
                print_error("Suggestion generation failed: " + suggestions_result.error().message());
                return 1;
            }

            auto& suggestions_list = suggestions_result.value();

            std::ranges::sort(suggestions_list,
                              [](const Suggestion& a, const Suggestion& b) {
                                  if (a.priority != b.priority) {
                                      return static_cast<int>(a.priority) < static_cast<int>(b.priority);
                                  }
                                  return a.estimated_savings > b.estimated_savings;
                              });

            if (suggestions_list.size() > limit) {
                suggestions_list.resize(limit);
            }

            if (is_json()) {
                std::cout << json::to_json(suggestions_list, true) << "\n";
            } else {
                if (detailed) {
                    print_detailed_suggestions(suggestions_list);
                } else {
                    SummaryPrinter printer(std::cout);
                    printer.print_suggestions(suggestions_list, limit);
                }

                if (!is_quiet()) {
                    Duration total_savings = Duration::zero();
                    for (const auto& s : suggestions_list) {
                        total_savings += s.estimated_savings;
                    }

                    std::cout << "\n";
                    std::cout << "Total: " << suggestions_list.size() << " suggestions\n";
                    std::cout << "Potential savings: " << format_duration(total_savings) << "\n";
                }
            }

            if (auto output_file = args.get("output")) {
                std::ofstream out(*output_file);
                if (!out) {
                    print_error("Failed to open output file: " + *output_file);
                    return 1;
                }
                out << json::to_json(suggestions_list, true);
                print("Suggestions written to " + *output_file);
            }

            return 0;
        }

    private:
        static void print_detailed_suggestions(const std::vector<Suggestion>& suggestions) {
            for (std::size_t i = 0; i < suggestions.size(); ++i) {
                const auto& s = suggestions[i];

                std::cout << "\n";
                std::cout << std::string(70, '=') << "\n";
                std::cout << "[" << (i + 1) << "/" << suggestions.size() << "] ";
                std::cout << colorize_priority(s.priority) << " ";
                std::cout << colorize_type(s.type) << "\n";
                std::cout << std::string(70, '=') << "\n\n";

                if (colors::enabled()) {
                    std::cout << colors::BOLD << s.title << colors::RESET << "\n\n";
                } else {
                    std::cout << s.title << "\n\n";
                }

                std::cout << "Description:\n";
                std::cout << "  " << s.description << "\n\n";

                if (!s.rationale.empty()) {
                    std::cout << "Rationale:\n";
                    std::cout << "  " << s.rationale << "\n\n";
                }

                std::cout << "Target File:\n";
                std::cout << "  " << s.target_file.path.string();
                if (s.target_file.has_line_range()) {
                    std::cout << " (lines " << s.target_file.line_start;
                    if (s.target_file.line_end != s.target_file.line_start) {
                        std::cout << "-" << s.target_file.line_end;
                    }
                    std::cout << ")";
                }
                std::cout << "\n";
                std::cout << "  Action: " << to_string(s.target_file.action) << "\n\n";

                if (!s.secondary_files.empty()) {
                    std::cout << "Secondary Files:\n";
                    for (const auto& f : s.secondary_files) {
                        std::cout << "  - " << f.path.string();
                        std::cout << " (" << to_string(f.action) << ")\n";
                    }
                    std::cout << "\n";
                }

                if (!s.before_code.code.empty()) {
                    std::cout << "Before:\n";
                    if (colors::enabled()) {
                        std::cout << colors::RED;
                    }
                    std::cout << "  " << s.before_code.code << "\n";
                    if (colors::enabled()) {
                        std::cout << colors::RESET;
                    }
                    std::cout << "\n";
                }

                if (!s.after_code.code.empty()) {
                    std::cout << "After:\n";
                    if (colors::enabled()) {
                        std::cout << colors::GREEN;
                    }
                    std::cout << "  " << s.after_code.code << "\n";
                    if (colors::enabled()) {
                        std::cout << colors::RESET;
                    }
                    std::cout << "\n";
                }

                if (!s.implementation_steps.empty()) {
                    std::cout << "Implementation Steps:\n";
                    for (std::size_t j = 0; j < s.implementation_steps.size(); ++j) {
                        std::cout << "  " << (j + 1) << ". " << s.implementation_steps[j] << "\n";
                    }
                    std::cout << "\n";
                }

                std::cout << "Impact:\n";
                std::cout << "  Estimated savings: " << format_duration(s.estimated_savings);
                std::cout << " (" << format_percent(s.estimated_savings_percent) << " of build time)\n";
                std::cout << "  Confidence: " << format_percent(s.confidence) << "\n";
                std::cout << "  Files affected: " << s.impact.total_files_affected << "\n";

                if (!s.caveats.empty()) {
                    std::cout << "\n";
                    if (colors::enabled()) {
                        std::cout << colors::YELLOW << "Caveats:" << colors::RESET << "\n";
                    } else {
                        std::cout << "Caveats:\n";
                    }
                    for (const auto& caveat : s.caveats) {
                        std::cout << "  - " << caveat << "\n";
                    }
                }

                if (!s.verification.empty()) {
                    std::cout << "\nVerification:\n";
                    std::cout << "  " << s.verification << "\n";
                }
            }
        }
    };

    namespace {
        struct SuggestCommandRegistrar {
            SuggestCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<SuggestCommand>()
                );
            }
        } suggest_registrar;
    }
}  // namespace bha::cli