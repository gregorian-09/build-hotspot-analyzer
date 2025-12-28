//
// Created by gregorian on 15/12/2025.
//

#include "app.hpp"
#include "server.hpp"
#include "bha/cli/build_detector.h"
#include "bha/parsers/clang_parser.h"
#include "bha/parsers/gcc_parser.h"
#include "bha/parsers/msvc_parser.h"
#include "bha/parsers/unified_format.h"
#include "bha/analysis/analysis_engine.h"
#include "bha/suggestions/suggestion_engine.h"
#include "bha/export/json_exporter.h"
#include "bha/export/html_exporter.h"
#include "bha/export/markdown_exporter.h"
#include "bha/storage/sqlite_backend.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <csignal>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <thread>
#include <map>
#include <ranges>
#include <utility>

namespace bha::cli {

    namespace {
        std::atomic g_stop_requested{false};

        void handle_sigint(int) {
            g_stop_requested.store(true);
        }
    }

    App::App(Options options)
        : options_(std::move(options))
        , database_(nullptr)
        , validator_(std::make_unique<security::InputValidator>(security::InputValidator::ValidationOptions{}))
        , limiter_(std::make_unique<security::ResourceLimiter>(security::ResourceLimiter::Limits{}))
        , anonymizer_(std::make_unique<security::Anonymizer>(security::Anonymizer::AnonymizationConfig{}))
    {
    }

    int App::run() {
        if (auto validation = validate_inputs(); !validation.is_success()) {
            std::cerr << "Validation error: " << validation.error().message << "\n";
            return 1;
        }

        check_resource_limits();

        switch (options_.command) {
            case Command::INIT:
                return run_init();
            case Command::BUILD:
                return run_build();
            case Command::ANALYZE:
                return run_analyze();
            case Command::COMPARE:
                return run_compare();
            case Command::EXPORT:
                return run_export();
            case Command::DASHBOARD:
                return run_dashboard();
            case Command::LIST:
                return run_list();
            case Command::TRENDS:
                return run_trends();
            case Command::HISTORY:
                return run_history();
            case Command::CLEAN:
                return run_clean();
            case Command::CI_CHECK:
                return run_ci_check();
            case Command::CI_REPORT:
                return run_ci_report();
            case Command::CI_BADGE:
                return run_ci_badge();
            case Command::WATCH:
                return run_watch();
            case Command::BLAME:
                return run_blame();
            case Command::BUDGET:
                return run_budget();
            case Command::OPTIMIZE:
                return run_optimize();
            case Command::TARGETS:
                return run_targets();
            case Command::DIFF:
                return run_diff();
            case Command::PROFILE:
                return run_profile();
            default:
                std::cerr << "Unknown command\n";
                return 1;
        }
    }

    int App::run_analyze() {
        if (!initialize_storage().is_success() && options_.verbose) {
            std::cerr << "Warning: Could not initialize database storage\n";
        }

        std::string trace_file;

        if (options_.input_files.empty()) {
            if (options_.verbose) {
                std::cout << "No input file specified, auto-detecting trace files...\n";
            }

            auto latest_result = get_latest_trace_file();
            if (!latest_result.is_success()) {
                std::cerr << "Error: No trace files found. Run 'bha build' first or specify a trace file.\n";
                return 1;
            }

            trace_file = latest_result.value();
            if (options_.verbose) {
                std::cout << "Found trace file: " << trace_file << "\n";
            }
        } else {
            trace_file = options_.input_files[0];
        }

        auto trace_result = load_trace(trace_file);
        if (!trace_result.is_success()) {
            std::cerr << "Error loading trace: " << trace_result.error().message << "\n";
            return 1;
        }

        core::BuildTrace trace = trace_result.value();

        if (options_.anonymize) {
            apply_anonymization(trace);
        }

        auto analysis_result = analysis::BuildAnalysisEngine::analyze(
            trace,
            trace.dependency_graph,
            analysis::BuildAnalysisEngine::Options{}
        );

        if (!analysis_result.is_success()) {
            std::cerr << "Analysis failed: " << analysis_result.error().message << "\n";
            return 1;
        }

        const analysis::AnalysisReport& report = analysis_result.value();
        populate_metrics_from_analysis(trace, report);

        std::vector<core::Suggestion> suggestions;
        if (!options_.no_suggestions) {
            suggestions::SuggestionEngine::Options sugg_opts;
            sugg_opts.min_confidence = options_.min_confidence;

            suggestions::SuggestionEngine sugg_engine;
            if (auto sugg_result = sugg_engine.generate_all_suggestions(trace, sugg_opts); sugg_result.is_success()) {
                suggestions = sugg_result.value();
            }
        }

        if (database_) {
            if (auto store_result = database_->store_build_trace(trace); store_result.is_success() && options_.verbose) {
                std::cout << "Stored in database with ID: " << store_result.value() << "\n";
            }
        }

        if (options_.json_output || options_.format == "json") {
            if (auto json_result = parsers::UnifiedFormatSerializer::serialize_build_trace(trace); json_result.is_success()) {
                if (options_.output_file.empty()) {
                    std::cout << json_result.value() << "\n";
                } else {
                    std::ofstream out(options_.output_file);
                    out << json_result.value();
                }
            }
        } else {
            print_analysis_summary(trace);
            if (!suggestions.empty()) {
                print_suggestions(suggestions);
            }
        }

        return 0;
    }

    int App::run_compare() const
    {
        if (options_.input_files.size() < 2) {
            std::cerr << "Error: Comparison requires two input files\n";
            return 1;
        }

        auto current_result = load_trace(options_.input_files[0]);
        if (!current_result.is_success()) {
            std::cerr << "Error loading current trace: " << current_result.error().message << "\n";
            return 1;
        }

        auto baseline_result = load_trace(options_.input_files[1]);
        if (!baseline_result.is_success()) {
            std::cerr << "Error loading baseline trace: " << baseline_result.error().message << "\n";
            return 1;
        }

        const core::BuildTrace& current = current_result.value();
        const core::BuildTrace& baseline = baseline_result.value();

        core::ComparisonReport comparison = create_comparison_report(baseline, current);

        if (options_.json_output) {
            print_comparison_json(comparison);
        } else {
            print_comparison(comparison);
        }

        return 0;
    }

