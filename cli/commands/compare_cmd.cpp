//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"

#include "bha/bha.hpp"
#include "bha/storage.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <optional>
#include <vector>

namespace bha::cli
{
    namespace {

        /**
         * Formats duration for display.
         */
        std::string format_dur(const Duration d) {
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
         * Helper for bold text.
         */
        std::string bold(const std::string& text) {
            if (colors::enabled()) {
                return std::string(colors::BOLD) + text + colors::RESET;
            }
            return text;
        }

        /**
         * Helper for red text.
         */
        std::string red(const std::string& text) {
            if (colors::enabled()) {
                return std::string(colors::RED) + text + colors::RESET;
            }
            return text;
        }

        /**
         * Helper for green text.
         */
        std::string green(const std::string& text) {
            if (colors::enabled()) {
                return std::string(colors::GREEN) + text + colors::RESET;
            }
            return text;
        }

        /**
         * Helper for dim text.
         */
        std::string dim(const std::string& text) {
            if (colors::enabled()) {
                return std::string(colors::DIM) + text + colors::RESET;
            }
            return text;
        }

        /**
         * Formats a delta with color.
         */
        std::string format_delta(const Duration d, const bool invert = false) {
            if (const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count(); ms == 0) {
                return dim("+/-0");
            } else
            {
                if (ms > 0) {
                    const std::string value = "+" + format_dur(d);
                    return invert ? green(value) : red(value);
                }
                const std::string value = "-" + format_dur(Duration(-ms));
                return invert ? red(value) : green(value);
            }
        }

        /**
         * Formats a percentage change with color.
         */
        std::string format_percent_change(const double percent) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1);

            if (std::abs(percent) < 0.1) {
                return dim("0%");
            }
            if (percent > 0) {
                ss << "+" << percent << "%";
                return red(ss.str());
            }
            ss << percent << "%";
            return green(ss.str());
        }

        std::string format_percent_change(const std::optional<double> percent) {
            if (!percent.has_value()) {
                return dim("n/a");
            }
            return format_percent_change(*percent);
        }

