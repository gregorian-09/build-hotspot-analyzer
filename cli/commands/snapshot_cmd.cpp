//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"

#include "bha/bha.hpp"
#include "bha/storage.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/suggestions/suggester.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace bha::cli
{
    namespace {

        /**
         * Formats a timestamp for display.
         */
        std::string format_time(const Timestamp ts) {
            const auto time_t_val = std::chrono::system_clock::to_time_t(ts);
            std::ostringstream ss;

#ifdef _WIN32
            std::tm time_info{};
            localtime_s(&time_info, &time_t_val);
            ss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
#else
            ss << std::put_time(std::localtime(&time_t_val), "%Y-%m-%d %H:%M:%S");
#endif

            return ss.str();
        }

        /**
         * Formats duration for display.
         */
        std::string format_duration_short(Duration d) {
            if (const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count(); ms < 1000) {
                return std::to_string(ms) + "ms";
            } else
            {
                if (ms < 60000) {
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(1) << (static_cast<double>(ms) / 1000.0) << "s";
                    return ss.str();
                }
                const auto mins = ms / 60000;
                const auto secs = (ms % 60000) / 1000;
                return std::to_string(mins) + "m " + std::to_string(secs) + "s";
            }
        }

        /**
         * Helper to format bold text.
         */
        std::string bold(const std::string& text) {
            if (colors::enabled()) {
                return std::string(colors::BOLD) + text + colors::RESET;
            }
            return text;
        }

        /**
         * Helper to format green text.
         */
        std::string green(const std::string& text) {
            if (colors::enabled()) {
                return std::string(colors::GREEN) + text + colors::RESET;
            }
            return text;
        }

    }  // namespace

    /**
     * Snapshot command - manages build snapshots for comparison.
     */
    class SnapshotCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "snapshot";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Manage build analysis snapshots for comparison";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha snapshot <subcommand> [OPTIONS]\n"
                   "\n"
                   "Subcommands:\n"
                   "  save <name> <trace-file>   Save a snapshot from trace file\n"
                   "  list                       List all snapshots\n"
                   "  show <name>                Show snapshot details\n"
                   "  delete <name>              Delete a snapshot\n"
                   "\n"
                   "Examples:\n"
                   "  bha snapshot save v1.0 build/trace.json\n"
                   "  bha snapshot save before-refactor trace.json -d \"Before major refactor\"\n"
                   "  bha snapshot list\n"
                   "  bha snapshot show v1.0\n"
                   "  bha snapshot delete old-snapshot";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"description", 'd', "Description for the snapshot", false, true, "", "TEXT"},
                {"tag", 0, "Add a tag to the snapshot", false, true, "", "TAG"},
                {"storage", 0, "Storage directory", false, true, ".bha/snapshots", "DIR"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.positional().empty()) {
                return "No subcommand specified. Use 'bha snapshot list|save|show|delete'";
            }

            const std::string& subcommand = args.positional()[0];
            if (subcommand != "save" && subcommand != "list" &&
                subcommand != "show" && subcommand != "delete") {
                return "Unknown subcommand: " + subcommand;
                }

            if (subcommand == "save" && args.positional().size() < 3) {
                return "Usage: bha snapshot save <name> <trace-file>";
            }

            if ((subcommand == "show" || subcommand == "delete") &&
                args.positional().size() < 2) {
                return "Usage: bha snapshot " + subcommand + " <name>";
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

            if (args.get_flag("json")) {
                set_output_format(OutputFormat::JSON);
            }

            const fs::path storage_dir = args.get_or("storage", ".bha/snapshots");
            storage::SnapshotStore store(storage_dir);

            if (const std::string& subcommand = args.positional()[0]; subcommand == "list") {
                return list_snapshots(store);
            } else
            {
                if (subcommand == "save") {
                    const std::string snap_name = args.positional()[1];
                    const fs::path trace_file = args.positional()[2];
                    const std::string desc = args.get_or("description", "");

                    // Parse tags (could have multiple --tag arguments)
                    std::vector<std::string> tags;
                    if (const auto tag = args.get("tag")) {
                        tags.push_back(*tag);
                    }

                    return save_snapshot(store, snap_name, trace_file, desc, tags);
                }
                if (subcommand == "show") {
                    const std::string snap_name = args.positional()[1];
                    return show_snapshot(store, snap_name);
                }
                if (subcommand == "delete") {
                    const std::string snap_name = args.positional()[1];
                    return delete_snapshot(store, snap_name);
                }
            }

            return 1;
        }

    private:
        [[nodiscard]] int list_snapshots(const storage::SnapshotStore& store) const
        {
            auto result = store.list();
            if (result.is_err()) {
                print_error("Failed to list snapshots: " + result.error().message());
                return 1;
            }

            const auto& snapshots = result.value();
            if (snapshots.empty()) {
                print("No snapshots found.");
                print("Create one with: bha snapshot save <name> <trace-file>");
                return 0;
            }

            const auto baseline = store.get_baseline();

            if (is_json()) {
                std::cout << "[\n";
                for (std::size_t i = 0; i < snapshots.size(); ++i) {
                    const auto& s = snapshots[i];
                    std::cout << "  {\n";
                    std::cout << R"(    "name": ")" << s.name << "\",\n";
                    std::cout << R"(    "description": ")" << s.description << "\",\n";
                    std::cout << R"(    "created_at": ")" << format_time(s.created_at) << "\",\n";
                    std::cout << R"(    "git_commit": ")" << s.git_commit << "\",\n";
                    std::cout << R"(    "git_branch": ")" << s.git_branch << "\",\n";
                    std::cout << "    \"file_count\": " << s.file_count << ",\n";
                    std::cout << "    \"total_build_time_ms\": "
                              << std::chrono::duration_cast<std::chrono::milliseconds>(s.total_build_time).count() << ",\n";
                    std::cout << "    \"is_baseline\": " << (baseline && *baseline == s.name ? "true" : "false") << "\n";
                    std::cout << "  }" << (i < snapshots.size() - 1 ? "," : "") << "\n";
                }
                std::cout << "]\n";
            } else {
                Table table({
                    {"Name", 20, false, std::nullopt},
                    {"Created", 20, false, std::nullopt},
                    {"Build Time", 12, true, std::nullopt},
                    {"Files", 8, true, std::nullopt},
                    {"Git", 25, false, std::nullopt}
                });

                for (const auto& s : snapshots) {
                    std::string snap_name = s.name;
                    if (baseline && *baseline == s.name) {
                        snap_name += " *";  // Mark baseline
                    }

                    std::string git_info;
                    if (!s.git_branch.empty()) {
                        git_info = s.git_branch;
                        if (!s.git_commit.empty() && s.git_commit.size() >= 7) {
                            git_info += " (" + s.git_commit.substr(0, 7) + ")";
                        }
                    }

                    table.add_row({
                        snap_name,
                        format_time(s.created_at),
                        format_duration_short(s.total_build_time),
                        std::to_string(s.file_count),
                        git_info.empty() ? "-" : git_info
                    });
                }

                table.render(std::cout);

                if (baseline) {
                    std::cout << "\n* = baseline\n";
                }
            }

            return 0;
        }

        int save_snapshot(
            storage::SnapshotStore& store,
            const std::string& snap_name,
            const fs::path& trace_file,
            const std::string& description,
            const std::vector<std::string>& tags
        ) const
        {
            if (store.exists(snap_name)) {
                print_error("Snapshot already exists: " + snap_name);
                print("Use 'bha snapshot delete " + snap_name + "' to remove it first.");
                return 1;
            }

            if (!fs::exists(trace_file)) {
                print_error("Trace file not found: " + trace_file.string());
                return 1;
            }

            print_verbose("Parsing trace file: " + trace_file.string());

            Spinner spinner("Parsing trace file");
            auto parse_result = parsers::parse_trace_file(trace_file);
            if (parse_result.is_err()) {
                spinner.fail("Failed to parse: " + parse_result.error().message());
                return 1;
            }

            BuildTrace trace;
            trace.timestamp = std::chrono::system_clock::now();
            trace.total_time = parse_result.value().metrics.total_time;
            trace.units.push_back(std::move(parse_result.value()));

            Spinner spinner2("Analyzing build");
            AnalysisOptions opts;
            opts.analyze_templates = true;
            opts.analyze_includes = true;

            auto analysis_result = analyzers::run_full_analysis(trace, opts);
            if (analysis_result.is_err()) {
                spinner2.fail("Analysis failed: " + analysis_result.error().message());
                return 1;
            }
            spinner2.success("Analyzed");

            Spinner spinner3("Generating suggestions");
            auto sugg_result = suggestions::generate_all_suggestions(trace, analysis_result.value(), SuggesterOptions{});
            std::vector<Suggestion> sugg_list;
            if (sugg_result.is_ok()) {
                sugg_list = std::move(sugg_result.value());
            }
            spinner3.success(std::to_string(sugg_list.size()) + " suggestions");

            Spinner spinner4("Saving snapshot");
            if (auto save_result = store.save(snap_name, analysis_result.value(), sugg_list, description, tags); save_result.is_err()) {
                spinner4.fail("Failed to save: " + save_result.error().message());
                return 1;
            }
            spinner4.success("Saved");

            if (!is_quiet()) {
                std::cout << "\n";
                std::cout << green("Snapshot saved: ") << snap_name << "\n";
                std::cout << "  Files analyzed: " << analysis_result.value().files.size() << "\n";
                std::cout << "  Build time: " << format_duration_short(
                    analysis_result.value().performance.total_build_time) << "\n";
                std::cout << "  Suggestions: " << sugg_list.size() << "\n";
                std::cout << "\nCompare with: bha compare " << snap_name << " <other-snapshot>\n";
            }

            return 0;
        }

        [[nodiscard]] int show_snapshot(const storage::SnapshotStore& store, const std::string& snap_name) const
        {
            auto result = store.load(snap_name);
            if (result.is_err()) {
                print_error("Snapshot not found: " + snap_name);
                return 1;
            }

            const auto& [metadata, analysis, suggestions] = result.value();
            const auto& meta = metadata;
            const auto& snapshot_analysis = analysis;

            if (is_json()) {
                std::cout << "{\n";
                std::cout << R"(  "name": ")" << meta.name << "\",\n";
                std::cout << R"(  "description": ")" << meta.description << "\",\n";
                std::cout << R"(  "created_at": ")" << format_time(meta.created_at) << "\",\n";
                std::cout << R"(  "git_commit": ")" << meta.git_commit << "\",\n";
                std::cout << R"(  "git_branch": ")" << meta.git_branch << "\",\n";
                std::cout << "  \"file_count\": " << meta.file_count << ",\n";
                std::cout << "  \"total_build_time_ms\": "
                          << std::chrono::duration_cast<std::chrono::milliseconds>(meta.total_build_time).count() << ",\n";
                std::cout << "  \"suggestions_count\": " << suggestions.size() << "\n";
                std::cout << "}\n";
            } else {
                std::cout << bold("Snapshot: ") << meta.name << "\n\n";

                std::cout << "Created:     " << format_time(meta.created_at) << "\n";
                if (!meta.description.empty()) {
                    std::cout << "Description: " << meta.description << "\n";
                }
                if (!meta.git_branch.empty()) {
                    std::cout << "Git Branch:  " << meta.git_branch << "\n";
                }
                if (!meta.git_commit.empty()) {
                    std::cout << "Git Commit:  " << meta.git_commit << "\n";
                }

                std::cout << "\n" << bold("Build Summary") << "\n";
                std::cout << "  Total Build Time:  " << format_duration_short(snapshot_analysis.performance.total_build_time) << "\n";
                std::cout << "  Files Analyzed:    " << snapshot_analysis.files.size() << "\n";
                std::cout << "  Avg File Time:     " << format_duration_short(snapshot_analysis.performance.avg_file_time) << "\n";
                std::cout << "  Parallelism:       " << std::fixed << std::setprecision(1)
                          << (snapshot_analysis.performance.parallelism_efficiency * 100.0) << "%\n";

                std::cout << "\n" << bold("Dependencies") << "\n";
                std::cout << "  Unique Headers:    " << snapshot_analysis.dependencies.unique_headers << "\n";
                std::cout << "  Total Includes:    " << snapshot_analysis.dependencies.total_includes << "\n";
                std::cout << "  Max Include Depth: " << snapshot_analysis.dependencies.max_include_depth << "\n";

                std::cout << "\n" << bold("Templates") << "\n";
                std::cout << "  Total Instantiations: " << snapshot_analysis.templates.total_instantiations << "\n";
                std::cout << "  Template Time:        " << format_duration_short(snapshot_analysis.templates.total_template_time) << "\n";

                std::cout << "\n" << bold("Suggestions: ") << suggestions.size() << "\n";

                if (!snapshot_analysis.files.empty()) {
                    std::cout << "\n" << bold("Top 5 Slowest Files") << "\n";
                    std::vector<analyzers::FileAnalysisResult> sorted_files = snapshot_analysis.files;
                    std::ranges::sort(sorted_files,
                                      [](const auto& a, const auto& b) {
                                          return a.compile_time > b.compile_time;
                                      });

                    for (std::size_t i = 0; i < std::min<std::size_t>(5, sorted_files.size()); ++i) {
                        std::cout << "  " << (i + 1) << ". "
                                  << format_path(sorted_files[i].file, 40) << " - "
                                  << format_duration_short(sorted_files[i].compile_time) << "\n";
                    }
                }
            }

            return 0;
        }

        [[nodiscard]] int delete_snapshot(const storage::SnapshotStore& store, const std::string& snap_name) const
        {
            if (!store.exists(snap_name)) {
                print_error("Snapshot not found: " + snap_name);
                return 1;
            }

            if (auto result = store.remove(snap_name); result.is_err()) {
                print_error("Failed to delete snapshot: " + result.error().message());
                return 1;
            }

            print("Snapshot deleted: " + snap_name);
            return 0;
        }
    };

    // Register the command
    namespace {
        struct SnapshotCommandRegistrar {
            SnapshotCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<SnapshotCommand>()
                );
            }
        } snapshot_registrar;
    }
}  // namespace bha::cli