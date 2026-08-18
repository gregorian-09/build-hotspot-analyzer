#include "bha/cli/commands/command.hpp"
#include "bha/cli/formatter.hpp"
#include "bha/build_systems/adapter.hpp"
#include "bha/lsp/suggestion_manager.hpp"
#include "bha/suggestions/suggester_catalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace bha::cli
{
    namespace fs = std::filesystem;
    using JsonValue = nlohmann::json;

    namespace {
        constexpr std::string_view kCliBuildProfileFile = ".bha-cli-build-profile.json";

        struct CliBuildProfile {
            fs::path project_root;
            std::optional<fs::path> build_dir;
            std::optional<fs::path> trace_dir;
            int recorded_build_time_ms = 0;
            std::string build_system;
        };

        constexpr std::string_view kProjectActionRecord = "record";
        constexpr std::string_view kProjectActionAnalyze = "analyze";
        constexpr std::string_view kProjectActionSuggestAlias = "suggest";
        constexpr std::string_view kProjectActionApply = "apply";
        constexpr std::string_view kProjectActionRevert = "revert";

        [[nodiscard]] bool is_supported_project_action(const std::string_view action) {
            return action == kProjectActionRecord || action == kProjectActionAnalyze ||
                   action == kProjectActionSuggestAlias || action == kProjectActionApply ||
                   action == kProjectActionRevert;
        }

        [[nodiscard]] std::string_view canonical_project_action(const std::string_view action) {
            if (action == kProjectActionSuggestAlias) {
                return kProjectActionAnalyze;
            }
            return action;
        }

        std::string_view lsp_priority_label(const lsp::Priority priority) {
            switch (priority) {
            case lsp::Priority::High:
                return "high";
            case lsp::Priority::Medium:
                return "medium";
            case lsp::Priority::Low:
                return "low";
            }
            return "low";
        }

        std::string_view lsp_suggestion_type_label(const lsp::SuggestionType type) {
            switch (type) {
            case lsp::SuggestionType::PrecompiledHeader:
                return "pch";
            case lsp::SuggestionType::HeaderSplit:
                return "header-split";
            case lsp::SuggestionType::UnityBuild:
                return "unity-build";
            case lsp::SuggestionType::TemplateOptimization:
                return "template";
            case lsp::SuggestionType::IncludeReduction:
                return "include";
            case lsp::SuggestionType::ForwardDeclaration:
                return "forward-decl";
            case lsp::SuggestionType::PIMPLPattern:
                return "pimpl";
            case lsp::SuggestionType::MoveToCpp:
                return "move-to-cpp";
            }
            return "unknown";
        }

        std::string_view complexity_label(const lsp::Complexity complexity) {
            switch (complexity) {
            case lsp::Complexity::Trivial:
                return "trivial";
            case lsp::Complexity::Simple:
                return "simple";
            case lsp::Complexity::Moderate:
                return "moderate";
            case lsp::Complexity::Complex:
                return "complex";
            }
            return "unknown";
        }

        JsonValue diagnostic_to_json(const lsp::Diagnostic& diagnostic) {
            return JsonValue{
                {"message", diagnostic.message},
                {"severity", static_cast<int>(diagnostic.severity)},
                {"source", diagnostic.source.value_or("")},
                {"line", diagnostic.range.start.line},
                {"column", diagnostic.range.start.character}
            };
        }

        JsonValue suggestion_to_json(const lsp::Suggestion& suggestion) {
            return JsonValue{
                {"id", suggestion.id},
                {"type", std::string(lsp_suggestion_type_label(suggestion.type))},
                {"priority", std::string(lsp_priority_label(suggestion.priority))},
                {"title", suggestion.title},
                {"description", suggestion.description},
                {"confidence", suggestion.confidence},
                {"autoApplicable", suggestion.auto_applicable},
                {"applicationMode", suggestion.application_mode.value_or("")},
                {"blockedReason", suggestion.auto_apply_blocked_reason.value_or("")},
                {"estimatedImpact", {
                    {"timeSavedMs", suggestion.estimated_impact.time_saved_ms},
                    {"percentage", suggestion.estimated_impact.percentage},
                    {"filesAffected", suggestion.estimated_impact.files_affected},
                    {"complexity", std::string(complexity_label(suggestion.estimated_impact.complexity))}
                }}
            };
        }

        fs::path resolve_project_root(const ParsedArgs& args) {
            if (const auto value = args.get("project-root"); value.has_value() && !value->empty()) {
                return fs::path(*value).lexically_normal();
            }
            return fs::current_path();
        }

        std::optional<fs::path> parse_optional_path(const ParsedArgs& args, const std::string& name) {
            if (const auto value = args.get(name); value.has_value() && !value->empty()) {
                return fs::path(*value).lexically_normal();
            }
            return std::nullopt;
        }

        fs::path resolve_cli_build_profile_path(
            const fs::path& project_root,
            const std::optional<fs::path>& build_dir,
            const std::optional<fs::path>& trace_dir
        ) {
            if (trace_dir.has_value()) {
                return *trace_dir / kCliBuildProfileFile;
            }
            if (build_dir.has_value()) {
                return *build_dir / kCliBuildProfileFile;
            }
            return project_root / kCliBuildProfileFile;
        }

        bool write_cli_build_profile(
            const fs::path& project_root,
            const std::optional<fs::path>& build_dir,
            const std::optional<fs::path>& trace_dir,
            const int recorded_build_time_ms,
            const std::string_view build_system
        ) {
            const fs::path profile_path =
                resolve_cli_build_profile_path(project_root, build_dir, trace_dir);
            std::error_code ec;
            if (const fs::path parent = profile_path.parent_path(); !parent.empty()) {
                fs::create_directories(parent, ec);
                if (ec) {
                    return false;
                }
            }

            JsonValue payload = {
                {"projectRoot", project_root.lexically_normal().string()},
                {"buildDir", build_dir.has_value() ? build_dir->lexically_normal().string() : ""},
                {"traceDir", trace_dir.has_value() ? trace_dir->lexically_normal().string() : ""},
                {"recordedBuildTimeMs", recorded_build_time_ms},
                {"buildSystem", std::string(build_system)}
            };

            std::ofstream out(profile_path);
            if (!out) {
                return false;
            }
            out << payload.dump(2) << "\n";
            return true;
        }

        std::optional<CliBuildProfile> read_cli_build_profile(
            const fs::path& project_root,
            const std::optional<fs::path>& build_dir,
            const std::optional<fs::path>& trace_dir
        ) {
            const fs::path profile_path =
                resolve_cli_build_profile_path(project_root, build_dir, trace_dir);
            std::ifstream in(profile_path);
            if (!in) {
                return std::nullopt;
            }

            JsonValue payload;
            try {
                in >> payload;
            } catch (...) {
                return std::nullopt;
            }

            CliBuildProfile profile;
            profile.project_root = fs::path(payload.value("projectRoot", "")).lexically_normal();
            const std::string build_dir_str = payload.value("buildDir", "");
            const std::string trace_dir_str = payload.value("traceDir", "");
            if (!build_dir_str.empty()) {
                profile.build_dir = fs::path(build_dir_str).lexically_normal();
            }
            if (!trace_dir_str.empty()) {
                profile.trace_dir = fs::path(trace_dir_str).lexically_normal();
            }
            profile.recorded_build_time_ms = payload.value("recordedBuildTimeMs", 0);
            profile.build_system = payload.value("buildSystem", "");

            if (profile.project_root != project_root.lexically_normal()) {
                return std::nullopt;
            }
            if (build_dir.has_value() && profile.build_dir.has_value() &&
                profile.build_dir->lexically_normal() != build_dir->lexically_normal()) {
                return std::nullopt;
            }
            if (trace_dir.has_value() && profile.trace_dir.has_value() &&
                profile.trace_dir->lexically_normal() != trace_dir->lexically_normal()) {
                return std::nullopt;
            }
            if (profile.recorded_build_time_ms <= 0) {
                return std::nullopt;
            }
            return profile;
        }

        void append_semicolon_separated_values(
            const std::optional<std::string>& raw_value,
            std::vector<std::string>& output
        ) {
            if (!raw_value.has_value() || raw_value->empty()) {
                return;
            }

            std::istringstream stream(*raw_value);
            std::string item;
            while (std::getline(stream, item, ';')) {
                if (!item.empty()) {
                    output.push_back(item);
                }
            }
        }

        build_systems::BuildOptions make_build_options(const ParsedArgs& args) {
            build_systems::BuildOptions options;
            options.enable_tracing = true;
            options.build_type = args.get_or("build-type", "Release");
            options.parallel_jobs = args.get_int("jobs").value_or(0);
            options.clean_first = args.get_flag("clean");
            options.verbose = args.get_flag("verbose");

            if (const auto build_dir = parse_optional_path(args, "build-dir"); build_dir.has_value()) {
                options.build_dir = *build_dir;
            }
            if (const auto trace_dir = parse_optional_path(args, "trace-dir"); trace_dir.has_value()) {
                options.trace_output_dir = *trace_dir;
            }

            options.compiler = args.get_or("compiler", "");
            options.c_compiler = args.get_or("c-compiler", "");
            options.cxx_compiler = args.get_or("cxx-compiler", "");
            append_semicolon_separated_values(args.get("extra-args"), options.extra_args);

            return options;
        }

        build_systems::BuildOptions make_validation_build_options(const ParsedArgs& args) {
            auto options = make_build_options(args);
            options.enable_tracing = false;
            options.clean_first = false;
            return options;
        }

        build_systems::IBuildSystemAdapter* resolve_build_adapter(
            const ParsedArgs& args,
            const fs::path& project_root
        ) {
            auto& registry = build_systems::BuildSystemRegistry::instance();
            if (const auto override_name = args.get("build-system"); override_name.has_value()) {
                return registry.get(*override_name);
            }
            return registry.detect(project_root);
        }

        struct CliTrustLoopResult {
            bool available = false;
            std::string reason;
            std::optional<int> predicted_savings_ms;
            std::optional<int> baseline_build_ms;
            std::optional<std::string> baseline_source;
            std::optional<int> rebuild_build_ms;
            std::optional<int> actual_savings_ms;
            std::optional<int> prediction_delta_ms;
            std::optional<double> actual_savings_percent;
        };

        std::optional<std::pair<int, std::string>> resolve_cli_trust_loop_baseline(
            const fs::path& project_root,
            const std::optional<fs::path>& build_dir,
            const std::optional<fs::path>& trace_dir,
            const lsp::AnalysisResult& analysis
        ) {
            if (const auto profile = read_cli_build_profile(project_root, build_dir, trace_dir); profile.has_value()) {
                return std::make_pair(profile->recorded_build_time_ms, std::string("recorded-build"));
            }
            if (analysis.baseline_metrics.total_duration_ms > 0) {
                return std::make_pair(
                    analysis.baseline_metrics.total_duration_ms,
                    std::string("trace-aggregate")
                );
            }
            return std::nullopt;
        }

        CliTrustLoopResult build_cli_trust_loop_result(
            const std::optional<int>& predicted_savings_ms,
            const std::optional<std::pair<int, std::string>>& baseline,
            const std::optional<int>& measured_rebuild_duration_ms,
            const bool validation_ran,
            const bool validation_success
        ) {
            CliTrustLoopResult result;
            result.predicted_savings_ms = predicted_savings_ms;
            if (!validation_ran) {
                result.reason = "validation-skipped";
                return result;
            }
            if (!validation_success) {
                result.reason = "validation-failed";
                return result;
            }
            if (!predicted_savings_ms.has_value()) {
                result.reason = "missing-predicted-savings";
                return result;
            }
            if (!baseline.has_value()) {
                result.reason = "missing-baseline";
                return result;
            }
            if (!measured_rebuild_duration_ms.has_value()) {
                result.reason = "missing-rebuild-duration";
                return result;
            }

            const int baseline_ms = baseline->first;
            const int rebuild_ms = *measured_rebuild_duration_ms;
            const int actual_savings_ms = baseline_ms - rebuild_ms;
            const int prediction_delta_ms = actual_savings_ms - *predicted_savings_ms;

            result.available = true;
            result.baseline_build_ms = baseline_ms;
            result.baseline_source = baseline->second;
            result.rebuild_build_ms = rebuild_ms;
            result.actual_savings_ms = actual_savings_ms;
            result.prediction_delta_ms = prediction_delta_ms;
            result.actual_savings_percent = baseline_ms > 0
                ? (static_cast<double>(actual_savings_ms) / static_cast<double>(baseline_ms)) * 100.0
                : 0.0;
            return result;
        }

        JsonValue cli_trust_loop_to_json(const CliTrustLoopResult& trust_loop) {
            if (!trust_loop.available) {
                return JsonValue{
                    {"available", false},
                    {"reason", trust_loop.reason}
                };
            }
            return JsonValue{
                {"available", true},
                {"predictedSavingsMs", trust_loop.predicted_savings_ms.value_or(0)},
                {"baselineBuildMs", trust_loop.baseline_build_ms.value_or(0)},
                {"baselineSource", trust_loop.baseline_source.value_or("unknown")},
                {"rebuildBuildMs", trust_loop.rebuild_build_ms.value_or(0)},
                {"actualSavingsMs", trust_loop.actual_savings_ms.value_or(0)},
                {"predictionDeltaMs", trust_loop.prediction_delta_ms.value_or(0)},
                {"actualSavingsPercent", trust_loop.actual_savings_percent.value_or(0.0)}
            };
        }

        lsp::SuggestionManagerConfig make_manager_config(const fs::path& project_root, const ParsedArgs& args) {
            auto config = lsp::SuggestionManagerConfig::defaults();
            config.workspace_root = project_root;
            config.min_confidence = args.get_double("min-confidence").value_or(config.min_confidence);
            config.include_unsafe_suggestions = args.get_flag("include-unsafe");
            config.allow_missing_compile_commands = true;
            return config;
        }

        lsp::AnalyzeSuggestionOptions make_analyze_options(const ParsedArgs& args) {
            lsp::AnalyzeSuggestionOptions options;
            const auto requested_types = args.get_all("type");
            for (const auto& token : requested_types) {
                if (const auto type = suggestions::parse_suggestion_type_id(token); type.has_value()) {
                    options.enabled_types.push_back(*type);
                }
            }
            if (args.has("min-confidence")) {
                options.min_confidence = args.get_double("min-confidence");
            }
            if (args.get_flag("include-unsafe")) {
                options.include_unsafe = true;
            }
            return options;
        }

        void print_suggestions_table(const std::vector<lsp::Suggestion>& suggestions) {
            Table table({
                {"ID", 14, false, std::nullopt},
                {"Priority", 8, false, std::nullopt},
                {"Type", 14, false, std::nullopt},
                {"Savings", 10, true, std::nullopt},
                {"Title", 0, false, std::nullopt},
            });

            for (const auto& suggestion : suggestions) {
                table.add_row({
                    suggestion.id,
                    std::string(lsp_priority_label(suggestion.priority)),
                    std::string(lsp_suggestion_type_label(suggestion.type)),
                    format_ms(static_cast<double>(suggestion.estimated_impact.time_saved_ms)),
                    suggestion.title,
                });
            }

            table.render(std::cout);
        }

        void print_diagnostics(const std::vector<lsp::Diagnostic>& diagnostics) {
            for (const auto& diagnostic : diagnostics) {
                std::cerr << diagnostic.source.value_or("bha")
                          << ":" << (diagnostic.range.start.line + 1)
                          << ":" << (diagnostic.range.start.character + 1)
                          << ": " << diagnostic.message << "\n";
            }
        }

        JsonValue make_analysis_json(const lsp::AnalysisResult& result) {
            JsonValue suggestions_json = JsonValue::array();
            for (const auto& suggestion : result.suggestions) {
                suggestions_json.push_back(suggestion_to_json(suggestion));
            }

            return JsonValue{
                {"analysisId", result.analysis_id},
                {"filesAnalyzed", result.files_analyzed},
                {"durationMs", result.duration_ms},
                {"baselineMetrics", {
                    {"totalDurationMs", result.baseline_metrics.total_duration_ms},
                    {"filesCompiled", result.baseline_metrics.files_compiled},
                    {"filesUpToDate", result.baseline_metrics.files_up_to_date}
                }},
                {"suggestions", suggestions_json}
            };
        }

        JsonValue make_apply_result_json(const lsp::ApplySuggestionResult& result) {
            JsonValue errors_json = JsonValue::array();
            for (const auto& diagnostic : result.errors) {
                errors_json.push_back(diagnostic_to_json(diagnostic));
            }

            return JsonValue{
                {"success", result.success},
                {"changedFiles", result.changed_files},
                {"backupId", result.backup_id.value_or("")},
                {"errors", errors_json}
            };
        }

        JsonValue make_apply_all_result_json(
            const lsp::ApplyAllResult& result,
            const std::optional<CliTrustLoopResult>& trust_loop = std::nullopt
        ) {
            JsonValue errors_json = JsonValue::array();
            for (const auto& diagnostic : result.errors) {
                errors_json.push_back(diagnostic_to_json(diagnostic));
            }

            JsonValue payload = JsonValue{
                {"success", result.success},
                {"appliedCount", result.applied_count},
                {"skippedCount", result.skipped_count},
                {"changedFiles", result.changed_files},
                {"appliedSuggestionIds", result.applied_suggestion_ids},
                {"backupId", result.backup_id},
                {"errors", errors_json}
            };
            if (trust_loop.has_value()) {
                payload["trustLoop"] = cli_trust_loop_to_json(*trust_loop);
            }
            return payload;
        }

        int sum_predicted_savings_for_ids(
            const lsp::AnalysisResult& analysis,
            const std::vector<std::string>& suggestion_ids
        ) {
            int total = 0;
            for (const auto& suggestion : analysis.suggestions) {
                if (std::ranges::find(suggestion_ids, suggestion.id) != suggestion_ids.end()) {
                    total += suggestion.estimated_impact.time_saved_ms;
                }
            }
            return total;
        }

        JsonValue make_revert_result_json(const lsp::RevertResult& result) {
            JsonValue errors_json = JsonValue::array();
            for (const auto& diagnostic : result.errors) {
                errors_json.push_back(diagnostic_to_json(diagnostic));
            }

            return JsonValue{
                {"success", result.success},
                {"restoredFiles", result.restored_files},
                {"errors", errors_json}
            };
        }
    }  // namespace

    class ProjectCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "project";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Run the project-level analyze/apply workflow used by IDE integrations";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha project <record|analyze|apply|revert> [OPTIONS]\n"
                   "\n"
                   "Aliases:\n"
                   "  suggest  Alias for analyze\n"
                   "\n"
                   "Examples:\n"
                   "  bha project record --project-root /path/to/project --build-dir build --trace-dir build/traces\n"
                   "  bha project analyze --project-root /path/to/project --build-dir build --trace-dir build/traces\n"
                   "  bha project apply --project-root /path/to/project --build-dir build --trace-dir build/traces --suggestion-id ana-2\n"
                   "  bha project apply --project-root /path/to/project --build-dir build --trace-dir build/traces --all --safe-only\n"
                   "  bha project revert --project-root /path/to/project --backup-id backup-123\n";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"project-root", 'r', "Project root directory (default: current directory)", false, true, "", "DIR"},
                {"build-dir", 'b', "Build directory", false, true, "", "DIR"},
                {"trace-dir", 't', "Trace directory", false, true, "", "DIR"},
                {"build-system", 's', "Build system override for record (cmake, ninja, make, ...)", false, true, "", "SYSTEM"},
                {"build-type", 'c', "Build configuration for record", false, true, "RelWithDebInfo", "TYPE"},
                {"jobs", 'j', "Parallel jobs for record", false, true, "0", "N"},
                {"compiler", 0, "Legacy single compiler override", false, true, "", "COMPILER"},
                {"c-compiler", 0, "C compiler override", false, true, "", "COMPILER"},
                {"cxx-compiler", 0, "C++ compiler override", false, true, "", "COMPILER"},
                {"extra-args", 0, "Additional build arguments for record (semicolon-separated)", false, true, "", "ARGS"},
                {"clean", 0, "Clean before recording build traces", false, false, "", ""},
                {"rebuild", 0, "Rebuild during analysis before generating suggestions", false, false, "", ""},
                {"suggestion-id", 0, "Suggestion ID to apply", false, true, "", "ID"},
                {"all", 0, "Apply all suggestions from the fresh analysis", false, false, "", ""},
                {"safe-only", 0, "Only apply suggestions with a direct automatic apply path", false, false, "", ""},
                {"min-priority", 'p', "Minimum priority for apply-all (high, medium, low)", false, true, "low", "LEVEL"},
                {"backup-id", 0, "Backup ID to revert", false, true, "", "ID"},
                {"min-confidence", 0, "Minimum confidence for analysis results", false, true, "0.5", "VALUE"},
                {"include-unsafe", 0, "Include potentially unsafe suggestions in analysis", false, false, "", ""},
                {"type", 0, "Restrict analysis to a suggestion type (repeatable)", false, true, "", "TYPE"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.positional().empty()) {
                return "Missing action. Use one of: record, analyze, apply, revert";
            }

            const std::string& action = args.positional().front();
            if (!is_supported_project_action(action)) {
                return "Unknown action: " + action;
            }

            const std::string_view canonical_action = canonical_project_action(action);
            if (canonical_action == kProjectActionApply) {
                const bool has_all = args.get_flag("all");
                const bool has_id = args.get("suggestion-id").has_value();
                if (has_all == has_id) {
                    return "Apply requires exactly one of --all or --suggestion-id <ID>";
                }
            }

            if (canonical_action == kProjectActionRevert && !args.get("backup-id").has_value()) {
                return "Revert requires --backup-id <ID>";
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

            const fs::path project_root = resolve_project_root(args);
            const auto build_dir = parse_optional_path(args, "build-dir");
            const auto trace_dir = parse_optional_path(args, "trace-dir");
            const std::string_view action = canonical_project_action(args.positional().front());

            if (action == kProjectActionRecord) {
                return execute_record(args, project_root);
            }
            if (action == kProjectActionAnalyze) {
                return execute_analyze(args, project_root, build_dir, trace_dir);
            }
            if (action == kProjectActionApply) {
                return execute_apply(args, project_root, build_dir, trace_dir);
            }
            return execute_revert(args, project_root);
        }

    private:
        int execute_record(const ParsedArgs& args, const fs::path& project_root) {
            auto* adapter = resolve_build_adapter(args, project_root);
            if (adapter == nullptr) {
                if (const auto override_name = args.get("build-system"); override_name.has_value()) {
                    print_error("Unknown build system: " + *override_name);
                } else {
                    print_error("Could not detect build system");
                }
                return 1;
            }

            auto options = make_build_options(args);
            options.on_output_line = [this](const std::string& line) {
                if (is_verbose()) {
                    print_verbose(line);
                }
            };

            auto build_result = adapter->build(project_root, options);
            if (build_result.is_err()) {
                print_error(build_result.error().message());
                return 1;
            }

            const auto& result = build_result.value();
            if (!result.success) {
                print_error(result.error_message.empty() ? "Build failed" : result.error_message);
                return 1;
            }

            write_cli_build_profile(
                project_root,
                options.build_dir,
                options.trace_output_dir,
                static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(result.build_time).count()
                ),
                adapter->name()
            );

            if (is_json()) {
                JsonValue payload = {
                    {"success", true},
                    {"buildSystem", adapter->name()},
                    {"buildTimeMs", std::chrono::duration_cast<std::chrono::milliseconds>(result.build_time).count()},
                    {"filesCompiled", result.files_compiled},
                    {"traceFiles", JsonValue::array()},
                    {"memoryFiles", JsonValue::array()}
                };
                for (const auto& file : result.trace_files) {
                    payload["traceFiles"].push_back(file.string());
                }
                for (const auto& file : result.memory_files) {
                    payload["memoryFiles"].push_back(file.string());
                }
                std::cout << payload.dump(2) << "\n";
                return 0;
            }

            print("Recorded build traces successfully");
            print("Build system: " + adapter->name());
            print("Build time: " + format_duration(result.build_time));
            print("Files compiled: " + format_count(result.files_compiled));
            print("Trace files: " + format_count(result.trace_files.size()));
            return 0;
        }

        int execute_analyze(
            const ParsedArgs& args,
            const fs::path& project_root,
            const std::optional<fs::path>& build_dir,
            const std::optional<fs::path>& trace_dir
        ) {
            auto manager = lsp::SuggestionManager(make_manager_config(project_root, args));
            const bool rebuild = args.get_flag("rebuild");
            auto emit_progress = [this](const std::string& phase, const int pct) {
                const std::string line = "[analyze " + std::to_string(pct) + "%] " + phase;
                if (is_json()) {
                    if (is_verbose()) {
                        std::cerr << line << "\n";
                    }
                    return;
                }
                if (verbosity() != Verbosity::Quiet) {
                    print(line);
                }
            };

            auto analysis = manager.analyze_project(
                project_root,
                build_dir,
                trace_dir,
                rebuild,
                emit_progress,
                make_analyze_options(args)
            );

            if (is_json()) {
                std::cout << make_analysis_json(analysis).dump(2) << "\n";
                return 0;
            }

            print("Analysis complete");
            print("Suggestions: " + format_count(analysis.suggestions.size()));
            print("Files analyzed: " + format_count(static_cast<std::size_t>(analysis.files_analyzed)));
            print_suggestions_table(analysis.suggestions);
            return 0;
        }

        int execute_apply(
            const ParsedArgs& args,
            const fs::path& project_root,
            const std::optional<fs::path>& build_dir,
            const std::optional<fs::path>& trace_dir
        ) {
            auto manager = lsp::SuggestionManager(make_manager_config(project_root, args));
            auto emit_analysis_progress = [this](const std::string& phase, const int pct) {
                const std::string line = "[analyze " + std::to_string(pct) + "%] " + phase;
                if (is_json()) {
                    if (is_verbose()) {
                        std::cerr << line << "\n";
                    }
                    return;
                }
                if (verbosity() != Verbosity::Quiet) {
                    print(line);
                }
            };

            const auto analysis = manager.analyze_project(
                project_root,
                build_dir,
                trace_dir,
                args.get_flag("rebuild"),
                emit_analysis_progress,
                make_analyze_options(args)
            );

            if (args.get_flag("all")) {
                auto emit_apply_progress = [this](const std::string& message) {
                    const std::string line = "[apply] " + message;
                    if (is_json()) {
                        if (is_verbose()) {
                            std::cerr << line << "\n";
                        }
                        return;
                    }
                    if (verbosity() != Verbosity::Quiet) {
                        print(line);
                    }
                };

                auto result = manager.apply_all_suggestions(
                    args.get("min-priority"),
                    args.get_flag("safe-only"),
                    emit_apply_progress
                );

                std::optional<CliTrustLoopResult> trust_loop;
                if (result.success && !result.applied_suggestion_ids.empty()) {
                    const auto predicted_savings_ms = sum_predicted_savings_for_ids(
                        analysis,
                        result.applied_suggestion_ids
                    );
                    const auto baseline = resolve_cli_trust_loop_baseline(
                        project_root,
                        build_dir,
                        trace_dir,
                        analysis
                    );

                    auto* adapter = resolve_build_adapter(args, project_root);
                    std::optional<int> rebuild_duration_ms;
                    bool validation_success = false;
                    bool validation_ran = false;
                    if (adapter != nullptr) {
                        auto build_options = make_validation_build_options(args);
                        build_options.on_output_line = [this](const std::string& line) {
                            if (is_verbose()) {
                                if (is_json()) {
                                    std::cerr << "[build] " << line << "\n";
                                } else {
                                    print("[build] " + line);
                                }
                            }
                        };
                        validation_ran = true;
                        if (auto build_result = adapter->build(project_root, build_options); build_result.is_ok()) {
                            const auto& rebuilt = build_result.value();
                            rebuild_duration_ms = static_cast<int>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    rebuilt.build_time
                                ).count()
                            );
                            validation_success = rebuilt.success;
                            if (!rebuilt.success) {
                                if (!result.backup_id.empty()) {
                                    const auto revert = manager.revert_changes_detailed(result.backup_id);
                                    if (!revert.success) {
                                        result.errors.insert(
                                            result.errors.end(),
                                            revert.errors.begin(),
                                            revert.errors.end()
                                        );
                                    }
                                }
                                lsp::Diagnostic diag;
                                diag.severity = lsp::DiagnosticSeverity::Error;
                                diag.source = "bha-cli";
                                diag.message = rebuilt.error_message.empty()
                                    ? "Build validation failed"
                                    : rebuilt.error_message;
                                result.errors.push_back(std::move(diag));
                                result.success = false;
                            }
                        } else {
                            lsp::Diagnostic diag;
                            diag.severity = lsp::DiagnosticSeverity::Error;
                            diag.source = "bha-cli";
                            diag.message = build_result.error().message();
                            result.errors.push_back(std::move(diag));
                            result.success = false;
                        }
                    }

                    trust_loop = build_cli_trust_loop_result(
                        std::make_optional(predicted_savings_ms),
                        baseline,
                        rebuild_duration_ms,
                        validation_ran,
                        validation_success
                    );
                }

                if (is_json()) {
                    std::cout << make_apply_all_result_json(result, trust_loop).dump(2) << "\n";
                } else {
                    print(result.success ? "Apply-all completed" : "Apply-all completed with errors");
                    print("Applied: " + format_count(result.applied_count));
                    print("Skipped: " + format_count(result.skipped_count));
                    if (trust_loop.has_value() && trust_loop->available) {
                        const char* direction =
                            trust_loop->actual_savings_ms.value_or(0) >= 0 ? "faster" : "slower";
                        print(
                            "Measured rebuild " + std::string(direction) + ": " +
                            format_duration(
                                std::chrono::milliseconds(
                                    std::abs(trust_loop->actual_savings_ms.value_or(0))
                                )
                            ) + " (" +
                            format_percent(
                                std::abs(trust_loop->actual_savings_percent.value_or(0.0))
                            ) + ")"
                        );
                        print(
                            std::string(
                                trust_loop->baseline_source.value_or("unknown") == "recorded-build"
                                    ? "Recorded baseline "
                                    : "Trace aggregate baseline "
                            ) +
                            format_duration(
                                std::chrono::milliseconds(trust_loop->baseline_build_ms.value_or(0))
                            ) + " -> measured rebuild " +
                            format_duration(
                                std::chrono::milliseconds(trust_loop->rebuild_build_ms.value_or(0))
                            )
                        );
                        print(
                            "Estimated " +
                            format_duration(
                                std::chrono::milliseconds(
                                    std::abs(trust_loop->predicted_savings_ms.value_or(0))
                                )
                            ) + "; measured-vs-estimate " +
                            format_duration(
                                std::chrono::milliseconds(
                                    std::abs(trust_loop->prediction_delta_ms.value_or(0))
                                )
                            )
                        );
                    }
                    if (!result.backup_id.empty()) {
                        print("Backup ID: " + result.backup_id);
                    }
                    if (!result.errors.empty()) {
                        print_diagnostics(result.errors);
                    }
                }
                return result.success ? 0 : 1;
            }

            const auto result = manager.apply_suggestion(*args.get("suggestion-id"));
            if (is_json()) {
                std::cout << make_apply_result_json(result).dump(2) << "\n";
            } else {
                print(result.success ? "Suggestion applied" : "Suggestion apply failed");
                if (result.backup_id.has_value()) {
                    print("Backup ID: " + *result.backup_id);
                }
                if (!result.changed_files.empty()) {
                    print("Changed files: " + format_count(result.changed_files.size()));
                }
                if (!result.errors.empty()) {
                    print_diagnostics(result.errors);
                }
            }
            return result.success ? 0 : 1;
        }

        int execute_revert(const ParsedArgs& args, const fs::path& project_root) {
            auto manager = lsp::SuggestionManager(make_manager_config(project_root, args));
            const auto result = manager.revert_changes_detailed(*args.get("backup-id"));

            if (is_json()) {
                std::cout << make_revert_result_json(result).dump(2) << "\n";
            } else {
                print(result.success ? "Revert completed" : "Revert completed with errors");
                print("Restored files: " + format_count(result.restored_files.size()));
                if (!result.errors.empty()) {
                    print_diagnostics(result.errors);
                }
            }

            return result.success ? 0 : 1;
        }
    };

    namespace {
        [[maybe_unused]] const bool registered = [] {
            CommandRegistry::instance().register_command(std::make_unique<ProjectCommand>());
            return true;
        }();
    }
}  // namespace bha::cli