        std::string format_percent_plain(const double percent) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << percent << "%";
            return ss.str();
        }

        std::string format_percent_plain(const std::optional<double> percent) {
            return percent.has_value() ? format_percent_plain(*percent) : "n/a";
        }

        std::string format_percent_magnitude(const std::optional<double> percent) {
            return percent.has_value() ? format_percent_plain(std::abs(*percent)) : "n/a";
        }

        bool gate_exceeded(
            const std::optional<double> percent_change,
            const std::optional<double> gate_percent
        ) {
            return percent_change.has_value() && gate_percent.has_value() &&
                *percent_change > *gate_percent;
        }

        std::string format_optional_json_number(const std::optional<double> value) {
            if (!value.has_value()) {
                return "null";
            }
            std::ostringstream output;
            output << std::setprecision(17) << *value;
            return output.str();
        }

    }  // namespace

    /**
     * Compare command - compares two build snapshots.
     */
    class CompareCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "compare";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Compare two build snapshots to identify regressions and improvements";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha compare <old-snapshot> <new-snapshot> [OPTIONS]\n"
                   "       bha compare --repeat <snapshot> <snapshot> [...] [OPTIONS]\n"
                   "       bha compare --baseline <new-snapshot> [OPTIONS]\n"
                   "\n"
                   "Compare two snapshots to identify build time changes, regressions,\n"
                   "and improvements between builds.\n"
                   "\n"
                   "Examples:\n"
                   "  bha compare v1.0 v2.0\n"
                   "  bha compare before-refactor after-refactor\n"
                   "  bha compare --repeat clean-1 clean-2 clean-3 --json\n"
                   "  bha compare --baseline current-build\n"
                   "  bha compare v1.0 v2.0 --top 20\n"
                   "  bha compare --baseline current --gate-tu 5 --gate-header 8 --gate-template 10";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"baseline", 'b', "Compare against baseline", false, false, "", ""},
                {"repeat", 0, "Summarize explicitly named repeated-run snapshots", false, false, "", ""},
                {"top", 't', "Number of top changes to show", false, true, "10", "N"},
                {"threshold", 0, "Overall regression threshold (%)", false, true, "5", "PERCENT"},
                {"gate-tu", 0, "Fail if Translation Unit category regresses beyond (%)", false, true, "", "PERCENT"},
                {"gate-header", 0, "Fail if Header category regresses beyond (%)", false, true, "", "PERCENT"},
                {"gate-template", 0, "Fail if Template category regresses beyond (%)", false, true, "", "PERCENT"},
                {"storage", 0, "Storage directory", false, true, ".bha/snapshots", "DIR"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.get_flag("repeat")) {
                if (args.get_flag("baseline")) {
                    return "--repeat cannot be combined with --baseline";
                }
                if (args.positional().size() < 2) {
                    return "Usage: bha compare --repeat <snapshot> <snapshot> [...]";
                }
            } else if (args.get_flag("baseline")) {
                if (args.positional().empty()) {
                    return "Usage: bha compare --baseline <snapshot>";
                }
            } else {
                if (args.positional().size() < 2) {
                    return "Usage: bha compare <old-snapshot> <new-snapshot>";
                }
            }

            const auto threshold = args.get_double("threshold").value_or(5.0);
            if (threshold < 0.0) {
                return "--threshold must be >= 0";
            }
            if (const auto gate_tu = args.get_double("gate-tu"); gate_tu && *gate_tu < 0.0) {
                return "--gate-tu must be >= 0";
            }
            if (const auto gate_header = args.get_double("gate-header"); gate_header && *gate_header < 0.0) {
                return "--gate-header must be >= 0";
            }
            if (const auto gate_template = args.get_double("gate-template"); gate_template && *gate_template < 0.0) {
                return "--gate-template must be >= 0";
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

            fs::path storage_dir = args.get_or("storage", ".bha/snapshots");
            storage::SnapshotStore store(storage_dir);

            if (args.get_flag("repeat")) {
                const auto result = store.summarize_repeated_runs(args.positional());
                if (result.is_err()) {
                    print_error("Repeated-run summary failed: " + result.error().message());
                    return 1;
                }

                if (is_json()) {
                    print_repeated_runs_json(result.value(), args.positional());
                } else {
                    print_repeated_runs(result.value(), args.positional());
                }
                return 0;
            }

            std::string old_name;
            std::string new_name;

            if (args.get_flag("baseline")) {
                auto baseline = store.get_baseline();
                if (!baseline) {
                    print_error("No baseline set. Use 'bha snapshot baseline set <snapshot>' first.");
                    return 1;
                }
                old_name = *baseline;
                new_name = args.positional()[0];
            } else {
                old_name = args.positional()[0];
                new_name = args.positional()[1];
            }

            std::size_t top_count = static_cast<std::size_t>(args.get_int("top").value_or(10));
            const double threshold_percent = args.get_double("threshold").value_or(5.0);
            const auto gate_tu = args.get_double("gate-tu");
            const auto gate_header = args.get_double("gate-header");
            const auto gate_template = args.get_double("gate-template");

            auto result = store.compare(old_name, new_name, threshold_percent / 100.0);
            if (result.is_err()) {
                print_error("Comparison failed: " + result.error().message());
                return 1;
            }

            const auto& comparison = result.value();
            const bool tu_gate_failed = gate_exceeded(comparison.translation_unit.percent_change, gate_tu);
            const bool header_gate_failed = gate_exceeded(comparison.headers.percent_change, gate_header);
            const bool template_gate_failed = gate_exceeded(comparison.templates.percent_change, gate_template);
            const bool category_gate_failed = tu_gate_failed || header_gate_failed || template_gate_failed;
            const bool overall_regression_failed = comparison.is_regression() && comparison.is_significant();

            if (is_json()) {
                print_comparison_json(
                    comparison, old_name, new_name,
                    gate_tu, gate_header, gate_template,
                    tu_gate_failed, header_gate_failed, template_gate_failed,
                    overall_regression_failed
                );
            } else {
                print_comparison(
                    comparison, old_name, new_name, top_count,
                    gate_tu, gate_header, gate_template,
                    tu_gate_failed, header_gate_failed, template_gate_failed,
                    overall_regression_failed
                );
            }

            return (overall_regression_failed || category_gate_failed) ? 1 : 0;
        }

    private:
        static void print_repeated_runs(
            const storage::ComparisonResult::RepeatedRunDistribution& result,
            const std::vector<std::string>& snapshot_names
        ) {
            std::cout << bold("Observed Repeated-Run Distribution") << "\n";
            std::cout << "  Snapshots: ";
            for (std::size_t i = 0; i < snapshot_names.size(); ++i) {
                if (i > 0) {
                    std::cout << ", ";
                }
                std::cout << snapshot_names[i];
            }
            std::cout << "\n";
            std::cout << "  Runs: " << result.run_count << "\n";
            std::cout << "  Min / Mean / Median / P90 / P99 / Max: "
                      << format_dur(result.min_build_time) << " / "
                      << format_dur(result.mean_build_time) << " / "
                      << format_dur(result.median_build_time) << " / "
                      << format_dur(result.p90_build_time) << " / "
                      << format_dur(result.p99_build_time) << " / "
                      << format_dur(result.max_build_time) << "\n";
            if (result.sample_standard_deviation.has_value()) {
                std::cout << "  Sample Std Dev: " << format_dur(*result.sample_standard_deviation) << "\n";
            } else {
                std::cout << "  Sample Std Dev: unavailable (one observation)\n";
            }
            std::cout << "  Statistics are descriptive and use only the named observed snapshots.\n";
        }

        static void print_repeated_runs_json(
            const storage::ComparisonResult::RepeatedRunDistribution& result,
            const std::vector<std::string>& snapshot_names
        ) {
            nlohmann::json distribution = {
                {"run_count", result.run_count},
                {"min_build_time_ns", result.min_build_time.count()},
                {"mean_build_time_ns", result.mean_build_time.count()},
                {"median_build_time_ns", result.median_build_time.count()},
                {"p90_build_time_ns", result.p90_build_time.count()},
                {"p99_build_time_ns", result.p99_build_time.count()},
                {"max_build_time_ns", result.max_build_time.count()}
            };
            if (result.sample_standard_deviation.has_value()) {
                distribution["sample_standard_deviation_ns"] =
                    result.sample_standard_deviation->count();
            } else {
                distribution["sample_standard_deviation_ns"] = nullptr;
            }

            nlohmann::json output = {
                {"snapshots", snapshot_names},
                {"repeated_runs", std::move(distribution)},
                {"statistics", "descriptive observations; no confidence or significance claim"}
            };
            std::cout << output.dump(2) << "\n";
        }

        static void print_comparison(
            const storage::ComparisonResult& result,
            const std::string& old_name,
            const std::string& new_name,
            std::size_t top_count,
            const std::optional<double> gate_tu,
            const std::optional<double> gate_header,
            const std::optional<double> gate_template,
            const bool tu_gate_failed,
            const bool header_gate_failed,
            const bool template_gate_failed,
            const bool overall_regression_failed
        ) {
            std::cout << bold("Build Comparison: ") << old_name << " -> " << new_name << "\n\n";

            // Overall summary
            std::cout << bold("Summary") << "\n";
            std::cout << "  Build Time: " << format_delta(result.build_time_delta) << " ("
                      << format_percent_change(result.build_time_percent_change) << ")\n";
            std::cout << "  File Count: " << (result.file_count_delta >= 0 ? "+" : "")
                      << result.file_count_delta << "\n";
            std::cout << "  Threshold: +" << std::fixed << std::setprecision(1)
                      << result.significance_threshold_percent << "%\n";

            std::cout << "\n" << bold("Category Deltas") << "\n";
            std::cout << "  TU:       " << format_delta(result.translation_unit.delta)
                      << " (" << format_percent_change(result.translation_unit.percent_change) << ")";
            if (gate_tu) {
                std::cout << " gate<=" << format_percent_plain(*gate_tu)
                          << " " << (tu_gate_failed ? red("[FAIL]") : green("[PASS]"));
            }
            std::cout << "\n";

            std::cout << "  Headers:  " << format_delta(result.headers.delta)
                      << " (" << format_percent_change(result.headers.percent_change) << ")";
            if (gate_header) {
                std::cout << " gate<=" << format_percent_plain(*gate_header)
                          << " " << (header_gate_failed ? red("[FAIL]") : green("[PASS]"));
            }
            std::cout << "\n";

            std::cout << "  Templates:" << format_delta(result.templates.delta)
                      << " (" << format_percent_change(result.templates.percent_change) << ")";
            if (gate_template) {
                std::cout << " gate<=" << format_percent_plain(*gate_template)
                          << " " << (template_gate_failed ? red("[FAIL]") : green("[PASS]"));
            }
            std::cout << "\n";

            const auto& distribution = result.translation_unit_regressions;
            if (distribution.matched_files > 0) {
                std::cout << "\n" << bold("Observed TU Regression Distribution") << "\n";
                std::cout << "  Matched Files: " << distribution.matched_files << "\n";
                std::cout << "  Positive Deltas: " << distribution.regressed_files << "\n";
                std::cout << "  Total Delta: " << format_dur(distribution.total_delta) << "\n";
                if (distribution.regressed_files > 0) {
                    std::cout << "  Min / Median / P90 / P99 / Max: "
                              << format_dur(distribution.min_delta) << " / "
                              << format_dur(distribution.median_delta) << " / "
                              << format_dur(distribution.p90_delta) << " / "
                              << format_dur(distribution.p99_delta) << " / "
                              << format_dur(distribution.max_delta) << "\n";
                }
            }

            // Status
            std::cout << "\n";
            if (overall_regression_failed) {
                std::cout << red("! REGRESSION DETECTED") << "\n";
                std::cout << "  Build time increased by "
                          << format_percent_plain(result.build_time_percent_change) << "\n";
            } else if (result.is_improvement() && result.is_significant()) {
                std::cout << green("+ BUILD TIME IMPROVED") << "\n";
                std::cout << "  Build time decreased by "
                          << format_percent_magnitude(result.build_time_percent_change) << "\n";
            } else {
                std::cout << dim("= No significant change") << "\n";
            }

            if (tu_gate_failed || header_gate_failed || template_gate_failed) {
                std::cout << red("! CATEGORY GATE FAILED") << "\n";
                if (tu_gate_failed) {
                    std::cout << "  - TU regression " << format_percent_plain(result.translation_unit.percent_change)
                              << " exceeded gate " << format_percent_plain(*gate_tu) << "\n";
                }
                if (header_gate_failed) {
                    std::cout << "  - Header regression " << format_percent_plain(result.headers.percent_change)
                              << " exceeded gate " << format_percent_plain(*gate_header) << "\n";
                }
                if (template_gate_failed) {
                    std::cout << "  - Template regression " << format_percent_plain(result.templates.percent_change)
                              << " exceeded gate " << format_percent_plain(*gate_template) << "\n";
                }
            }

            if (!result.regressions.empty()) {
                std::cout << "\n" << bold(red("File Regressions"))
                          << " (" << result.regressions.size() << " files slower)\n";

                Table table({
                    {"File", 40, false, std::nullopt},
                    {"Old", 10, true, std::nullopt},
                    {"New", 10, true, std::nullopt},
                    {"Delta", 10, true, std::nullopt},
                    {"Change", 8, true, std::nullopt}
                });

                std::size_t count = 0;
                for (const auto& [file, old_time, new_time, delta, percent_change] : result.regressions) {
                    if (count >= top_count) break;

                    std::ostringstream pct;
                    pct << (percent_change.has_value()
                        ? "+" + format_percent_plain(*percent_change)
                        : "n/a");

                    table.add_row({
                        format_path(file, 40),
                        format_dur(old_time),
                        format_dur(new_time),
                        "+" + format_dur(delta),
                        pct.str()
                    });
                    count++;
                }

                table.render(std::cout);

                if (result.regressions.size() > top_count) {
                    std::cout << "  ... and " << (result.regressions.size() - top_count) << " more\n";
                }
            }

            if (!result.improvements.empty()) {
                std::cout << "\n" << bold(green("File Improvements"))
                          << " (" << result.improvements.size() << " files faster)\n";

                Table table({
                    {"File", 40, false, std::nullopt},
                    {"Old", 10, true, std::nullopt},
                    {"New", 10, true, std::nullopt},
                    {"Delta", 10, true, std::nullopt},
                    {"Change", 8, true, std::nullopt}
                });

                std::size_t count = 0;
                for (const auto& [file, old_time, new_time, delta, percent_change] : result.improvements) {
                    if (count >= top_count) break;

                    table.add_row({
                        format_path(file, 40),
                        format_dur(old_time),
                        format_dur(new_time),
                        format_dur(delta),
                        format_percent_plain(percent_change)
                    });
                    count++;
                }

                table.render(std::cout);

                if (result.improvements.size() > top_count) {
                    std::cout << "  ... and " << (result.improvements.size() - top_count) << " more\n";
                }
            }

            // New and removed files
            if (!result.new_files.empty()) {
                std::cout << "\n" << bold("New Files") << " (" << result.new_files.size() << ")\n";
                std::size_t count = 0;
                for (const auto& file : result.new_files) {
                    if (count >= top_count) break;
                    std::cout << "  + " << format_path(file, 60) << "\n";
                    count++;
                }
                if (result.new_files.size() > top_count) {
                    std::cout << "  ... and " << (result.new_files.size() - top_count) << " more\n";
                }
            }

            if (!result.removed_files.empty()) {
                std::cout << "\n" << bold("Removed Files") << " (" << result.removed_files.size() << ")\n";
                std::size_t count = 0;
                for (const auto& file : result.removed_files) {
                    if (count >= top_count) break;
                    std::cout << "  - " << format_path(file, 60) << "\n";
                    count++;
                }
                if (result.removed_files.size() > top_count) {
                    std::cout << "  ... and " << (result.removed_files.size() - top_count) << " more\n";
                }
            }

            // Header changes
            if (!result.header_regressions.empty()) {
                std::cout << "\n" << bold("Header Regressions")
                          << " (" << result.header_regressions.size() << ")\n";

                std::size_t count = 0;
                for (const auto& [header, old_inclusions, new_inclusions, old_time, new_time] : result.header_regressions) {
                    if (count >= 5) break;
                    std::cout << "  " << format_path(header, 40)
                              << " - inclusions: " << old_inclusions << " -> " << new_inclusions
                              << ", time: " << format_dur(old_time) << " -> " << format_dur(new_time) << "\n";
                    count++;
                }
            }

            // Template changes
            if (!result.template_regressions.empty()) {
                std::cout << "\n" << bold("Template Regressions")
                          << " (" << result.template_regressions.size() << ")\n";

                std::size_t count = 0;
                for (const auto& change : result.template_regressions) {
                    if (count >= 5) break;
                    std::cout << "  " << change.name
                              << " - count: " << change.old_count << " -> " << change.new_count
                              << ", time: " << format_dur(change.old_time)
                              << " -> " << format_dur(change.new_time) << "\n";
                    count++;
                }
            }
        }

        static void print_comparison_json(
            const storage::ComparisonResult& result,
            const std::string& old_name,
            const std::string& new_name,
            const std::optional<double> gate_tu,
            const std::optional<double> gate_header,
            const std::optional<double> gate_template,
            const bool tu_gate_failed,
            const bool header_gate_failed,
            const bool template_gate_failed,
            const bool overall_regression_failed
        ) {
            const bool any_category_gate_failed = tu_gate_failed || header_gate_failed || template_gate_failed;
            std::cout << "{\n";
            std::cout << "  \"old_snapshot\": \"" << old_name << "\",\n";
            std::cout << "  \"new_snapshot\": \"" << new_name << "\",\n";
            std::cout << "  \"build_time_delta_ms\": "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(result.build_time_delta).count() << ",\n";
            std::cout << "  \"build_time_percent_change\": "
                      << format_optional_json_number(result.build_time_percent_change) << ",\n";
            std::cout << "  \"significance_threshold_percent\": " << result.significance_threshold_percent << ",\n";
            std::cout << "  \"file_count_delta\": " << result.file_count_delta << ",\n";
            std::cout << "  \"is_regression\": " << (result.is_regression() ? "true" : "false") << ",\n";
            std::cout << "  \"is_significant\": " << (result.is_significant() ? "true" : "false") << ",\n";
            std::cout << "  \"overall_gate_failed\": " << (overall_regression_failed ? "true" : "false") << ",\n";
            std::cout << "  \"category_gate_failed\": " << (any_category_gate_failed ? "true" : "false") << ",\n";
            std::cout << "  \"exit_failure\": " << ((overall_regression_failed || any_category_gate_failed) ? "true" : "false") << ",\n";
            std::cout << "  \"regressions_count\": " << result.regressions.size() << ",\n";
            std::cout << "  \"improvements_count\": " << result.improvements.size() << ",\n";
            std::cout << "  \"new_files_count\": " << result.new_files.size() << ",\n";
            std::cout << "  \"removed_files_count\": " << result.removed_files.size() << ",\n";
            const auto& distribution = result.translation_unit_regressions;
            std::cout << "  \"translation_unit_regressions\": {\n";
            std::cout << "    \"matched_files\": " << distribution.matched_files << ",\n";
            std::cout << "    \"regressed_files\": " << distribution.regressed_files << ",\n";
            std::cout << "    \"total_delta_ms\": "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(distribution.total_delta).count() << ",\n";
            std::cout << "    \"min_delta_ms\": "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(distribution.min_delta).count() << ",\n";
            std::cout << "    \"median_delta_ms\": "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(distribution.median_delta).count() << ",\n";
            std::cout << "    \"p90_delta_ms\": "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(distribution.p90_delta).count() << ",\n";
            std::cout << "    \"p99_delta_ms\": "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(distribution.p99_delta).count() << ",\n";
            std::cout << "    \"max_delta_ms\": "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(distribution.max_delta).count() << "\n";
            std::cout << "  },\n";

            std::cout << "  \"categories\": {\n";
            std::cout << "    \"translation_unit\": {\n";
            std::cout << "      \"delta_ms\": " << std::chrono::duration_cast<std::chrono::milliseconds>(result.translation_unit.delta).count() << ",\n";
            std::cout << "      \"percent_change\": "
                      << format_optional_json_number(result.translation_unit.percent_change) << ",\n";
            std::cout << "      \"gate_percent\": " << (gate_tu ? std::to_string(*gate_tu) : "null") << ",\n";
            std::cout << "      \"gate_failed\": " << (tu_gate_failed ? "true" : "false") << "\n";
            std::cout << "    },\n";
            std::cout << "    \"headers\": {\n";
            std::cout << "      \"delta_ms\": " << std::chrono::duration_cast<std::chrono::milliseconds>(result.headers.delta).count() << ",\n";
            std::cout << "      \"percent_change\": "
                      << format_optional_json_number(result.headers.percent_change) << ",\n";
            std::cout << "      \"gate_percent\": " << (gate_header ? std::to_string(*gate_header) : "null") << ",\n";
            std::cout << "      \"gate_failed\": " << (header_gate_failed ? "true" : "false") << "\n";
            std::cout << "    },\n";
            std::cout << "    \"templates\": {\n";
            std::cout << "      \"delta_ms\": " << std::chrono::duration_cast<std::chrono::milliseconds>(result.templates.delta).count() << ",\n";
            std::cout << "      \"percent_change\": "
                      << format_optional_json_number(result.templates.percent_change) << ",\n";
            std::cout << "      \"gate_percent\": " << (gate_template ? std::to_string(*gate_template) : "null") << ",\n";
            std::cout << "      \"gate_failed\": " << (template_gate_failed ? "true" : "false") << "\n";
            std::cout << "    }\n";
            std::cout << "  }\n";
            std::cout << "}\n";
        }
    };

    namespace {
        struct CompareCommandRegistrar {
            CompareCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<CompareCommand>()
                );
            }
        } compare_registrar;
    }
}  // namespace bha::cli