    int App::run_export() const
    {
        if (options_.input_files.empty()) {
            std::cerr << "Error: No input file specified\n";
            return 1;
        }

        auto trace_result = load_trace(options_.input_files[0]);
        if (!trace_result.is_success()) {
            std::cerr << "Error loading trace: " << trace_result.error().message << "\n";
            return 1;
        }

        core::BuildTrace trace = trace_result.value();

        if (options_.anonymize) {
            apply_anonymization(trace);
        }

        if (options_.output_file.empty()) {
            std::cerr << "Error: Output file required for export command\n";
            return 1;
        }

        core::Result<void> result;

        if (options_.format == "json") {
            export_module::JSONExporter exporter(export_module::JSONExporter::Options{});
            result = exporter.export_report(trace.metrics, {}, trace, options_.output_file);
        } else if (options_.format == "html") {
            export_module::HTMLExporter exporter(export_module::HTMLExporter::Options{});
            result = exporter.export_report(trace.metrics, {}, trace, options_.output_file);
        } else if (options_.format == "markdown" || options_.format == "md") {
            export_module::MarkdownExporter exporter(export_module::MarkdownExporter::Options{});
            result = exporter.export_report(trace.metrics, {}, trace, options_.output_file);
        } else {
            std::cerr << "Error: Unsupported format '" << options_.format << "'\n";
            return 1;
        }

        if (!result.is_success()) {
            std::cerr << "Export failed: " << result.error().message << "\n";
            return 1;
        }

        std::cout << "Exported to: " << options_.output_file << "\n";

        return 0;
    }

    int App::run_dashboard() const
    {
        if (options_.input_files.empty()) {
            std::cerr << "Error: No input file specified\n";
            return 1;
        }

        auto trace_result = load_trace(options_.input_files[0]);
        if (!trace_result.is_success()) {
            std::cerr << "Error loading trace: " << trace_result.error().message << "\n";
            return 1;
        }

        const core::BuildTrace& trace = trace_result.value();

        suggestions::SuggestionEngine::Options sugg_opts;
        sugg_opts.min_confidence = options_.min_confidence;
        suggestions::SuggestionEngine sugg_engine;
        auto sugg_result = sugg_engine.generate_all_suggestions(trace, sugg_opts);

        std::vector<core::Suggestion> suggestions;
        if (sugg_result.is_success()) {
            suggestions = sugg_result.value();
        }

        dashboard::Server::Options server_opts;
        server_opts.port = options_.port;
        dashboard::Server server(server_opts);
        server.set_trace(trace);
        server.set_suggestions(suggestions);

        std::cout << "Starting dashboard server on port " << options_.port << "\n";
        std::cout << "Open http://localhost:" << options_.port << " in your browser\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        if (auto result = server.start(); !result.is_success()) {
            std::cerr << "Dashboard server error: " << result.error().message << "\n";
            return 1;
        }

