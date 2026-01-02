//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"

#include "bha/bha.hpp"
#include "bha/storage.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>

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

    }  // namespace

    /**
     * Compare command - compares two build snapshots.
     */
    class CompareCommand : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "compare";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Compare two build snapshots to identify regressions and improvements";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha compare <old-snapshot> <new-snapshot> [OPTIONS]\n"
                   "       bha compare --baseline <new-snapshot> [OPTIONS]\n"
                   "\n"
                   "Compare two snapshots to identify build time changes, regressions,\n"
                   "and improvements between builds.\n"
                   "\n"
                   "Examples:\n"
                   "  bha compare v1.0 v2.0\n"
                   "  bha compare before-refactor after-refactor\n"
                   "  bha compare --baseline current-build\n"
                   "  bha compare v1.0 v2.0 --top 20";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"baseline", 'b', "Compare against baseline", false, false, "", ""},
                {"top", 't', "Number of top changes to show", false, true, "10", "N"},
                {"threshold", 0, "Significance threshold (%)", false, true, "5", "PERCENT"},
                {"storage", 0, "Storage directory", false, true, ".bha/snapshots", "DIR"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.get_flag("baseline")) {
                if (args.positional().empty()) {
                    return "Usage: bha compare --baseline <snapshot>";
                }
            } else {
                if (args.positional().size() < 2) {
                    return "Usage: bha compare <old-snapshot> <new-snapshot>";
                }
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

            std::string old_name;
            std::string new_name;

            if (args.get_flag("baseline")) {
                auto baseline = store.get_baseline();
                if (!baseline) {
                    print_error("No baseline set. Use 'bha baseline set <snapshot>' first.");
                    return 1;
                }
                old_name = *baseline;
                new_name = args.positional()[0];
            } else {
                old_name = args.positional()[0];
                new_name = args.positional()[1];
            }

            std::size_t top_count = static_cast<std::size_t>(args.get_int("top").value_or(10));

            auto result = store.compare(old_name, new_name);
            if (result.is_err()) {
                print_error("Comparison failed: " + result.error().message());
                return 1;
            }

            const auto& comparison = result.value();

            if (is_json()) {
                print_comparison_json(comparison, old_name, new_name);
            } else {
                print_comparison(comparison, old_name, new_name, top_count);
            }

            return comparison.is_regression() ? 1 : 0;
        }

    private:
        static void print_comparison(
            const storage::ComparisonResult& result,
            const std::string& old_name,
            const std::string& new_name,
            std::size_t top_count
        ) {
            std::cout << bold("Build Comparison: ") << old_name << " -> " << new_name << "\n\n";

            // Overall summary
            std::cout << bold("Summary") << "\n";
            std::cout << "  Build Time: " << format_delta(result.build_time_delta) << " ("
                      << format_percent_change(result.build_time_percent_change) << ")\n";
            std::cout << "  File Count: " << (result.file_count_delta >= 0 ? "+" : "")
                      << result.file_count_delta << "\n";

            // Status
            std::cout << "\n";
            if (result.is_regression() && result.is_significant()) {
                std::cout << red("! REGRESSION DETECTED") << "\n";
                std::cout << "  Build time increased by "
                          << std::fixed << std::setprecision(1) << result.build_time_percent_change << "%\n";
            } else if (result.is_improvement() && result.is_significant()) {
                std::cout << green("+ BUILD TIME IMPROVED") << "\n";
                std::cout << "  Build time decreased by "
                          << std::fixed << std::setprecision(1) << -result.build_time_percent_change << "%\n";
            } else {
                std::cout << dim("= No significant change") << "\n";
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
                    pct << std::fixed << std::setprecision(1) << "+" << percent_change << "%";

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

                    std::ostringstream pct;
                    pct << std::fixed << std::setprecision(1) << percent_change << "%";

                    table.add_row({
                        format_path(file, 40),
                        format_dur(old_time),
                        format_dur(new_time),
                        format_dur(delta),
                        pct.str()
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
                for (const auto& [name, old_count, new_count, old_time, new_time] : result.template_regressions) {
                    if (count >= 5) break;
                    std::cout << "  " << name
                              << " - count: " << old_count << " -> " << new_count
                              << ", time: " << format_dur(old_time) << " -> " << format_dur(new_time) << "\n";
                    count++;
                }
            }
        }

        static void print_comparison_json(
            const storage::ComparisonResult& result,
            const std::string& old_name,
            const std::string& new_name
        ) {
            std::cout << "{\n";
            std::cout << "  \"old_snapshot\": \"" << old_name << "\",\n";
            std::cout << "  \"new_snapshot\": \"" << new_name << "\",\n";
            std::cout << "  \"build_time_delta_ms\": "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(result.build_time_delta).count() << ",\n";
            std::cout << "  \"build_time_percent_change\": " << result.build_time_percent_change << ",\n";
            std::cout << "  \"file_count_delta\": " << result.file_count_delta << ",\n";
            std::cout << "  \"is_regression\": " << (result.is_regression() ? "true" : "false") << ",\n";
            std::cout << "  \"is_significant\": " << (result.is_significant() ? "true" : "false") << ",\n";
            std::cout << "  \"regressions_count\": " << result.regressions.size() << ",\n";
            std::cout << "  \"improvements_count\": " << result.improvements.size() << ",\n";
            std::cout << "  \"new_files_count\": " << result.new_files.size() << ",\n";
            std::cout << "  \"removed_files_count\": " << result.removed_files.size() << "\n";
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