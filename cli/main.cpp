//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/bha.hpp"
#include "bha/cli/commands/command.hpp"
#include "bha/cli/formatter.hpp"

#include "bha/parsers/all_parsers.hpp"
#include "bha/analyzers/all_analyzers.hpp"
#include "bha/suggestions/all_suggesters.hpp"
#include "bha/build_systems/adapter.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>

namespace
{
    void print_version() {
        std::cout << bha::PROJECT_NAME << " v" << bha::VERSION_STRING << "\n";
        std::cout << "Build Hotspot Analyzer - Find and fix slow C++ builds\n";
    }

    void print_usage() {
        using namespace bha::cli;

        std::cout << "\n";
        if (colors::enabled()) {
            std::cout << colors::BOLD << "bha" << colors::RESET << " - Build Hotspot Analyzer\n";
        } else {
            std::cout << "bha - Build Hotspot Analyzer\n";
        }
        std::cout << "\n";
        std::cout << "A cross-platform C++ build time analyzer with actionable optimization\n";
        std::cout << "suggestions. Supports GCC, Clang, MSVC, Intel, and NVCC compilers.\n";
        std::cout << "\n";
        std::cout << "Usage: bha <command> [options]\n";
        std::cout << "\n";

        if (colors::enabled()) {
            std::cout << colors::BOLD << "Commands:" << colors::RESET << "\n";
        } else {
            std::cout << "Commands:\n";
        }

        const auto& registry = CommandRegistry::instance();
        auto commands = registry.list();

        std::ranges::sort(commands,
                          [](const Command* a, const Command* b) {
                              return a->name() < b->name();
                          });

        for (const auto* cmd : commands) {
            std::cout << "  " << std::left << std::setw(12) << cmd->name();
            std::cout << cmd->description() << "\n";
        }

        std::cout << "\n";
        std::cout << "  " << std::left << std::setw(12) << "version";
        std::cout << "Show version information\n";
        std::cout << "  " << std::left << std::setw(12) << "help";
        std::cout << "Show this help message\n";

        std::cout << "\n";
        if (colors::enabled()) {
            std::cout << colors::BOLD << "Global Options:" << colors::RESET << "\n";
        } else {
            std::cout << "Global Options:\n";
        }
        std::cout << "  -h, --help      Show help for a command\n";
        std::cout << "  -v, --verbose   Enable verbose output\n";
        std::cout << "  -q, --quiet     Only show errors\n";
        std::cout << "  --json          Output in JSON format\n";
        std::cout << "  --no-color      Disable colored output\n";

        std::cout << "\n";
        std::cout << "Use 'bha <command> --help' for more information about a command.\n";
        std::cout << "\n";
        if (colors::enabled()) {
            std::cout << colors::BOLD << "Examples:" << colors::RESET << "\n";
        } else {
            std::cout << "Examples:\n";
        }
        std::cout << "  bha analyze build/*.json              Analyze Clang time-trace files\n";
        std::cout << "  bha suggest --detailed traces/        Get suggestions with code examples\n";
        std::cout << "  bha suggest --pch-min-includes 5      Custom PCH detection threshold\n";
        std::cout << "  bha export --format html -o report    Generate interactive HTML report\n";
        std::cout << "  bha snapshot save baseline            Save analysis for comparison\n";
        std::cout << "  bha compare baseline current          Compare two snapshots\n";

        std::cout << "\n";
        if (colors::enabled()) {
            std::cout << colors::BOLD << "Key Features:" << colors::RESET << "\n";
        } else {
            std::cout << "Key Features:\n";
        }
        std::cout << "  - Multi-compiler: GCC, Clang, MSVC, Intel ICC, NVIDIA NVCC\n";
        std::cout << "  - Actionable suggestions with before/after code examples\n";
        std::cout << "  - Configurable heuristics (--pch-*, --template-*, --unity-*, etc.)\n";
        std::cout << "  - HTML reports with flame graphs, include trees, dependency graphs\n";
        std::cout << "  - Snapshot comparison to track build time improvements\n";
        std::cout << "  - CMake integration for automatic trace capture\n";
        std::cout << "\n";
    }
}  // namespace

int main(const int argc, char* argv[]) {
    using namespace bha::cli;

    bha::parsers::register_all_parsers();
    bha::analyzers::register_all_analyzers();
    bha::suggestions::register_all_suggesters();
    bha::build_systems::register_all_adapters();

    std::vector<std::string> args(argv + 1, argv + argc);

    for (const auto& arg : args) {
        if (arg == "--no-color") {
            colors::set_enabled(false);
        }
    }

    if (args.empty()) {
        print_usage();
        return 0;
    }

    const std::string& command_name = args[0];

    if (command_name == "version" || command_name == "--version" || command_name == "-v") {
        print_version();
        return 0;
    }

    if (command_name == "help" || command_name == "--help" || command_name == "-h") {
        if (args.size() > 1 && args[1] != "--help" && args[1][0] != '-') {
            if (const auto* cmd = CommandRegistry::instance().find(args[1])) {
                cmd->print_help();
                return 0;
            }
            std::cerr << "Unknown command: " << args[1] << "\n";
            return 1;
        }
        print_usage();
        return 0;
    }

    auto* cmd = CommandRegistry::instance().find(command_name);
    if (!cmd) {
        std::cerr << "Unknown command: " << command_name << "\n";
        std::cerr << "Run 'bha help' for usage information.\n";
        return 1;
    }

    const std::vector cmd_args(args.begin() + 1, args.end());
    auto parse_result = parse_arguments(cmd_args, cmd->arguments());

    if (!parse_result.success) {
        std::cerr << "Error: " << parse_result.error << "\n";
        std::cerr << "Run 'bha " << command_name << " --help' for usage.\n";
        return 1;
    }

    if (parse_result.args.get_flag("help")) {
        cmd->print_help();
        return 0;
    }

    if (const auto validation_error = cmd->validate(parse_result.args); !validation_error.empty()) {
        std::cerr << "Error: " << validation_error << "\n";
        std::cerr << "Run 'bha " << command_name << " --help' for usage.\n";
        return 1;
    }

    try {
        return cmd->execute(parse_result.args);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}