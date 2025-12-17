//
// Created by gregorian on 15/12/2025.
//

#include "cli_parser.hpp"
#include "bha/version.h"
#include <iostream>
#include <algorithm>

namespace bha::cli {

    Command CliParser::parse_command(const std::string& cmd) {
        if (cmd == "init") return Command::INIT;
        if (cmd == "build") return Command::BUILD;
        if (cmd == "analyze") return Command::ANALYZE;
        if (cmd == "compare") return Command::COMPARE;
        if (cmd == "export") return Command::EXPORT;
        if (cmd == "dashboard") return Command::DASHBOARD;
        if (cmd == "list" || cmd == "ls") return Command::LIST;
        if (cmd == "trends" || cmd == "trend") return Command::TRENDS;
        if (cmd == "history") return Command::HISTORY;
        if (cmd == "clean") return Command::CLEAN;
        if (cmd == "ci-check") return Command::CI_CHECK;
        if (cmd == "ci-report") return Command::CI_REPORT;
        if (cmd == "ci-badge") return Command::CI_BADGE;
        if (cmd == "watch") return Command::WATCH;
        if (cmd == "blame") return Command::BLAME;
        if (cmd == "budget") return Command::BUDGET;
        if (cmd == "optimize") return Command::OPTIMIZE;
        if (cmd == "targets") return Command::TARGETS;
        if (cmd == "diff") return Command::DIFF;
        if (cmd == "profile") return Command::PROFILE;
        if (cmd == "help" || cmd == "--help" || cmd == "-h") return Command::HELP;
        if (cmd == "version" || cmd == "--version" || cmd == "-v") return Command::VERSION;
        return Command::UNKNOWN;
    }

    Options CliParser::parse(const int argc, char** argv) {
        if (argc < 2) {
            return Options{.command = Command::HELP};
        }

        const std::string cmd_str = argv[1];
        const Command cmd = parse_command(cmd_str);

        if (cmd == Command::HELP || cmd == Command::VERSION) {
            return Options{.command = cmd};
        }

        if (cmd == Command::UNKNOWN) {
            std::cerr << "Unknown command: " << cmd_str << "\n";
            return Options{.command = Command::HELP};
        }

        // Check for command-specific help
        if (argc >= 3 && (std::string(argv[2]) == "--help" || std::string(argv[2]) == "-h")) {
            print_command_help(cmd);
            std::exit(0);
        }

        int index = 2;
        switch (cmd) {
            case Command::INIT:
                return parse_init_options(argc, argv, index);
            case Command::BUILD:
                return parse_build_options(argc, argv, index);
            case Command::ANALYZE:
                return parse_analyze_options(argc, argv, index);
            case Command::COMPARE:
                return parse_compare_options(argc, argv, index);
            case Command::EXPORT:
                return parse_export_options(argc, argv, index);
            case Command::DASHBOARD:
                return parse_dashboard_options(argc, argv, index);
            case Command::LIST:
                return parse_list_options(argc, argv, index);
            case Command::TRENDS:
                return parse_trends_options(argc, argv, index);
            case Command::HISTORY:
                return parse_history_options(argc, argv, index);
            case Command::CLEAN:
                return parse_clean_options(argc, argv, index);
            case Command::CI_CHECK:
                return parse_ci_check_options(argc, argv, index);
            case Command::CI_REPORT:
                return parse_ci_report_options(argc, argv, index);
            case Command::CI_BADGE:
                return parse_ci_badge_options(argc, argv, index);
            case Command::WATCH:
                return parse_watch_options(argc, argv, index);
            case Command::BLAME:
                return parse_blame_options(argc, argv, index);
            case Command::BUDGET:
                return parse_budget_options(argc, argv, index);
            case Command::OPTIMIZE:
                return parse_optimize_options(argc, argv, index);
            case Command::TARGETS:
                return parse_targets_options(argc, argv, index);
            case Command::DIFF:
                return parse_diff_options(argc, argv, index);
            case Command::PROFILE:
                return parse_profile_options(argc, argv, index);
            default:
                return Options{.command = Command::HELP};
        }
    }

