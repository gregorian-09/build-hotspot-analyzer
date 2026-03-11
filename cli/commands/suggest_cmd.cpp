//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"
#include "bha/cli/suggestion_utils.hpp"

#include "bha/bha.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/suggestions/suggester.hpp"
#include "bha/suggestions/suggester_catalog.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <unordered_set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace bha::cli
{
    namespace fs = std::filesystem;

    namespace {
        nlohmann::json descriptor_to_json(const suggestions::SuggesterDescriptor& descriptor) {
            nlohmann::json supported_types = nlohmann::json::array();
            for (const auto type : descriptor.supported_types) {
                supported_types.push_back(suggestions::suggestion_type_id(type));
            }

            nlohmann::json requirements = nlohmann::json::array();
            for (const auto& requirement : descriptor.input_requirements) {
                requirements.push_back(requirement);
            }

            return nlohmann::json{
                {"id", descriptor.id},
                {"className", descriptor.class_name},
                {"description", descriptor.description},
                {"supportedTypes", supported_types},
                {"inputRequirements", requirements},
                {"potentiallyAutoApplicable", descriptor.potentially_auto_applicable},
                {"supportsExplainMode", descriptor.supports_explain_mode}
            };
        }

        void print_descriptor_text(const suggestions::SuggesterDescriptor& descriptor) {
            std::cout << descriptor.id << " (" << descriptor.class_name << ")\n";
            std::cout << "  " << descriptor.description << "\n";
            std::cout << "  Supported types: ";
            for (std::size_t i = 0; i < descriptor.supported_types.size(); ++i) {
                if (i > 0) {
                    std::cout << ", ";
                }
                std::cout << suggestions::suggestion_type_id(descriptor.supported_types[i]);
            }
            std::cout << "\n";
            std::cout << "  Potentially auto-applicable: " << (descriptor.potentially_auto_applicable ? "yes" : "no") << "\n";
            std::cout << "  Input requirements:\n";
            for (const auto& requirement : descriptor.input_requirements) {
                std::cout << "    - " << requirement << "\n";
            }
        }

        std::vector<SuggestionType> intersect_types(
            const std::vector<SuggestionType>& lhs,
            const std::vector<SuggestionType>& rhs
        ) {
            std::vector<SuggestionType> result;
            for (const auto type : lhs) {
                if (std::ranges::find(rhs, type) != rhs.end() &&
                    std::ranges::find(result, type) == result.end()) {
                    result.push_back(type);
                }
            }
            return result;
        }
    }

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
                   "  bha suggest --type pch --type forward-decl traces/\n"
                   "  bha suggest --list-suggesters\n"
                   "  bha suggest --describe-suggester pch";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"output", 'o', "Output file for suggestions", false, true, "", "FILE"},
                {"format", 'f', "Output format (text, json)", false, true, "text", "FORMAT"},
                {"limit", 'n', "Maximum number of suggestions", false, true, "20", "N"},
                {"min-priority", 'p', "Minimum priority (low, medium, high, critical)", false, true, "low", "LEVEL"},
                {"min-confidence", 'c', "Minimum confidence (0.0-1.0)", false, true, "0.5", "VALUE"},
                {"type", 0, "Filter by suggestion type (can be repeated)", false, true, "", "TYPE"},
                {"suggester", 0, "Filter by suggester id/class-name (can be repeated)", false, true, "", "NAME"},
                {"list-suggesters", 0, "List available suggesters and required inputs", false, false, "", ""},
                {"describe-suggester", 0, "Describe a specific suggester without running analysis", false, true, "", "NAME"},
                {"explain", 0, "Relax thresholds for exploratory suggester runs", false, false, "", ""},
                {"include-unsafe", 0, "Include potentially unsafe suggestions", false, false, "", ""},
                {"detailed", 'd', "Show detailed suggestion info", false, false, "", ""},
                {"disable-consolidation", 0, "Disable suggestion consolidation", false, false, "", ""},
                {"unreal-mode", 0, "Enable Unreal module-aware suggestions", false, false, "", ""},

                    // Heuristics configuration overrides
                {"pch-min-includes", 0, "Min header inclusions for PCH (default: 10)", false, true, "10", "N"},
                {"pch-min-time", 0, "Min aggregate parse time for PCH in ms (default: 500)", false, true, "500", "MS"},
                {"template-min-count", 0, "Min template instantiation count (default: 5)", false, true, "5", "N"},
                {"template-min-time", 0, "Min template time in ms (default: 100)", false, true, "100", "MS"},
                {"unity-files-per-unit", 0, "Files per unity build unit (default: 50)", false, true, "50", "N"},
                {"unity-min-files", 0, "Min files for unity build (default: 10)", false, true, "10", "N"},
                {"unity-min-time", 0, "Min aggregate unity group compile time in ms (default: 10)", false, true, "10", "MS"},
                {"header-min-time", 0, "Min header parse time in ms (default: 100)", false, true, "100", "MS"},
                {"header-min-includers", 0, "Min includers for header split (default: 5)", false, true, "5", "N"},
                {"fwd-decl-min-time", 0, "Min parse time for fwd decl in ms (default: 50)", false, true, "50", "MS"},
                {"codegen-threshold", 0, "Long code generation threshold in ms (default: 500)", false, true, "500", "MS"},
                {"unreal-min-module-files", 0, "Min Unreal module source files for unity suggestions (default: 8)", false, true, "8", "N"},
                {"unreal-min-pch-time", 0, "Min Unreal module include parse time for PCH in ms (default: 300)", false, true, "300", "MS"},
                {"max-suggest-time", 0, "Max total suggestion time in ms (default: 0, no limit)", false, true, "0", "MS"},
                {"max-suggester-time", 0, "Max time per suggester in ms (default: 0, no limit)", false, true, "0", "MS"},
                {"max-analyze-time", 0, "Max total analysis time in ms (default: 0, no limit)", false, true, "0", "MS"},
                {"max-analyzer-time", 0, "Max time per analyzer in ms (default: 0, no limit)", false, true, "0", "MS"},
                {"max-files", 0, "Max files to report (default: 10)", false, true, "10", "N"},
                {"min-file-time", 0, "Min file time threshold in ms (default: 10)", false, true, "10", "MS"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            const bool list_mode = args.get_flag("list-suggesters");
            const bool describe_mode = args.get("describe-suggester").has_value();
            if (!list_mode && !describe_mode && args.positional().empty()) {
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

            if (args.get_flag("list-suggesters")) {
                const auto descriptors = suggestions::list_suggester_descriptors();
                if (is_json()) {
                    nlohmann::json payload = nlohmann::json::array();
                    for (const auto& descriptor : descriptors) {
                        payload.push_back(descriptor_to_json(descriptor));
                    }
                    std::cout << payload.dump(2) << "\n";
                } else {
                    std::cout << "Available suggesters:\n\n";
                    for (const auto& descriptor : descriptors) {
                        print_descriptor_text(descriptor);
                        std::cout << "\n";
                    }
                }
                return 0;
            }

            if (auto requested = args.get("describe-suggester")) {
                const auto descriptor = suggestions::find_suggester_descriptor(*requested);
                if (!descriptor.has_value()) {
                    print_error("Unknown suggester: " + *requested);
                    return 1;
                }
                if (is_json()) {
                    std::cout << descriptor_to_json(*descriptor).dump(2) << "\n";
                } else {
                    print_descriptor_text(*descriptor);
                }
                return 0;
            }

            std::size_t limit = static_cast<std::size_t>(args.get_int("limit").value_or(20));
            double min_confidence = args.get_double("min-confidence").value_or(0.5);
            bool include_unsafe = args.get_flag("include-unsafe");
            bool detailed = args.get_flag("detailed");

            auto min_priority = Priority::Low;
            if (auto priority_str = args.get_or("min-priority", "low"); priority_str == "critical") min_priority = Priority::Critical;
            else if (priority_str == "high") min_priority = Priority::High;
            else if (priority_str == "medium") min_priority = Priority::Medium;

            std::vector<SuggestionType> cli_type_filters;
            if (auto type_filters = args.get_all("type"); !type_filters.empty()) {
                for (const auto& type_str : type_filters) {
                    if (auto type = suggestions::parse_suggestion_type_id(type_str)) {
                        if (std::ranges::find(cli_type_filters, *type) == cli_type_filters.end()) {
                            cli_type_filters.push_back(*type);
                        }
                    } else {
                        print_error("Unknown suggestion type: " + type_str);
                        print_error("Valid types: pch, forward-decl, header-split, include-removal, move-to-cpp, template-instantiation, unity-build, pimpl.");
                        return 1;
                    }
                }
            }

            std::vector<SuggestionType> suggester_type_filters;
            if (auto suggester_filters = args.get_all("suggester"); !suggester_filters.empty()) {
                for (const auto& token : suggester_filters) {
                    const auto descriptor = suggestions::find_suggester_descriptor(token);
                    if (!descriptor.has_value()) {
                        print_error("Unknown suggester: " + token);
                        return 1;
                    }
                    for (const auto type : descriptor->supported_types) {
                        if (std::ranges::find(suggester_type_filters, type) == suggester_type_filters.end()) {
                            suggester_type_filters.push_back(type);
                        }
                    }
                }
            }

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
            analysis_opts.verbose = is_verbose() && !is_json();
            if (auto val = args.get_int("max-analyze-time")) {
                analysis_opts.max_total_time = std::chrono::milliseconds(*val);
            }
            if (auto val = args.get_int("max-analyzer-time")) {
                analysis_opts.max_analyzer_time = std::chrono::milliseconds(*val);
            }
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
            suggester_opts.enable_consolidation = !args.get_flag("disable-consolidation");
            if (auto val = args.get_int("max-suggest-time")) {
                suggester_opts.max_total_time = std::chrono::milliseconds(*val);
            }
            if (auto val = args.get_int("max-suggester-time")) {
                suggester_opts.max_suggester_time = std::chrono::milliseconds(*val);
            }

            if (!cli_type_filters.empty() && !suggester_type_filters.empty()) {
                suggester_opts.enabled_types = intersect_types(cli_type_filters, suggester_type_filters);
            } else if (!cli_type_filters.empty()) {
                suggester_opts.enabled_types = cli_type_filters;
            } else if (!suggester_type_filters.empty()) {
                suggester_opts.enabled_types = suggester_type_filters;
            }

            if (!suggester_opts.enabled_types.empty()) {
                print_verbose("Filtering suggestions to " + std::to_string(suggester_opts.enabled_types.size()) + " types");
            }

            if (args.get_flag("explain")) {
                suggester_opts.min_confidence = 0.0;
                suggester_opts.include_unsafe = true;
                suggester_opts.enable_consolidation = false;
            }

            // Apply heuristics config overrides from CLI
            auto& analysis = suggester_opts.heuristics.analysis;
            auto& pch = suggester_opts.heuristics.pch;
            auto& templates = suggester_opts.heuristics.templates;
            auto& codegen = suggester_opts.heuristics.codegen;
            auto& headers = suggester_opts.heuristics.headers;
            auto& unity_build = suggester_opts.heuristics.unity_build;
            auto& forward_decl = suggester_opts.heuristics.forward_decl;
            auto& unreal = suggester_opts.heuristics.unreal;

            unreal.enabled = args.get_flag("unreal-mode");

            if (auto val = args.get_int("pch-min-includes")) {
                pch.min_include_count = static_cast<std::size_t>(*val);
            }
            if (auto val = args.get_int("pch-min-time")) {
                pch.min_aggregate_time = std::chrono::milliseconds(*val);
            }

            if (auto val = args.get_int("template-min-count")) {
                templates.min_instantiation_count = static_cast<std::size_t>(*val);
            }
            if (auto val = args.get_int("template-min-time")) {
                templates.min_total_time = std::chrono::milliseconds(*val);
            }

            if (auto val = args.get_int("unity-files-per-unit")) {
                unity_build.files_per_unit = static_cast<std::size_t>(*val);
            }
            if (auto val = args.get_int("unity-min-files")) {
                unity_build.min_files_threshold = static_cast<std::size_t>(*val);
            }
            if (auto val = args.get_int("unity-min-time")) {
                unity_build.min_group_total_time = std::chrono::milliseconds(*val);
            }

            if (auto val = args.get_int("header-min-time")) {
                headers.min_parse_time = std::chrono::milliseconds(*val);
            }
            if (auto val = args.get_int("header-min-includers")) {
                headers.min_includers_for_split = static_cast<std::size_t>(*val);
            }

            if (auto val = args.get_int("fwd-decl-min-time")) {
                forward_decl.min_parse_time = std::chrono::milliseconds(*val);
            }

            if (auto val = args.get_int("codegen-threshold")) {
                codegen.long_codegen_threshold = std::chrono::milliseconds(*val);
            }
            if (auto val = args.get_int("unreal-min-module-files")) {
                unreal.min_module_files_for_unity = static_cast<std::size_t>(*val);
            }
            if (auto val = args.get_int("unreal-min-pch-time")) {
                unreal.min_module_include_time_for_pch = std::chrono::milliseconds(*val);
            }

            if (args.get_flag("explain")) {
                pch.min_include_count = 1;
                pch.min_aggregate_time = std::chrono::milliseconds(1);
                templates.min_instantiation_count = 1;
                templates.min_total_time = std::chrono::milliseconds(1);
                unity_build.min_files_threshold = 2;
                unity_build.min_group_total_time = std::chrono::milliseconds(1);
                headers.min_parse_time = std::chrono::milliseconds(1);
                headers.min_includers_for_split = 1;
                forward_decl.min_parse_time = std::chrono::milliseconds(1);
                codegen.long_codegen_threshold = std::chrono::milliseconds(1);
                unreal.min_module_files_for_unity = 1;
                unreal.min_module_include_time_for_pch = std::chrono::milliseconds(1);
            }

            if (auto val = args.get_int("max-files")) {
                analysis.max_files_to_report = static_cast<std::size_t>(*val);
            }
            if (auto val = args.get_int("min-file-time")) {
                analysis.min_file_time = std::chrono::milliseconds(*val);
            }

            print_verbose("Heuristics config:");
            print_verbose("  PCH min includes: " + std::to_string(pch.min_include_count));
            print_verbose("  PCH min time: " + std::to_string(pch.min_aggregate_time.count()) + "ms");
            print_verbose("  Template min count: " + std::to_string(templates.min_instantiation_count));
            print_verbose("  Unity min files: " + std::to_string(unity_build.min_files_threshold));
            print_verbose("  Unity min time: " + std::to_string(unity_build.min_group_total_time.count()) + "ms");
            print_verbose("  Unreal mode: " + std::string(unreal.enabled ? "enabled" : "auto-detect"));

            std::vector<fs::path> input_paths;
            input_paths.reserve(args.positional().size());
            for (const auto& path : args.positional()) {
                input_paths.emplace_back(path);
            }
            fs::path project_root = suggestion_utils::resolve_project_root_for_suggestions(
                input_paths,
                build_trace,
                analysis_result.value(),
                20
            );
            if (const auto compile_commands_path = suggestion_utils::find_compile_commands_path(project_root)) {
                suggester_opts.compile_commands_path = *compile_commands_path;
                print_verbose("Using compile_commands: " + compile_commands_path->generic_string());
            }
            print_verbose("Resolved project root: " + project_root.generic_string());
            auto suggestions_result = suggestions::generate_all_suggestions(
                build_trace, analysis_result.value(), suggester_opts, project_root
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

                if (!s.before_code.code.empty() || !s.after_code.code.empty()) {
                    std::cout << "Code Changes:\n";
                    if (!s.before_code.code.empty()) {
                        if (colors::enabled()) {
                            std::cout << colors::RED << "  --- Before";
                        } else {
                            std::cout << "  --- Before";
                        }
                        if (!s.before_code.file.empty()) {
                            std::cout << " (" << s.before_code.file.string();
                            if (s.before_code.line > 0) {
                                std::cout << ":" << s.before_code.line;
                            }
                            std::cout << ")";
                        }
                        if (colors::enabled()) {
                            std::cout << colors::RESET;
                        }
                        std::cout << "\n";

                        std::istringstream stream(s.before_code.code);
                        std::string line;
                        while (std::getline(stream, line)) {
                            if (colors::enabled()) {
                                std::cout << colors::DIM << "  | " << colors::RESET << line << "\n";
                            } else {
                                std::cout << "  | " << line << "\n";
                            }
                        }
                        std::cout << "\n";
                    }

                    if (!s.after_code.code.empty()) {
                        if (colors::enabled()) {
                            std::cout << colors::GREEN << "  +++ After";
                        } else {
                            std::cout << "  +++ After";
                        }
                        if (!s.after_code.file.empty()) {
                            std::cout << " (" << s.after_code.file.string();
                            if (s.after_code.line > 0) {
                                std::cout << ":" << s.after_code.line;
                            }
                            std::cout << ")";
                        }
                        if (colors::enabled()) {
                            std::cout << colors::RESET;
                        }
                        std::cout << "\n";

                        std::istringstream stream(s.after_code.code);
                        std::string line;
                        while (std::getline(stream, line)) {
                            if (colors::enabled()) {
                                std::cout << colors::DIM << "  | " << colors::RESET << line << "\n";
                            } else {
                                std::cout << "  | " << line << "\n";
                            }
                        }
                        std::cout << "\n";
                    }
                }

                if (!s.edits.empty()) {
                    if (colors::enabled()) {
                        std::cout << colors::CYAN << "Text Edits (" << s.edits.size() << "):" << colors::RESET << "\n";
                    } else {
                        std::cout << "Text Edits (" << s.edits.size() << "):\n";
                    }
                    for (std::size_t j = 0; j < s.edits.size(); ++j) {
                        const auto& [file, start_line, start_col, end_line, end_col, new_text] = s.edits[j];
                        std::cout << "  [" << (j + 1) << "] " << file.string();
                        std::cout << ":" << start_line << ":" << start_col;
                        std::cout << " -> " << end_line << ":" << end_col << "\n";

                        if (!new_text.empty()) {
                            std::istringstream stream(new_text);
                            std::string line;
                            bool first = true;
                            while (std::getline(stream, line)) {
                                if (first) {
                                    std::cout << "      Replace with: ";
                                    first = false;
                                } else {
                                    std::cout << "                    ";
                                }
                                if (colors::enabled()) {
                                    std::cout << colors::GREEN << line << colors::RESET << "\n";
                                } else {
                                    std::cout << line << "\n";
                                }
                            }
                            if (new_text.empty() || new_text.back() == '\n') {
                            }
                        } else {
                            if (colors::enabled()) {
                                std::cout << "      " << colors::RED << "(delete)" << colors::RESET << "\n";
                            } else {
                                std::cout << "      (delete)\n";
                            }
                        }
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
                std::cout << "  Confidence: " << format_percent(s.confidence * 100.0) << "\n";
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

                if (s.is_safe) {
                    if (colors::enabled()) {
                        std::cout << "\n" << colors::GREEN << "✓ This suggestion is marked as safe to apply automatically" << colors::RESET << "\n";
                    } else {
                        std::cout << "\n[Safe to apply automatically]\n";
                    }
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