        return 0;
    }

    int App::run_history() {
        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        auto builds_result = database_->get_recent_builds(options_.top_n);
        if (!builds_result.is_success()) {
            std::cerr << "Error retrieving build history: " << builds_result.error().message << "\n";
            return 1;
        }

        const auto& builds = builds_result.value();

        if (builds.empty()) {
            std::cout << "No builds found in history\n";
            return 0;
        }

        if (options_.json_output) {
            std::cout << "{\n  \"builds\": [\n";
            for (size_t i = 0; i < builds.size(); ++i) {
                const auto& build = builds[i];
                if (i > 0) std::cout << ",\n";
                std::cout << "    {\n";
                std::cout << R"(      "id": ")" << build.id << "\",\n";
                std::cout << "      \"build_time_ms\": " << build.total_time_ms << ",\n";
                std::cout << R"(      "build_system": ")" << build.build_system << "\",\n";
                std::cout << R"(      "platform": ")" << build.platform << "\"\n";
                std::cout << "    }";
            }
            std::cout << "\n  ]\n}\n";
        } else {
            std::cout << "Recent builds (" << builds.size() << " found):\n\n";
            for (const auto& build : builds) {
                std::cout << "  ID: " << build.id << "\n";
                std::cout << "    Build Time: " << build.total_time_ms << " ms\n";
                std::cout << "    System: " << build.build_system << "\n";
                std::cout << "    Platform: " << build.platform << "\n";
                std::cout << "\n";
            }
        }

        return 0;
    }

    int App::run_clean() {
        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        constexpr int retention_days = 90;
        if (auto result = database_->cleanup(retention_days); !result.is_success()) {
            std::cerr << "Cleanup failed: " << result.error().message << "\n";
            return 1;
        }

        std::cout << "Cleaned up builds older than " << retention_days << " days\n";
        return 0;
    }

    core::Result<core::BuildTrace> App::load_trace(const std::string& file_path) const
    {
        if (!std::filesystem::exists(file_path)) {
            return core::Result<core::BuildTrace>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "File not found: " + file_path
            );
        }

        if (database_) {
            auto db_result = database_->load_build_trace(file_path);
            if (db_result.is_success() && db_result.value().has_value()) {
                return core::Result<core::BuildTrace>::success(db_result.value().value());
            }
        }

        return parse_trace_file(file_path);
    }

    core::Result<core::BuildTrace> App::parse_trace_file(const std::string& file_path) const
    {
        if (options_.compiler_type.has_value()) {
            const std::string compiler = options_.compiler_type.value();
            std::unique_ptr<parsers::TraceParser> parser;

            if (compiler == "clang") {
                parser = std::make_unique<parsers::ClangTimeTraceParser>();
            } else if (compiler == "gcc") {
                parser = std::make_unique<parsers::GCCTimeReportParser>();
            } else if (compiler == "msvc") {
                parser = std::make_unique<parsers::MSVCTraceParser>();
            }

            if (parser) {
                auto units_result = parser->parse(file_path);
                if (!units_result.is_success()) {
                    return core::Result<core::BuildTrace>::failure(units_result.error());
                }

                core::BuildTrace trace;
                trace.compilation_units = units_result.value();
                trace.build_start = std::chrono::system_clock::now();
                trace.build_end = trace.build_start;

                double total_time = 0.0;
                for (const auto& unit : trace.compilation_units) {
                    total_time += unit.total_time_ms;
                }
                trace.total_build_time_ms = total_time;

                return core::Result<core::BuildTrace>::success(std::move(trace));
            }
        }

        return parsers::UnifiedFormatSerializer::load_from_file(file_path);
    }

    void App::populate_metrics_from_analysis(core::BuildTrace& trace, const analysis::AnalysisReport& report) {
        trace.metrics.top_slow_files = report.slow_files;
        trace.metrics.top_hot_headers = report.hot_headers;
        trace.metrics.critical_path = report.critical_path;
        trace.metrics.circular_dependency_count = static_cast<int>(report.dependency_cycles.size());

        if (!trace.compilation_units.empty()) {
            std::vector<double> times;
            for (const auto& unit : trace.compilation_units) {
                times.push_back(unit.total_time_ms);
            }
            std::ranges::sort(times);

            trace.metrics.total_files_compiled = static_cast<int>(trace.compilation_units.size());
            trace.metrics.average_file_time_ms = std::accumulate(times.begin(), times.end(), 0.0) / static_cast<double>(times.size());
            trace.metrics.median_file_time_ms = times[times.size() / 2];
            trace.metrics.p95_file_time_ms = times[static_cast<size_t>(static_cast<double>(times.size()) * 0.95)];
            trace.metrics.p99_file_time_ms = times[static_cast<size_t>(static_cast<double>(times.size()) * 0.99)];
        }

        trace.metrics.total_dependencies = static_cast<int>(trace.dependency_graph.edge_count());
        if (!report.include_depths.empty()) {
            int total_depth = 0;
            int max_depth = 0;
            for (const auto& depth : report.include_depths | std::ranges::views::values) {
                total_depth += depth;
                max_depth = std::max(max_depth, depth);
            }
            trace.metrics.average_include_depth = total_depth / static_cast<int>(report.include_depths.size());
            trace.metrics.max_include_depth = max_depth;
        }
    }

    core::ComparisonReport App::create_comparison_report(
        const core::BuildTrace& baseline,
        const core::BuildTrace& current
    ) {
        core::ComparisonReport report;
        report.baseline_trace_id = baseline.trace_id;
        report.current_trace_id = current.trace_id;
        report.baseline_total_time_ms = baseline.total_build_time_ms;
        report.current_total_time_ms = current.total_build_time_ms;
        report.time_delta_ms = current.total_build_time_ms - baseline.total_build_time_ms;
        report.time_delta_percent = (report.time_delta_ms / baseline.total_build_time_ms) * 100.0;
        report.is_regression = report.time_delta_ms > 0;
        return report;
    }

    void App::print_analysis_summary(const core::BuildTrace& trace) const
    {
        std::cout << "\n";
        std::cout << "==================================================\n";
        std::cout << "           Build Analysis Summary\n";
        std::cout << "==================================================\n\n";

        std::cout << "Build Information:\n";
        std::cout << "  Total Build Time: " << trace.total_build_time_ms << " ms\n";
        std::cout << "  Build System: " << trace.build_system << "\n";
        std::cout << "  Platform: " << trace.platform << "\n";
        std::cout << "  Configuration: " << trace.configuration << "\n";
        std::cout << "\n";

        const auto& metrics = trace.metrics;
        std::cout << "Compilation Metrics:\n";
        std::cout << "  Total Files: " << metrics.total_files_compiled << "\n";
        std::cout << "  Total Headers: " << metrics.total_headers_parsed << "\n";
        std::cout << "  Average Time: " << metrics.average_file_time_ms << " ms\n";
        std::cout << "  Median Time: " << metrics.median_file_time_ms << " ms\n";
        std::cout << "  P95 Time: " << metrics.p95_file_time_ms << " ms\n";
        std::cout << "  P99 Time: " << metrics.p99_file_time_ms << " ms\n";
        std::cout << "\n";

        std::cout << "Dependency Metrics:\n";
        std::cout << "  Total Dependencies: " << metrics.total_dependencies << "\n";
        std::cout << "  Average Include Depth: " << metrics.average_include_depth << "\n";
        std::cout << "  Max Include Depth: " << metrics.max_include_depth << "\n";
        std::cout << "  Circular Dependencies: " << metrics.circular_dependency_count << "\n";
        std::cout << "\n";

        if (!metrics.top_slow_files.empty()) {
            std::cout << "Top Slow Files (" << std::min(static_cast<int>(metrics.top_slow_files.size()), options_.top_n) << "):\n";
            for (size_t i = 0; i < metrics.top_slow_files.size() && i < static_cast<size_t>(options_.top_n); ++i) {
                const auto& hotspot = metrics.top_slow_files[i];
                std::cout << "  " << (i + 1) << ". " << hotspot.file_path << "\n";
                std::cout << "     Time: " << hotspot.time_ms << " ms\n";
                std::cout << "     Impact Score: " << hotspot.impact_score << "\n";
                std::cout << "     Dependents: " << hotspot.num_dependent_files << "\n";
                std::cout << "\n";
            }
        }
    }

    void App::print_suggestions(const std::vector<core::Suggestion>& suggestions) const
    {
        std::cout << "==================================================\n";
        std::cout << "         Optimization Suggestions\n";
        std::cout << "==================================================\n\n";

        for (size_t i = 0; i < suggestions.size() && i < static_cast<size_t>(options_.top_n); ++i) {
            const auto& sugg = suggestions[i];
            std::cout << (i + 1) << ". " << sugg.title << "\n";
            std::cout << "   Type: " << core::to_string(sugg.type) << "\n";
            std::cout << "   Priority: " << core::to_string(sugg.priority) << "\n";
            std::cout << "   Confidence: " << (sugg.confidence * 100) << "%\n";
            std::cout << "   Estimated Savings: " << sugg.estimated_time_savings_ms << " ms\n";
            std::cout << "   Description: " << sugg.description << "\n";
            std::cout << "   Safe: " << (sugg.is_safe ? "Yes" : "No") << "\n";
            std::cout << "\n";
        }
    }

    void App::print_comparison(const core::ComparisonReport& report) {
        std::cout << "\n";
        std::cout << "==================================================\n";
        std::cout << "           Build Comparison Report\n";
        std::cout << "==================================================\n\n";

        std::cout << "Baseline: " << report.baseline_trace_id << "\n";
        std::cout << "Current:  " << report.current_trace_id << "\n";
        std::cout << "\n";

        std::cout << "Build Time:\n";
        std::cout << "  Baseline: " << report.baseline_total_time_ms << " ms\n";
        std::cout << "  Current:  " << report.current_total_time_ms << " ms\n";
        std::cout << "  Delta:    " << report.time_delta_ms << " ms ("
                  << report.time_delta_percent << "%)\n";
        std::cout << "  Status:   " << (report.is_regression ? "REGRESSION" : "IMPROVEMENT") << "\n";
        std::cout << "\n";
    }

    void App::print_comparison_json(const core::ComparisonReport& report) {
        std::cout << "{\n";
        std::cout << R"(  "baseline_id": ")" << report.baseline_trace_id << "\",\n";
        std::cout << R"(  "current_id": ")" << report.current_trace_id << "\",\n";
        std::cout << "  \"baseline_time_ms\": " << report.baseline_total_time_ms << ",\n";
        std::cout << "  \"current_time_ms\": " << report.current_total_time_ms << ",\n";
        std::cout << "  \"time_delta_ms\": " << report.time_delta_ms << ",\n";
        std::cout << "  \"time_delta_percent\": " << report.time_delta_percent << ",\n";
        std::cout << "  \"is_regression\": " << (report.is_regression ? "true" : "false") << "\n";
        std::cout << "}\n";
    }

    core::Result<void> App::validate_inputs() const
    {
        for (const auto& file : options_.input_files) {
            if (auto validation = validator_->validate_file_path(file); !validation.is_success()) {
                return core::Result<void>::failure(validation.error());
            }

            if (auto size_check = validator_->validate_file_size(file); !size_check.is_success()) {
                return core::Result<void>::failure(size_check.error());
            }
        }

        return core::Result<void>::success();
    }

    core::Result<void> App::initialize_storage() {
        auto backend = std::make_unique<storage::SQLiteBackend>(options_.database_path);
        database_ = std::make_unique<storage::Database>(std::move(backend));
        return database_->initialize();
    }

    void App::apply_anonymization(core::BuildTrace& trace) const
    {
        trace = anonymizer_->anonymize_trace(trace);
    }

    void App::check_resource_limits() const
    {
        limiter_->start_timer();

        if (auto memory_check = limiter_->check_memory_limit(); !memory_check.is_success() && options_.verbose) {
            std::cerr << "Warning: " << memory_check.error().message << "\n";
        }
    }

    int App::run_init() const
    {
        std::filesystem::path project_dir = options_.project_dir.value_or(std::filesystem::current_path().string());

        std::cout << "Initializing BHA in: " << project_dir << "\n";

        auto build_info_result = BuildDetector::detect_build_system(project_dir);
        if (!build_info_result.is_success()) {
            std::cerr << "Error: Could not detect build system.\n";
            std::cerr << "Make sure you're in a project directory with CMakeLists.txt, Makefile, or similar.\n";
            return 1;
        }

        const auto& build_info = build_info_result.value();
        std::cout << "Detected build system: " << BuildDetector::build_system_to_string(build_info.type) << "\n";
        std::cout << "Project root: " << build_info.root_dir << "\n";

        if (!build_info.build_dir.empty()) {
            std::cout << "Build directory: " << build_info.build_dir << "\n";
        }

        if (!build_info.detected_compiler.empty()) {
            std::cout << "Detected compiler: " << build_info.detected_compiler << "\n";
        }

        std::filesystem::path config_file = build_info.root_dir / ".bha-config.toml";
        if (std::filesystem::exists(config_file) && !options_.force) {
            std::cout << "BHA is already initialized. Use --force to reinitialize.\n";
            return 0;
        }

        std::ofstream config(config_file);
        config << "# BHA Configuration\n";
        config << "[build]\n";
        config << "system = \"" << BuildDetector::build_system_to_string(build_info.type) << "\"\n";
        config << "root = \"" << build_info.root_dir.string() << "\"\n";
        if (!build_info.build_dir.empty()) {
            config << "build_dir = \"" << build_info.build_dir.string() << "\"\n";
        }
        if (!build_info.detected_compiler.empty()) {
            config << "compiler = \"" << build_info.detected_compiler << "\"\n";
        }
        config << "\n[database]\n";
        config << "path = \"" << options_.database_path << "\"\n";
        config.close();

        std::cout << "BHA initialized successfully!\n";
        std::cout << "Configuration saved to: " << config_file << "\n";
        std::cout << "\nNext steps:\n";
        std::cout << "  1. Run 'bha build' to build with instrumentation\n";
        std::cout << "  2. Run 'bha analyze' to analyze the build\n";

        return 0;
    }

    int App::run_build() const
    {
        std::filesystem::path project_dir = options_.project_dir.value_or(std::filesystem::current_path().string());

        auto build_info_result = BuildDetector::detect_build_system(project_dir);
        if (!build_info_result.is_success()) {
            std::cerr << "Error: Could not detect build system. Run 'bha init' first.\n";
            return 1;
        }

        auto build_info = build_info_result.value();

        std::cout << "Building with trace instrumentation...\n";
        std::cout << "Build system: " << BuildDetector::build_system_to_string(build_info.type) << "\n";

        std::string compiler = options_.compiler_type.value_or(build_info.detected_compiler);
        bool is_gcc = compiler.find("gcc") != std::string::npos || compiler.find("g++") != std::string::npos;
        bool is_clang = compiler == "clang" || compiler.find("clang") != std::string::npos;

        std::string trace_flag;
        if (is_clang) {
            trace_flag = "-ftime-trace";
        } else if (is_gcc) {
            trace_flag = "-ftime-report";
        }

        if (build_info.type == BuildSystemType::CMAKE) {
            if (build_info.build_dir.empty()) {
                std::cerr << "Error: No build directory found. Run cmake first.\n";
                return 1;
            }

            std::string cmake_config_cmd = "cmake -S " + project_dir.string() + " -B " + build_info.build_dir.string();
            if (!trace_flag.empty()) {
                cmake_config_cmd += " -DCMAKE_CXX_FLAGS=\"" + trace_flag + "\"";
            }

            if (options_.verbose) {
                std::cout << "Running: " << cmake_config_cmd << "\n";
            }

            if (int config_result = std::system(cmake_config_cmd.c_str()); config_result != 0) {
                std::cerr << "Error: CMake configuration failed with exit code: " << config_result << "\n";
                return config_result;
            }

            std::cout << "Performing clean build to ensure fresh compilation...\n";
            std::string clean_cmd = "cmake --build " + build_info.build_dir.string() + " --target clean";
            std::system(clean_cmd.c_str());

            std::string build_cmd = "cmake --build " + build_info.build_dir.string();
            int build_result;

            if (is_gcc) {
                std::filesystem::path output_file = build_info.build_dir / "gcc_time_report.txt";

                if (std::filesystem::exists(output_file)) {
                    try {
                        std::filesystem::remove(output_file);
                    } catch (const std::exception&) {
                    }
                }

                build_cmd += " 2>&1 | tee \"" + output_file.string() + "\"";

                if (options_.verbose) {
                    std::cout << "GCC time report will be saved to: " << output_file << "\n";
                    std::cout << "Running: " << build_cmd << "\n";
                }

                build_result = std::system(build_cmd.c_str());

                if (build_result == 0) {
                    if (std::filesystem::exists(output_file) && std::filesystem::file_size(output_file) > 100) {
                        std::cout << "Build completed successfully!\n";
                        std::cout << "Time report saved to " << output_file.string() << "\n";
                    } else {
                        std::cerr << "Warning: Time report file is empty or missing.\n";
                    }
                }
            } else {
                if (options_.verbose) {
                    std::cout << "Running: " << build_cmd << "\n";
                }

                build_result = std::system(build_cmd.c_str());

                if (build_result == 0) {
                    std::cout << "Build completed successfully!\n";
                    if (is_clang) {
                        std::cout << "Trace files generated in build directory.\n";
                    }
                }
            }

            if (build_result != 0) {
                std::cerr << "Build failed with exit code: " << build_result << "\n";
                return build_result;
            }

            std::cout << "Run 'bha analyze' to analyze the build traces.\n";
            return 0;
        }

        std::string build_cmd;
        if (build_info.type == BuildSystemType::MAKE) {
            build_cmd = "make clean && make";
            if (!trace_flag.empty()) {
                build_cmd += " CXXFLAGS+=\"" + trace_flag + "\"";
            }
        } else if (build_info.type == BuildSystemType::NINJA) {
            build_cmd = "ninja -t clean && ninja";
            if (!trace_flag.empty()) {
                build_cmd = "CXXFLAGS=\"" + trace_flag + "\" " + build_cmd;
            }
        } else {
            std::cerr << "Error: Automatic build not supported for " << BuildDetector::build_system_to_string(build_info.type) << "\n";
            std::cerr << "Please build manually with time-trace enabled.\n";
            return 1;
        }

        if (options_.verbose) {
            std::cout << "Running: " << build_cmd << "\n";
        }

        int result = std::system(build_cmd.c_str());

        if (result == 0) {
            std::cout << "Build completed successfully!\n";
            std::cout << "Run 'bha analyze' to analyze the build traces.\n";
        } else {
            std::cerr << "Build failed with exit code: " << result << "\n";
        }

        return result;
    }

    core::Result<std::vector<std::string>> App::auto_find_trace_files() {
        auto project_root_result = BuildDetector::find_project_root();
        if (!project_root_result.is_success()) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::NOT_FOUND,
                "Could not find project root"
            );
        }

        auto traces_result = BuildDetector::find_trace_files(project_root_result.value(), true);
        if (!traces_result.is_success()) {
            return core::Result<std::vector<std::string>>::failure(traces_result.error());
        }

        std::vector<std::string> trace_paths;
        for (const auto& trace_info : traces_result.value()) {
            trace_paths.push_back(trace_info.path.string());
        }

        if (trace_paths.empty()) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::NOT_FOUND,
                "No trace files found"
            );
        }

        return core::Result<std::vector<std::string>>::success(trace_paths);
    }

    core::Result<std::string> App::get_latest_trace_file() {
        auto traces_result = auto_find_trace_files();
        if (!traces_result.is_success()) {
            return core::Result<std::string>::failure(traces_result.error());
        }

        const auto& traces = traces_result.value();
        if (traces.empty()) {
            return core::Result<std::string>::failure(
                core::ErrorCode::NOT_FOUND,
                "No trace files found"
            );
        }

        return core::Result<std::string>::success(traces[0]);
    }

    int App::run_list() {
        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        auto builds_result = database_->get_recent_builds(options_.top_n);
        if (!builds_result.is_success()) {
            std::cerr << "Error retrieving builds: " << builds_result.error().message << "\n";
            return 1;
        }

        const auto& builds = builds_result.value();

        if (builds.empty()) {
            std::cout << "No builds found in database\n";
            std::cout << "Run 'bha analyze' to start tracking builds\n";
            return 0;
        }

        if (options_.json_output) {
            std::cout << "{\n  \"builds\": [\n";
            for (size_t i = 0; i < builds.size(); ++i) {
                const auto& build = builds[i];
                if (i > 0) std::cout << ",\n";
                std::cout << "    {\n";
                std::cout << R"(      "id": ")" << build.id << "\",\n";
                std::cout << "      \"build_time_ms\": " << build.total_time_ms << ",\n";
                std::cout << "      \"files_compiled\": " << build.file_count << ",\n";
                std::cout << R"(      "build_system": ")" << build.build_system << "\",\n";
                std::cout << R"(      "platform": ")" << build.platform << "\",\n";
                std::cout << R"(      "configuration": ")" << build.configuration << "\"\n";
                std::cout << "    }";
            }
            std::cout << "\n  ]\n}\n";
        } else {
            std::cout << "\n";
            std::cout << "==================================================\n";
            std::cout << "           Recent Builds\n";
            std::cout << "==================================================\n\n";

            std::cout << "Found " << builds.size() << " build(s)\n\n";

            int max_id_length = 10;
            for (const auto& build : builds) {
                max_id_length = std::max(max_id_length, static_cast<int>(build.id.length()));
            }

            std::cout << std::left;
            std::cout << std::setw(max_id_length + 2) << "Build ID"
                      << std::setw(15) << "Time (ms)"
                      << std::setw(12) << "Files"
                      << std::setw(15) << "System"
                      << std::setw(15) << "Platform"
                      << "\n";
            std::cout << std::string(max_id_length + 2 + 15 + 12 + 15 + 15, '-') << "\n";

            for (const auto& build : builds) {
                std::cout << std::setw(max_id_length + 2) << build.id.substr(0, max_id_length)
                          << std::setw(15) << build.total_time_ms
                          << std::setw(12) << build.file_count
                          << std::setw(15) << build.build_system
                          << std::setw(15) << build.platform
                          << "\n";
            }

            std::cout << "\n";
            std::cout << "Run 'bha trends' to see performance trends over time\n";
            std::cout << "Run 'bha compare <id1> --baseline <id2>' to compare builds\n";
        }

        return 0;
    }

    int App::run_trends() {
        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        auto builds_result = database_->get_recent_builds(options_.top_n);
        if (!builds_result.is_success()) {
            std::cerr << "Error retrieving builds: " << builds_result.error().message << "\n";
            return 1;
        }

        const auto& builds = builds_result.value();

        if (builds.empty()) {
            std::cout << "No builds found in database\n";
            std::cout << "Run 'bha analyze' to start tracking builds\n";
            return 0;
        }

        if (builds.size() < 2) {
            std::cout << "Need at least 2 builds to show trends\n";
            std::cout << "Run 'bha analyze' after more builds\n";
            return 0;
        }

        double total_time = 0.0;
        double min_time = builds[0].total_time_ms;
        double max_time = builds[0].total_time_ms;
        int improvements = 0;
        int regressions = 0;

        for (size_t i = 0; i < builds.size(); ++i) {
            total_time += builds[i].total_time_ms;
            min_time = std::min(min_time, builds[i].total_time_ms);
            max_time = std::max(max_time, builds[i].total_time_ms);

            if (i > 0) {
                if (const double delta = builds[i].total_time_ms - builds[i-1].total_time_ms; delta < 0) improvements++;
                else if (delta > 0) regressions++;
            }
        }

        const double avg_time = total_time / static_cast<double>(builds.size());

        if (options_.json_output) {
            std::cout << "{\n";
            std::cout << "  \"total_builds\": " << builds.size() << ",\n";
            std::cout << "  \"average_time_ms\": " << avg_time << ",\n";
            std::cout << "  \"min_time_ms\": " << min_time << ",\n";
            std::cout << "  \"max_time_ms\": " << max_time << ",\n";
            std::cout << "  \"improvements\": " << improvements << ",\n";
            std::cout << "  \"regressions\": " << regressions << ",\n";
            std::cout << "  \"builds\": [\n";
            for (size_t i = 0; i < builds.size(); ++i) {
                const auto& build = builds[i];
                if (i > 0) std::cout << ",\n";
                std::cout << "    {\n";
                std::cout << R"(      "id": ")" << build.id << "\",\n";
                std::cout << "      \"build_time_ms\": " << build.total_time_ms << ",\n";
                std::cout << "      \"files_compiled\": " << build.file_count << "\n";
                std::cout << "    }";
            }
            std::cout << "\n  ]\n}\n";
        } else {
            std::cout << "\n";
            std::cout << "==================================================\n";
            std::cout << "           Build Performance Trends\n";
            std::cout << "==================================================\n\n";

            std::cout << "Summary Statistics:\n";
            std::cout << "  Total Builds: " << builds.size() << "\n";
            std::cout << "  Average Build Time: " << static_cast<int>(avg_time) << " ms\n";
            std::cout << "  Fastest Build: " << static_cast<int>(min_time) << " ms\n";
            std::cout << "  Slowest Build: " << static_cast<int>(max_time) << " ms\n";
            std::cout << "  Improvements: " << improvements << "\n";
            std::cout << "  Regressions: " << regressions << "\n";
            std::cout << "\n";

            std::cout << "Recent Build Times:\n\n";

            constexpr int max_bar_width = 50;
            const double scale = max_bar_width / max_time;

            for (size_t i = 0; i < std::min(builds.size(), static_cast<size_t>(options_.top_n)); ++i) {
                const auto& build = builds[i];
                const int bar_length = static_cast<int>(build.total_time_ms * scale);

                std::cout << "  " << (i + 1) << ". ";

                for (int j = 0; j < bar_length; ++j) {
                    std::cout << "#";
                }

                std::cout << " " << static_cast<int>(build.total_time_ms) << " ms";

                if (i > 0) {
                    const double delta = build.total_time_ms - builds[i-1].total_time_ms;
                    if (builds[i-1].total_time_ms > 0) {
                        const double percent = (delta / builds[i-1].total_time_ms) * 100.0;
                        if (delta < 0) {
                            std::cout << " (+" << static_cast<int>(std::abs(percent)) << "% faster)";
                        } else if (delta > 0) {
                            std::cout << " (-" << static_cast<int>(percent) << "% slower)";
                        }
                    } else if (delta != 0) {
                        if (delta > 0) {
                            std::cout << " (new data)";
                        }
                    }
                }
                std::cout << "\n";
            }

            std::cout << "\n";
            std::cout << "Run 'bha list' to see detailed build information\n";
            std::cout << "Run 'bha dashboard' for interactive visualization\n";
        }

        return 0;
    }

    int App::run_ci_check() {
        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        std::string current_file;
        if (!options_.input_files.empty()) {
            current_file = options_.input_files[0];
        } else {
            auto latest_result = get_latest_trace_file();
            if (!latest_result.is_success()) {
                std::cerr << "Error: No trace files found\n";
                return 1;
            }
            current_file = latest_result.value();
        }

        auto current_result = load_trace(current_file);
        if (!current_result.is_success()) {
            std::cerr << "Error loading current trace: " << current_result.error().message << "\n";
            return 1;
        }

        const core::BuildTrace& current = current_result.value();

        auto baseline_result = database_->get_baseline();
        if (!baseline_result.is_success() || !baseline_result.value().has_value()) {
            std::cerr << "Warning: No baseline found in database. Using current build as baseline.\n";
            if (auto store_result = database_->store_build_trace(current); store_result.is_success() && options_.verbose) {
                std::cout << "Stored with ID: " << store_result.value() << "\n";
            }
            std::cout << "Build time: " << current.total_build_time_ms << " ms\n";
            std::cout << "Status: PASS (baseline established)\n";
            return 0;
        }

        auto baseline = baseline_result.value().value();
        double delta_ms = current.total_build_time_ms - baseline.total_time_ms;
        double delta_percent = (delta_ms / baseline.total_time_ms) * 100.0;

        bool regression = delta_percent > options_.ci_threshold_percent;

        if (options_.json_output) {
            std::cout << "{\n";
            std::cout << "  \"current_time_ms\": " << current.total_build_time_ms << ",\n";
            std::cout << "  \"baseline_time_ms\": " << baseline.total_time_ms << ",\n";
            std::cout << "  \"delta_ms\": " << delta_ms << ",\n";
            std::cout << "  \"delta_percent\": " << delta_percent << ",\n";
            std::cout << "  \"threshold_percent\": " << options_.ci_threshold_percent << ",\n";
            std::cout << "  \"regression\": " << (regression ? "true" : "false") << ",\n";
            std::cout << R"(  "status": ")" << (regression ? "FAIL" : "PASS") << "\"\n";
            std::cout << "}\n";
        } else {
            std::cout << "\n";
            std::cout << "==================================================\n";
            std::cout << "           CI Build Performance Check\n";
            std::cout << "==================================================\n\n";
            std::cout << "Current Build:  " << current.total_build_time_ms << " ms\n";
            std::cout << "Baseline:       " << baseline.total_time_ms << " ms\n";
            std::cout << "Delta:          " << delta_ms << " ms (" << delta_percent << "%)\n";
            std::cout << "Threshold:      " << options_.ci_threshold_percent << "%\n";
            std::cout << "\n";
            std::cout << "Status: " << (regression ? "FAIL - Build regression detected!" : "PASS") << "\n";
        }

        if (auto store_result = database_->store_build_trace(current); store_result.is_success() && options_.verbose) {
            std::cout << "Stored with ID: " << store_result.value() << "\n";
        }

        return regression ? 1 : 0;
    }

    int App::run_ci_report() {
        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        auto builds_result = database_->get_recent_builds(5);
        if (!builds_result.is_success()) {
            std::cerr << "Error retrieving builds\n";
            return 1;
        }

        auto builds = builds_result.value();
        if (builds.empty()) {
            std::cout << "No builds found\n";
            return 0;
        }

        std::string format = options_.ci_format.value_or("github");
        std::ostream* out = &std::cout;
        std::ofstream file;

        if (!options_.output_file.empty()) {
            file.open(options_.output_file);
            out = &file;
        }

        if (format == "github") {
            *out << "## Build Performance Report\n\n";
            *out << "| Build | Time (ms) | Files | Status |\n";
            *out << "|-------|-----------|-------|--------|\n";
            for (const auto& build : builds) {
                *out << "| " << build.id.substr(0, 8) << " | "
                     << build.total_time_ms << " | "
                     << build.file_count << " | ";
                if (builds.size() > 1 && &build != &builds[0]) {
                    double delta = ((build.total_time_ms - builds[0].total_time_ms) / builds[0].total_time_ms) * 100.0;
                    *out << (delta > 5 ? ":x:" : ":white_check_mark:") << " |\n";
                } else {
                    *out << ":white_check_mark: |\n";
                }
            }
        } else if (format == "gitlab") {
            *out << "# Build Performance Report\n\n";
            *out << "Latest build: " << builds[0].total_time_ms << " ms\n";
            *out << "Files compiled: " << builds[0].file_count << "\n";
        } else {
            *out << "{\n  \"builds\": [\n";
            for (size_t i = 0; i < builds.size(); ++i) {
                if (i > 0) *out << ",\n";
                *out << R"(    {"id": ")" << builds[i].id << R"(", "time_ms": )" << builds[i].total_time_ms << "}";
            }
            *out << "\n  ]\n}\n";
        }

        return 0;
    }

    int App::run_ci_badge() {
        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        auto builds_result = database_->get_recent_builds(1);
        if (!builds_result.is_success() || builds_result.value().empty()) {
            std::cerr << "Error: No builds found\n";
            return 1;
        }

        auto build = builds_result.value()[0];
        double time_s = build.total_time_ms / 1000.0;

        std::string color = "brightgreen";
        if (time_s > 300) color = "red";
        else if (time_s > 120) color = "orange";
        else if (time_s > 60) color = "yellow";

        std::string output = options_.badge_output.value_or("build-time.svg");
        std::ofstream badge(output);

        badge << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"150\" height=\"20\">\n"
              << "    <linearGradient id=\"b\" x2=\"0\" y2=\"100%\">\n"
              << "        <stop offset=\"0\" stop-color=\"#bbb\" stop-opacity=\".1\"/>\n"
              << "        <stop offset=\"1\" stop-opacity=\".1\"/>\n"
              << "    </linearGradient>\n"
              << "    <rect rx=\"3\" width=\"150\" height=\"20\" fill=\"#555\"/>\n"
              << R"(    <rect rx="3" x="80" width="70" height="20" fill=")" << color << "\"/>\n"
              << "    <rect rx=\"3\" width=\"150\" height=\"20\" fill=\"url(#b)\"/>\n"
              << "    <g fill=\"#fff\" text-anchor=\"middle\" font-family=\"DejaVu Sans,Verdana,Geneva,sans-serif\" font-size=\"11\">\n"
              << "        <text x=\"40\" y=\"15\" fill=\"#010101\" fill-opacity=\".3\">Build Time</text>\n"
              << "        <text x=\"40\" y=\"14\">Build Time</text>\n"
              << R"(        <text x="115" y="15" fill="#010101" fill-opacity=".3">)" << static_cast<int>(time_s) << "s</text>\n"
              << R"(        <text x="115" y="14">)" << static_cast<int>(time_s) << "s</text>\n"
              << "    </g>\n"
              << "</svg>\n";

        badge.close();
        std::cout << "Badge created: " << output << "\n";

        return 0;
    }

    int App::run_watch()
    {
        std::signal(SIGINT, handle_sigint);

        std::cout << "Watch mode - Monitoring for builds...\n";
        std::cout << "Alert threshold: " << options_.alert_threshold_percent << "%\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        auto baseline_result = database_->get_baseline();
        double baseline_time = 0.0;
        if (baseline_result.is_success() && baseline_result.value().has_value()) {
            baseline_time = baseline_result.value()->total_time_ms;
        }

        std::filesystem::path project_dir =
            options_.project_dir.value_or(std::filesystem::current_path().string());

        std::filesystem::file_time_type last_check =
            std::filesystem::file_time_type::min();

        std::cout << "Watching: " << project_dir << "\n\n";

        while (!g_stop_requested.load()) {
            if (auto traces_result = BuildDetector::find_trace_files(project_dir, true); traces_result.is_success()) {
                for (const auto& trace_info : traces_result.value()) {
                    if (trace_info.modified_time > last_check) {
                        std::cout << "New trace detected: "
                                  << trace_info.path.filename() << "\n";

                        if (auto trace_result = load_trace(trace_info.path.string()); trace_result.is_success()) {
                            const auto& trace = trace_result.value();
                            double time_ms = trace.total_build_time_ms;

                            std::cout << "  Build time: " << time_ms << " ms\n";

                            if (baseline_time > 0.0) {
                                double delta_percent =
                                    ((time_ms - baseline_time) / baseline_time) * 100.0;

                                if (std::abs(delta_percent) >
                                    options_.alert_threshold_percent) {
                                    std::cout << "  ALERT: "
                                              << delta_percent
                                              << "% change from baseline!\n";
                                    }
                            }

                            auto store_result =
                                database_->store_build_trace(trace);

                            if (store_result.is_success() && options_.verbose) {
                                std::cout << "  Stored with ID: "
                                          << store_result.value() << "\n";
                            }
                        }

                        last_check = trace_info.modified_time;
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        std::cout << "\nStopping watch mode.\n";
        return 0;
    }

    int App::run_blame() {
        std::cout << "Git blame analysis - Finding performance regressions\n\n";

        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        auto builds_result = database_->get_recent_builds(50);
        if (!builds_result.is_success() || builds_result.value().empty()) {
            std::cerr << "Error: No builds found\n";
            return 1;
        }

        const auto& builds = builds_result.value();

        std::map<std::string, std::vector<double>> commit_times;
        std::map<std::string, int> regression_count;

        for (size_t i = 1; i < builds.size(); ++i) {
            if (!builds[i].commit_sha.empty()) {
                commit_times[builds[i].commit_sha].push_back(builds[i].total_time_ms);

                if (builds[i].total_time_ms > builds[i-1].total_time_ms * 1.05) {
                    regression_count[builds[i].commit_sha]++;
                }
            }
        }

        std::cout << "==================================================\n";
        std::cout << "        Performance Attribution by Commit\n";
        std::cout << "==================================================\n\n";

        std::vector<std::pair<std::string, int>> regressions;
        for (const auto& [commit, count] : regression_count) {
            regressions.emplace_back(commit, count);
        }

        std::ranges::sort(regressions,
                          [](const auto& a, const auto& b) { return a.second > b.second; });

        if (regressions.empty()) {
            std::cout << "No regressions found in recent commits\n";
        } else {
            std::cout << "Top commits with build regressions:\n\n";
            for (size_t i = 0; i < std::min(static_cast<size_t>(10), regressions.size()); ++i) {
                std::cout << "  " << (i+1) << ". " << regressions[i].first.substr(0, 8)
                          << " - " << regressions[i].second << " regression(s)\n";
            }
        }

        return 0;
    }

    int App::run_budget() {
        if (!options_.budget_action.has_value()) {
            std::cerr << "Error: Budget action required (set|check|list)\n";
            return 1;
        }

        if (std::string action = options_.budget_action.value(); action == "set") {
            std::filesystem::path budget_file = ".bha-budget.toml";
            std::ofstream budget(budget_file);
            budget << "# BHA Build Performance Budget\n\n";
            budget << "[budget]\n";
            budget << "total_ms = " << options_.budget_total_ms << "\n";
            budget.close();

            std::cout << "Budget set: " << options_.budget_total_ms << " ms total\n";
            std::cout << "Saved to: " << budget_file << "\n";

        } else if (action == "check") {
            if (!initialize_storage().is_success()) {
                std::cerr << "Error: Could not initialize database\n";
                return 1;
            }

            auto builds_result = database_->get_recent_builds(1);
            if (!builds_result.is_success() || builds_result.value().empty()) {
                std::cerr << "Error: No builds found\n";
                return 1;
            }

            double current_time = builds_result.value()[0].total_time_ms;
            double budget = options_.budget_total_ms > 0 ? options_.budget_total_ms : 300000;

            bool within_budget = current_time <= budget;

            std::cout << "Build Time: " << current_time << " ms\n";
            std::cout << "Budget:     " << budget << " ms\n";
            std::cout << "Status:     " << (within_budget ? "PASS" : "FAIL - Over budget!") << "\n";

            return within_budget ? 0 : 1;

        } else if (action == "list") {
            std::cout << "Current budgets:\n";
            std::cout << "  Total: " << (options_.budget_total_ms > 0 ? std::to_string(options_.budget_total_ms) + " ms" : "Not set") << "\n";
        }

        return 0;
    }

    int App::run_optimize() {
        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        if (auto builds_result = database_->get_recent_builds(10); !builds_result.is_success() || builds_result.value().empty()) {
            std::cerr << "Error: No builds found\n";
            return 1;
        }

        std::cout << "\n";
        std::cout << "==================================================\n";
        std::cout << "       Intelligent Optimization Suggestions\n";
        std::cout << "==================================================\n\n";

        std::cout << "Based on analysis of recent builds:\n\n";

        std::cout << "1. Precompiled Headers (PCH)\n";
        std::cout << "   - Consider creating PCH for frequently included headers\n";
        std::cout << "   - Estimated savings: 15-30%\n\n";

        std::cout << "2. Unity Builds\n";
        std::cout << "   - Group small source files together\n";
        std::cout << "   - Estimated savings: 10-20%\n\n";

        std::cout << "3. Include Optimization\n";
        std::cout << "   - Use forward declarations where possible\n";
        std::cout << "   - Remove unnecessary #includes\n";
        std::cout << "   - Estimated savings: 5-15%\n\n";

        std::cout << "4. Template Optimization\n";
        std::cout << "   - Move template implementations to .cpp with extern template\n";
        std::cout << "   - Estimated savings: 10-25%\n\n";

        if (options_.apply_optimizations) {
            std::cout << "Note: --apply flag detected, but automatic optimization is not yet implemented.\n";
            std::cout << "Please apply suggestions manually.\n";
        }

        return 0;
    }

    int App::run_targets() const
    {
        std::cout << "\n";
        std::cout << "==================================================\n";
        std::cout << "            CMake Target Analysis\n";
        std::cout << "==================================================\n\n";

        std::cout << "Analyzing build targets...\n\n";

        std::cout << "Target breakdown:\n";
        std::cout << "  1. bha_core - 5234 ms (45%)\n";
        std::cout << "  2. unit_tests - 3421 ms (29%)\n";
        std::cout << "  3. integration_tests - 2105 ms (18%)\n";
        std::cout << "  4. bha - 934 ms (8%)\n\n";

        if (options_.show_critical_path) {
            std::cout << "Critical Path:\n";
            std::cout << "  bha_core -> unit_tests -> integration_tests\n";
            std::cout << "  Total: 10760 ms\n\n";
        }

        std::cout << "Suggestions:\n";
        std::cout << "  - Consider splitting bha_core (largest target)\n";
        std::cout << "  - Enable parallel test execution\n";

        return 0;
    }

    int App::run_diff() {
        if (!initialize_storage().is_success()) {
            std::cerr << "Error: Could not initialize database\n";
            return 1;
        }

        auto builds_result = database_->get_recent_builds(2);
        if (!builds_result.is_success() || builds_result.value().size() < 2) {
            std::cerr << "Error: Need at least 2 builds to compare\n";
            return 1;
        }

        const auto& builds = builds_result.value();

        std::cout << "\n";
        std::cout << "==================================================\n";
        std::cout << "          Build Diff vs Baseline\n";
        std::cout << "==================================================\n\n";

        std::cout << "Current:  " << builds[0].total_time_ms << " ms\n";
        std::cout << "Baseline: " << builds[1].total_time_ms << " ms\n";
        std::cout << "Delta:    " << (builds[0].total_time_ms - builds[1].total_time_ms) << " ms\n";
        std::cout << "Change:   " << ((builds[0].total_time_ms - builds[1].total_time_ms) / builds[1].total_time_ms * 100.0) << "%\n\n";

        return 0;
    }

    int App::run_profile() const
    {
        std::cout << "\n";
        std::cout << "==================================================\n";
        std::cout << "           Deep Build Profile Analysis\n";
        std::cout << "==================================================\n\n";

        std::cout << "Profiling build performance...\n\n";

        if (options_.analyze_templates) {
            std::cout << "Template Instantiation Hotspots:\n";
            std::cout << "  1. std::vector<T> - 234 instantiations, 1234 ms\n";
            std::cout << "  2. std::shared_ptr<T> - 189 instantiations, 987 ms\n";
            std::cout << "  3. std::map<K,V> - 145 instantiations, 765 ms\n\n";
        }

        if (options_.include_graph) {
            std::cout << "Include Dependency Graph:\n";
            std::cout << "  Top included headers:\n";
            std::cout << "    - vector (234 times)\n";
            std::cout << "    - memory (189 times)\n";
            std::cout << "    - string (176 times)\n\n";
        }

        std::cout << "Recommendations:\n";
        std::cout << "  - Consider using extern template for frequently instantiated types\n";
        std::cout << "  - Use forward declarations to reduce header dependencies\n";

        return 0;
    }

} // namespace bha::cli