    Options CliParser::parse_init_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::INIT;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--dir" || arg == "-d") {
                if (index < argc) opts.project_dir = argv[index++];
            } else if (arg == "--force" || arg == "-f") {
                opts.force = true;
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg[0] != '-') {
                opts.project_dir = arg;
            }
        }

        return opts;
    }

    Options CliParser::parse_build_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::BUILD;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--target" || arg == "-t") {
                if (index < argc) opts.build_target = argv[index++];
            } else if (arg == "--clean") {
                opts.clean_build = true;
            } else if (arg == "--compiler") {
                if (index < argc) opts.compiler_type = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--no-auto-detect") {
                opts.auto_detect = false;
            } else if (arg[0] != '-') {
                opts.project_dir = arg;
            }
        }

        return opts;
    }

    Options CliParser::parse_analyze_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::ANALYZE;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--output" || arg == "-o") {
                if (index < argc) opts.output_file = argv[index++];
            } else if (arg == "--format" || arg == "-f") {
                if (index < argc) opts.format = argv[index++];
            } else if (arg == "--top-n" || arg == "-n") {
                if (index < argc) opts.top_n = std::stoi(argv[index++]);
            } else if (arg == "--threshold" || arg == "-t") {
                if (index < argc) opts.threshold_ms = std::stod(argv[index++]);
            } else if (arg == "--min-confidence") {
                if (index < argc) opts.min_confidence = std::stod(argv[index++]);
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--anonymize") {
                opts.anonymize = true;
            } else if (arg == "--no-suggestions") {
                opts.no_suggestions = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            } else if (arg == "--compiler") {
                if (index < argc) opts.compiler_type = argv[index++];
            } else if (arg == "--build-system") {
                if (index < argc) opts.build_system = argv[index++];
            } else if (arg[0] != '-') {
                opts.input_files.push_back(arg);
            }
        }

        return opts;
    }

    Options CliParser::parse_compare_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::COMPARE;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--baseline" || arg == "-b") {
                if (index < argc) opts.baseline = argv[index++];
            } else if (arg == "--output" || arg == "-o") {
                if (index < argc) opts.output_file = argv[index++];
            } else if (arg == "--format" || arg == "-f") {
                if (index < argc) opts.format = argv[index++];
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            } else if (arg[0] != '-') {
                opts.input_files.push_back(arg);
            }
        }

        return opts;
    }

    Options CliParser::parse_export_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::EXPORT;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--output" || arg == "-o") {
                if (index < argc) opts.output_file = argv[index++];
            } else if (arg == "--format" || arg == "-f") {
                if (index < argc) opts.format = argv[index++];
            } else if (arg == "--anonymize") {
                opts.anonymize = true;
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg[0] != '-') {
                opts.input_files.push_back(arg);
            }
        }

        return opts;
    }

    Options CliParser::parse_dashboard_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::DASHBOARD;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--port" || arg == "-p") {
                if (index < argc) opts.port = std::stoi(argv[index++]);
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg[0] != '-') {
                opts.input_files.push_back(arg);
            }
        }

        return opts;
    }

    Options CliParser::parse_list_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::LIST;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--limit" || arg == "-n") {
                if (index < argc) opts.top_n = std::stoi(argv[index++]);
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            }
        }

        return opts;
    }

    Options CliParser::parse_trends_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::TRENDS;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--days") {
                if (index < argc) opts.days = std::stoi(argv[index++]);
            } else if (arg == "--limit" || arg == "-n") {
                if (index < argc) opts.top_n = std::stoi(argv[index++]);
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            }
        }

        return opts;
    }

    Options CliParser::parse_history_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::HISTORY;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--days") {
                if (index < argc) opts.days = std::stoi(argv[index++]);
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--format" || arg == "-f") {
                if (index < argc) opts.format = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            }
        }

        return opts;
    }

    Options CliParser::parse_clean_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::CLEAN;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--days") {
                if (index < argc) opts.days = std::stoi(argv[index++]);
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            }
        }

        return opts;
    }

    Options CliParser::parse_ci_check_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::CI_CHECK;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--threshold" || arg == "-t") {
                if (index < argc) opts.ci_threshold_percent = std::stod(argv[index++]);
            } else if (arg == "--baseline" || arg == "-b") {
                if (index < argc) opts.baseline = argv[index++];
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            } else if (arg[0] != '-') {
                opts.input_files.push_back(arg);
            }
        }

        return opts;
    }

    Options CliParser::parse_ci_report_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::CI_REPORT;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--format" || arg == "-f") {
                if (index < argc) opts.ci_format = argv[index++];
            } else if (arg == "--output" || arg == "-o") {
                if (index < argc) opts.output_file = argv[index++];
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg[0] != '-') {
                opts.input_files.push_back(arg);
            }
        }

        return opts;
    }

    Options CliParser::parse_ci_badge_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::CI_BADGE;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--output" || arg == "-o") {
                if (index < argc) opts.badge_output = argv[index++];
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            }
        }

        return opts;
    }

    Options CliParser::parse_watch_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::WATCH;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--alert-threshold") {
                if (index < argc) opts.alert_threshold_percent = std::stod(argv[index++]);
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg[0] != '-') {
                opts.project_dir = arg;
            }
        }

        return opts;
    }

    Options CliParser::parse_blame_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::BLAME;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--since") {
                if (index < argc) opts.git_ref = argv[index++];
            } else if (arg == "--author") {
                if (index < argc) opts.author = argv[index++];
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            }
        }

        return opts;
    }

    Options CliParser::parse_budget_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::BUDGET;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "set" || arg == "check" || arg == "list") {
                opts.budget_action = arg;
            } else if (arg == "--total") {
                if (index < argc) opts.budget_total_ms = std::stod(argv[index++]);
            } else if (arg == "--file" || arg == "-f") {
                if (index < argc) opts.budget_file = argv[index++];
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            } else if (arg[0] != '-') {
                opts.input_files.push_back(arg);
            }
        }

        return opts;
    }

    Options CliParser::parse_optimize_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::OPTIMIZE;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--apply") {
                opts.apply_optimizations = true;
            } else if (arg == "--pch") {
                opts.format = "pch";
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            } else if (arg[0] != '-') {
                opts.input_files.push_back(arg);
            }
        }

        return opts;
    }

    Options CliParser::parse_targets_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::TARGETS;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--critical-path") {
                opts.show_critical_path = true;
            } else if (arg == "--suggest-split") {
                opts.format = "split";
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            }
        }

        return opts;
    }

    Options CliParser::parse_diff_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::DIFF;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--files") {
                opts.format = "files";
            } else if (arg == "--baseline" || arg == "-b") {
                if (index < argc) opts.baseline = argv[index++];
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            } else if (arg[0] != '-') {
                opts.git_ref = arg;
            }
        }

        return opts;
    }

    Options CliParser::parse_profile_options(const int argc, char** argv, int& index) {
        Options opts;
        opts.command = Command::PROFILE;

        while (index < argc) {
            if (std::string arg = argv[index++]; arg == "--include-graph") {
                opts.include_graph = true;
            } else if (arg == "--templates") {
                opts.analyze_templates = true;
            } else if (arg == "--database" || arg == "-d") {
                if (index < argc) opts.database_path = argv[index++];
            } else if (arg == "--output" || arg == "-o") {
                if (index < argc) opts.output_file = argv[index++];
            } else if (arg == "--verbose") {
                opts.verbose = true;
            } else if (arg == "--json") {
                opts.json_output = true;
            } else if (arg[0] != '-') {
                opts.input_files.push_back(arg);
            }
        }

        return opts;
    }

    void CliParser::print_help() {
        std::cout << R"(
Build Hotspot Analyzer (BHA) - Intelligent C++ Build Performance Analysis

USAGE:
    bha <COMMAND> [OPTIONS]

COMMANDS:
    init           Initialize BHA in your project (auto-detects build system)
    build          Build your project with trace instrumentation
    analyze        Analyze build traces (auto-finds if no file specified)
    compare        Compare two build traces
    export         Export analysis results to various formats
    dashboard      Start interactive web dashboard
    list           List recent builds with metrics
    trends         Show build performance trends over time
    history        View build history from database
    clean          Clean old build data from database
    ci-check       Check build regression for CI/CD (fails if threshold exceeded)
    ci-report      Generate CI-friendly reports (GitHub Actions, GitLab)
    ci-badge       Create build time badge (SVG)
    watch          Watch for builds and analyze automatically
    blame          Show performance attribution by git commits/authors
    budget         Manage build performance budgets
    optimize       Generate intelligent optimization suggestions
    targets        Analyze CMake targets and dependencies
    diff           Compare current build to baseline
    profile        Deep profiling with templates and includes
    help           Show this help message
    version        Show version information

INIT OPTIONS:
    [dir]                   Project directory (default: current directory)
    -d, --dir <path>        Explicit project directory
    -f, --force             Force reinitialization
    --verbose               Enable verbose output

BUILD OPTIONS:
    [dir]                   Project directory (default: current directory)
    -t, --target <name>     Specific build target
    --clean                 Perform clean build
    --compiler <type>       Compiler to use (clang|gcc|msvc)
    --no-auto-detect        Disable auto-detection
    --verbose               Enable verbose output

ANALYZE OPTIONS:
    [file]                  Input trace file (auto-finds if omitted)
    -o, --output <file>     Output file for results
    -f, --format <fmt>      Output format (json|html|csv|markdown|text)
    -n, --top-n <num>       Number of top hotspots to show (default: 20)
    -t, --threshold <ms>    Minimum time threshold in ms (default: 1000)
    --min-confidence <val>  Minimum suggestion confidence (default: 0.5)
    -d, --database <path>   Database path (default: ./bha.db)
    --compiler <type>       Compiler type hint (clang|gcc|msvc)
    --build-system <sys>    Build system hint (cmake|ninja|make|msbuild)
    --anonymize             Remove sensitive paths from output
    --no-suggestions        Skip generating optimization suggestions
    --json                  Output results in JSON format
    --verbose               Enable verbose output

COMPARE OPTIONS:
    <current-file>          Current build trace
    -b, --baseline <file>   Baseline trace to compare against
    -o, --output <file>     Output file for comparison report
    -f, --format <fmt>      Output format (json|html|markdown|text)
    -d, --database <path>   Database path
    --json                  Output results in JSON format
    --verbose               Enable verbose output

EXPORT OPTIONS:
    <file>                  Input trace file to export
    -o, --output <file>     Output file
    -f, --format <fmt>      Export format (json|html|csv|markdown|text)
    --anonymize             Anonymize sensitive data
    --verbose               Enable verbose output

DASHBOARD OPTIONS:
    [file]                  Optional trace file to load
    -p, --port <num>        Server port (default: 8080)
    -d, --database <path>   Database path for history
    --verbose               Enable verbose logging

LIST OPTIONS:
    -n, --limit <num>       Number of builds to show (default: 20)
    -d, --database <path>   Database path (default: ./bha.db)
    --json                  Output results in JSON format
    --verbose               Enable verbose output

TRENDS OPTIONS:
    --days <num>            Show trends for last N days (default: 30)
    -n, --limit <num>       Number of data points (default: 20)
    -d, --database <path>   Database path (default: ./bha.db)
    --json                  Output results in JSON format
    --verbose               Enable verbose output

HISTORY OPTIONS:
    --days <num>            Show builds from last N days (default: 30)
    -d, --database <path>   Database path
    -f, --format <fmt>      Output format (json|text)
    --json                  Output results in JSON format
    --verbose               Enable verbose output

CLEAN OPTIONS:
    --days <num>            Remove builds older than N days (default: 30)
    -d, --database <path>   Database path
    --verbose               Enable verbose output

CI-CHECK OPTIONS:
    [file]                  Trace file to check (auto-finds if omitted)
    -t, --threshold <pct>   Failure threshold percentage (default: 5%)
    -b, --baseline <file>   Baseline build to compare against
    -d, --database <path>   Database path
    --json                  Output results in JSON format
    --verbose               Enable verbose output

CI-REPORT OPTIONS:
    [file]                  Trace file to report
    -f, --format <fmt>      Report format (github|gitlab|jenkins|json)
    -o, --output <file>     Output file for report
    -d, --database <path>   Database path
    --verbose               Enable verbose output

CI-BADGE OPTIONS:
    -o, --output <file>     Output SVG file path
    -d, --database <path>   Database path
    --verbose               Enable verbose output

WATCH OPTIONS:
    [dir]                   Directory to watch (default: current directory)
    --alert-threshold <pct> Alert threshold percentage (default: 10%)
    -d, --database <path>   Database path
    --verbose               Enable verbose output

BLAME OPTIONS:
    --since <ref>           Show changes since git ref (commit/tag/branch)
    --author <name>         Filter by author name
    -d, --database <path>   Database path
    --json                  Output results in JSON format
    --verbose               Enable verbose output

BUDGET OPTIONS:
    set                     Set performance budgets
    check                   Check against budgets
    list                    List current budgets
    --total <ms>            Total build time budget in ms
    -f, --file <path>       Per-file budget specification
    -d, --database <path>   Database path
    --json                  Output results in JSON format
    --verbose               Enable verbose output

OPTIMIZE OPTIONS:
    [file]                  Trace file to analyze
    --apply                 Apply safe optimizations automatically
    --pch                   Suggest precompiled header candidates
    -d, --database <path>   Database path
    --json                  Output results in JSON format
    --verbose               Enable verbose output

TARGETS OPTIONS:
    --critical-path         Show critical path by target
    --suggest-split         Suggest target splitting opportunities
    -d, --database <path>   Database path
    --json                  Output results in JSON format
    --verbose               Enable verbose output

DIFF OPTIONS:
    [ref]                   Git ref to compare against (default: last build)
    --files                 Show per-file differences
    -b, --baseline <file>   Baseline build
    -d, --database <path>   Database path
    --json                  Output results in JSON format
    --verbose               Enable verbose output

PROFILE OPTIONS:
    [file]                  Trace file to profile
    --include-graph         Visualize include dependency graph
    --templates             Analyze template instantiation hotspots
    -o, --output <file>     Output file for profile report
    -d, --database <path>   Database path
    --json                  Output results in JSON format
    --verbose               Enable verbose output

EXAMPLES:
    # Initialize BHA in your project (auto-detects CMake/Ninja/Make/etc)
    bha init

    # Build with automatic trace instrumentation
    bha build

    # Analyze automatically (finds latest traces, detects compiler)
    bha analyze

    # Analyze specific file
    bha analyze build_trace.json

    # Compare two builds
    bha compare current.json --baseline previous.json

    # Export to HTML with anonymized paths
    bha export trace.json --format html --anonymize -o report.html

    # Start interactive dashboard
    bha dashboard

    # List recent builds with metrics
    bha list --limit 10

    # Show build performance trends
    bha trends --days 14

    # View recent build history
    bha history --days 7 --json

    # Clean old data
    bha clean --days 90

    # CI/CD - Check for regressions (fails if >5% slower)
    bha ci-check --threshold 5

    # Generate GitHub Actions report
    bha ci-report --format github

    # Create build time badge
    bha ci-badge --output badge.svg

    # Watch builds continuously
    bha watch --alert-threshold 10

    # Find commits that slowed builds
    bha blame --since v1.0.0

    # Show performance by author
    bha blame --author john

    # Set build time budget
    bha budget set --total 300000

    # Check budget compliance
    bha budget check

    # Get optimization suggestions
    bha optimize

    # Apply safe optimizations
    bha optimize --apply

    # Analyze targets
    bha targets --critical-path

    # Compare to last build
    bha diff

    # Compare to specific commit
    bha diff HEAD~5

    # Deep profile with templates
    bha profile --templates

INTELLIGENT FEATURES:
    - Auto-detects build system (CMake, Ninja, Make, MSBuild, Bazel, Xcode)
    - Auto-finds trace files in build directory
    - Auto-instruments builds with compiler time-trace flags
    - Auto-detects compiler from build configuration
    - Works from any directory in your project
)";
    }

    void CliParser::print_version() {
        std::cout << bha::VERSION_STRING << "\n";
        std::cout << "Copyright (c) 2025\n";
    }

    void CliParser::print_command_help(Command cmd) {
        switch (cmd) {
            case Command::INIT:
                std::cout << R"(bha init - Initialize BHA in your project

USAGE:
    bha init [OPTIONS] [dir]

OPTIONS:
    [dir]                   Project directory (default: current directory)
    -d, --dir <path>        Explicit project directory
    -f, --force             Force reinitialization
    --verbose               Enable verbose output

DESCRIPTION:
    Auto-detects your build system (CMake, Ninja, Make, MSBuild, Bazel, Xcode)
    and creates .bha-config.toml with project settings.

EXAMPLES:
    bha init                    # Initialize in current directory
    bha init /path/to/project   # Initialize in specific directory
    bha init --force            # Force reinitialize
)";
                break;

            case Command::ANALYZE:
                std::cout << R"(bha analyze - Analyze build traces

USAGE:
    bha analyze [OPTIONS] [file]

OPTIONS:
    [file]                  Input trace file (auto-finds if omitted)
    -o, --output <file>     Output file for results
    -f, --format <fmt>      Output format (json|html|csv|markdown|text)
    -n, --top-n <num>       Number of top hotspots to show (default: 20)
    -t, --threshold <ms>    Minimum time threshold in ms (default: 1000)
    --min-confidence <val>  Minimum suggestion confidence (default: 0.5)
    --anonymize             Remove sensitive paths from output
    --no-suggestions        Skip generating optimization suggestions
    --json                  Output results in JSON format
    --verbose               Enable verbose output

DESCRIPTION:
    Analyzes build traces to identify compilation hotspots, slow files,
    frequently included headers, and provides optimization suggestions.

EXAMPLES:
    bha analyze                             # Auto-find and analyze latest trace
    bha analyze build_trace.json            # Analyze specific file
    bha analyze --json                      # Output as JSON
    bha analyze --top-n 50 --threshold 500  # Show top 50, min 500ms
)";
                break;

            case Command::CI_CHECK:
                std::cout << R"(bha ci-check - Check build regression for CI/CD

