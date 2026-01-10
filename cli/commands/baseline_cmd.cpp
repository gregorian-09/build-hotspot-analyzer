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

namespace bha::cli
{
    /**
     * Baseline command - manages the baseline snapshot for comparison.
     */
    class BaselineCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "baseline";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Manage the baseline snapshot for comparisons";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha baseline <subcommand> [OPTIONS]\n"
                   "\n"
                   "Subcommands:\n"
                   "  set <snapshot>   Set a snapshot as the baseline\n"
                   "  show             Show current baseline details\n"
                   "  clear            Remove the baseline\n"
                   "\n"
                   "The baseline is used as the default 'old' snapshot when running\n"
                   "'bha compare --baseline <new-snapshot>'.\n"
                   "\n"
                   "Examples:\n"
                   "  bha baseline set v1.0\n"
                   "  bha baseline show\n"
                   "  bha baseline clear";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"storage", 0, "Storage directory", false, true, ".bha/snapshots", "DIR"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.positional().empty()) {
                return "No subcommand specified. Use 'bha baseline set|show|clear'";
            }

            const std::string& subcommand = args.positional()[0];
            if (subcommand != "set" && subcommand != "show" && subcommand != "clear") {
                return "Unknown subcommand: " + subcommand;
            }

            if (subcommand == "set" && args.positional().size() < 2) {
                return "Usage: bha baseline set <snapshot>";
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

            if (const std::string& subcommand = args.positional()[0]; subcommand == "set") {
                const std::string name = args.positional()[1];
                return set_baseline(store, name);
            } else
            {
                if (subcommand == "show") {
                    return show_baseline(store);
                }
                if (subcommand == "clear") {
                    return clear_baseline(store);
                }
            }

            return 1;
        }

    private:
        int set_baseline(const storage::SnapshotStore& store, const std::string& name) const
        {
            if (!store.exists(name)) {
                print_error("Snapshot not found: " + name);
                print("Use 'bha snapshot list' to see available snapshots.");
                return 1;
            }

            if (auto result = store.set_baseline(name); result.is_err()) {
                print_error("Failed to set baseline: " + result.error().message());
                return 1;
            }

            print("Baseline set to: " + name);
            print("\nUse 'bha compare --baseline <new-snapshot>' to compare against this baseline.");

            return 0;
        }

        int show_baseline(const storage::SnapshotStore& store) const
        {
            const auto baseline = store.get_baseline();

            if (!baseline) {
                if (is_json()) {
                    std::cout << "{\"baseline\": null}\n";
                } else {
                    print("No baseline set.");
                    print("Use 'bha baseline set <snapshot>' to set one.");
                }
                return 0;
            }

            auto result = store.load(*baseline);
            if (result.is_err()) {
                print_error("Failed to load baseline: " + result.error().message());
                return 1;
            }

            const auto& snapshot = result.value();

            if (is_json()) {
                std::cout << "{\n";
                std::cout << "  \"baseline\": \"" << *baseline << "\",\n";
                std::cout << "  \"created_at\": \"" << format_time(snapshot.metadata.created_at) << "\",\n";
                std::cout << "  \"git_commit\": \"" << snapshot.metadata.git_commit << "\",\n";
                std::cout << "  \"git_branch\": \"" << snapshot.metadata.git_branch << "\",\n";
                std::cout << "  \"file_count\": " << snapshot.metadata.file_count << ",\n";
                std::cout << "  \"total_build_time_ms\": "
                          << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 snapshot.metadata.total_build_time).count() << "\n";
                std::cout << "}\n";
            } else {
                if (colors::enabled()) {
                    std::cout << colors::BOLD << "Current Baseline: " << colors::RESET << *baseline << "\n\n";
                } else {
                    std::cout << "Current Baseline: " << *baseline << "\n\n";
                }

                std::cout << "Created:    " << format_time(snapshot.metadata.created_at) << "\n";
                if (!snapshot.metadata.description.empty()) {
                    std::cout << "Description: " << snapshot.metadata.description << "\n";
                }
                if (!snapshot.metadata.git_branch.empty()) {
                    std::cout << "Git Branch: " << snapshot.metadata.git_branch << "\n";
                }
                if (!snapshot.metadata.git_commit.empty()) {
                    std::cout << "Git Commit: " << snapshot.metadata.git_commit << "\n";
                }

                std::cout << "\nBuild Summary:\n";
                std::cout << "  Files:      " << snapshot.metadata.file_count << "\n";
                std::cout << "  Build Time: " << format_duration(snapshot.metadata.total_build_time) << "\n";

                std::cout << "\nCompare with: bha compare --baseline <new-snapshot>\n";
            }

            return 0;
        }

        int clear_baseline(const storage::SnapshotStore& store) const
        {
            const auto baseline = store.get_baseline();

            if (!baseline) {
                print("No baseline is currently set.");
                return 0;
            }

            if (auto result = store.clear_baseline(); result.is_err()) {
                print_error("Failed to clear baseline: " + result.error().message());
                return 1;
            }

            print("Baseline cleared (was: " + *baseline + ")");

            return 0;
        }

        static std::string format_time(const Timestamp ts) {
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

        static std::string format_duration(const Duration d) {
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
    };

    // Register the command
    namespace {
        struct BaselineCommandRegistrar {
            BaselineCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<BaselineCommand>()
                );
            }
        } baseline_registrar;
    }
}  // namespace bha::cli