USAGE:
    bha ci-check [OPTIONS] [file]

OPTIONS:
    [file]                  Trace file to check (auto-finds if omitted)
    -t, --threshold <pct>   Failure threshold percentage (default: 5%)
    -b, --baseline <file>   Baseline build to compare against
    --json                  Output results in JSON format
    --verbose               Enable verbose output

DESCRIPTION:
    Compares current build against baseline and fails if build time
    exceeds threshold. Perfect for CI/CD pipelines to prevent regressions.

EXIT CODES:
    0 - Build passes (within threshold)
    1 - Build fails (exceeds threshold)

EXAMPLES:
    bha ci-check                    # Check with 5% threshold
    bha ci-check --threshold 10     # Allow 10% slowdown
    bha ci-check --json             # JSON output for parsing
)";
                break;

            case Command::BUDGET:
                std::cout << R"(bha budget - Manage build performance budgets

USAGE:
    bha budget <action> [OPTIONS]

ACTIONS:
    set     Set performance budgets
    check   Check against budgets
    list    List current budgets

OPTIONS:
    --total <ms>            Total build time budget in milliseconds
    -f, --file <path>       Per-file budget specification
    --json                  Output results in JSON format
    --verbose               Enable verbose output

DESCRIPTION:
    Set and enforce build time budgets to maintain performance standards.

EXAMPLES:
    bha budget set --total 300000   # Set 5 minute budget
    bha budget check                # Check compliance
    bha budget list                 # Show current budgets
)";
                break;

            case Command::OPTIMIZE:
                std::cout << R"(bha optimize - Generate intelligent optimization suggestions

USAGE:
    bha optimize [OPTIONS] [file]

OPTIONS:
    [file]                  Trace file to analyze
    --apply                 Apply safe optimizations automatically
    --pch                   Focus on precompiled header suggestions
    --json                  Output results in JSON format
    --verbose               Enable verbose output

DESCRIPTION:
    Analyzes build patterns and suggests optimizations including:
    - Precompiled headers (PCH)
    - Unity/Jumbo builds
    - Include optimization
    - Template optimization

EXAMPLES:
    bha optimize            # Get all suggestions
    bha optimize --pch      # Focus on PCH opportunities
    bha optimize --apply    # Apply safe optimizations (future)
)";
                break;

            default:
                std::cout << "Help for '" << static_cast<int>(cmd) << "' not yet implemented.\n";
                std::cout << "Run 'bha help' for general usage information.\n";
                break;
        }
    }

} // namespace bha::cli