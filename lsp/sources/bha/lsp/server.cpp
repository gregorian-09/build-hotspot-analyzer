#include "bha/lsp/server.hpp"
#include "bha/lsp/uri.hpp"
#include "bha/build_systems/adapter.hpp"
#include "bha/suggestions/suggester_catalog.hpp"
#include "bha/suggestions/unreal_context.hpp"
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <thread>
#include <unordered_set>
#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace bha::lsp
{
    namespace {
        std::mutex g_lsp_write_mutex;
    }

    std::string shell_quote_for_shell(const std::string& input) {
#ifdef _WIN32
        std::string escaped = "\"";
        for (const char c : input) {
            if (c == '"') {
                escaped += "\\\"";
            } else {
                escaped.push_back(c);
            }
        }
        escaped.push_back('"');
        return escaped;
#else
        std::string escaped;
        escaped.reserve(input.size() + 2);
        escaped.push_back('\'');
        for (const char c : input) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped.push_back(c);
            }
        }
        escaped.push_back('\'');
        return escaped;
#endif
    }

    std::optional<bha::SuggestionType> parse_suggestion_type_token(const std::string& token) {
        return suggestions::parse_suggestion_type_id(token);
    }

    std::string suggestion_type_token(const bha::SuggestionType type) {
        return suggestions::suggestion_type_id(type);
    }

    json build_trust_loop_payload(
        const std::optional<int>& predicted_savings_ms,
        const std::optional<int>& baseline_duration_ms,
        const bool validation_ran,
        const bool validation_success,
        const std::optional<int>& measured_rebuild_duration_ms
    ) {
        if (!validation_ran) {
            return {
                {"available", false},
                {"reason", "validation-skipped"}
            };
        }
        if (!validation_success) {
            return {
                {"available", false},
                {"reason", "validation-failed"}
            };
        }
        if (!predicted_savings_ms.has_value()) {
            return {
                {"available", false},
                {"reason", "missing-predicted-savings"}
            };
        }
        if (!baseline_duration_ms.has_value()) {
            return {
                {"available", false},
                {"reason", "missing-baseline"}
            };
        }
        if (!measured_rebuild_duration_ms.has_value()) {
            return {
                {"available", false},
                {"reason", "missing-rebuild-duration"}
            };
        }

        const int predicted = *predicted_savings_ms;
        const int baseline_ms = *baseline_duration_ms;
        const int rebuild_ms = *measured_rebuild_duration_ms;
        const int actual_savings_ms = baseline_ms - rebuild_ms;
        const int prediction_delta_ms = actual_savings_ms - predicted;

        json payload = {
            {"available", true},
            {"predictedSavingsMs", predicted},
            {"actualSavingsMs", actual_savings_ms},
            {"predictionDeltaMs", prediction_delta_ms},
            {"baselineBuildMs", baseline_ms},
            {"rebuildBuildMs", rebuild_ms},
            {"actualSavingsPercent", baseline_ms > 0
                ? (static_cast<double>(actual_savings_ms) / static_cast<double>(baseline_ms)) * 100.0
                : 0.0}
        };

        if (predicted != 0) {
            payload["predictionErrorPercent"] =
                (static_cast<double>(std::abs(prediction_delta_ms)) /
                    static_cast<double>(std::abs(predicted))) * 100.0;
        }

        if (actual_savings_ms > 0) {
            payload["status"] = prediction_delta_ms >= 0 ? "met-or-exceeded" : "below-prediction";
        } else if (actual_savings_ms == 0) {
            payload["status"] = "no-change";
        } else {
            payload["status"] = "regression";
        }

        return payload;
    }

    std::optional<std::filesystem::path> workspace_path_from_uri_or_path(const std::string& workspace_root) {
        if (workspace_root.empty()) {
            return std::nullopt;
        }
        std::string root = workspace_root;
        if (root.starts_with("file://")) {
            root = root.substr(7);
        }
        if (root.empty()) {
            return std::nullopt;
        }
        return std::filesystem::path(root);
    }

    std::string utc_timestamp_iso8601() {
        using clock = std::chrono::system_clock;
        const auto now = clock::now();
        const std::time_t tt = clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &tt);
#else
        gmtime_r(&tt, &tm);
#endif
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return out.str();
    }

    bool command_exists(const std::string& name) {
        if (name.empty()) {
            return false;
        }
#ifdef _WIN32
        const std::string cmd = "where " + name + " >nul 2>nul";
#else
        const std::string cmd = "command -v " + name + " >/dev/null 2>&1";
#endif
        return std::system(cmd.c_str()) == 0;
    }

    bool has_unreal_markers(const std::filesystem::path& project_root) {
        return suggestions::is_unreal_project_root(project_root);
    }

    std::size_t count_unreal_rules_files(
        const std::filesystem::path& project_root,
        const std::string& suffix,
        const std::size_t limit = 4000
    ) {
        namespace fs = std::filesystem;
        const fs::path source_root = project_root / "Source";
        if (!fs::exists(source_root)) {
            return 0;
        }
        std::error_code ec;
        std::size_t count = 0;
        std::size_t scanned = 0;
        for (const auto& entry : fs::recursive_directory_iterator(source_root, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (++scanned > limit) {
                break;
            }
            const std::string name = entry.path().filename().string();
            if (name.ends_with(suffix)) {
                ++count;
            }
        }
        return count;
    }

    bool has_file_with_extension(
        const std::filesystem::path& directory,
        const std::string& extension
    ) {
        namespace fs = std::filesystem;
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            return false;
        }
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(directory, ec)) {
            if (ec) {
                return false;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() == extension) {
                return true;
            }
        }
        return false;
    }

    json build_unreal_environment_checks(const std::filesystem::path& project_root) {
        json checks = json::array();
        if (!has_unreal_markers(project_root)) {
            return checks;
        }

        checks.push_back({
            {"id", "project-detected"},
            {"status", "ok"},
            {"severity", "info"},
            {"message", "Detected Unreal project markers (.uproject or ModuleRules/TargetRules)."}
        });

        const bool has_engine_env =
            (std::getenv("BHA_UE_BUILD_SCRIPT") && *std::getenv("BHA_UE_BUILD_SCRIPT")) ||
            (std::getenv("UE_ENGINE_ROOT") && *std::getenv("UE_ENGINE_ROOT")) ||
            (std::getenv("UNREAL_ENGINE_ROOT") && *std::getenv("UNREAL_ENGINE_ROOT"));
        const bool has_ubt_on_path = command_exists("UnrealBuildTool");
        if (has_engine_env || has_ubt_on_path) {
            checks.push_back({
                {"id", "build-tooling"},
                {"status", "ok"},
                {"severity", "info"},
                {"message", "Unreal build tooling is discoverable for rebuild validation."}
            });
        } else {
            checks.push_back({
                {"id", "build-tooling"},
                {"status", "warning"},
                {"severity", "warning"},
                {"message", "Unreal build tooling is not discoverable from current environment."},
                {"recommendedAction", "Set BHA_UE_BUILD_SCRIPT or UE_ENGINE_ROOT/UNREAL_ENGINE_ROOT, or add UnrealBuildTool to PATH."}
            });
        }

        const std::size_t module_rules = count_unreal_rules_files(project_root, ".Build.cs");
        const std::size_t target_rules = count_unreal_rules_files(project_root, ".Target.cs");
        if (module_rules == 0) {
            checks.push_back({
                {"id", "module-rules"},
                {"status", "warning"},
                {"severity", "warning"},
                {"message", "No *.Build.cs files were found under Source/."},
                {"recommendedAction", "Verify module layout or point analysis to the actual Unreal workspace root."}
            });
        } else {
            checks.push_back({
                {"id", "module-rules"},
                {"status", "ok"},
                {"severity", "info"},
                {"message", "Found " + std::to_string(module_rules) + " Unreal ModuleRules files."}
            });
        }

        if (target_rules == 0) {
            checks.push_back({
                {"id", "target-rules"},
                {"status", "warning"},
                {"severity", "warning"},
                {"message", "No *.Target.cs files were found under Source/; target inference may be limited."},
                {"recommendedAction", "Add or expose target rules to improve build-target resolution."}
            });
        } else {
            checks.push_back({
                {"id", "target-rules"},
                {"status", "ok"},
                {"severity", "info"},
                {"message", "Found " + std::to_string(target_rules) + " Unreal TargetRules files."}
            });
        }

        const bool has_rider_workspace = std::filesystem::exists(project_root / ".idea");
        const bool has_visual_studio_solution = has_file_with_extension(project_root, ".sln");
        if (!has_rider_workspace && !has_visual_studio_solution) {
            checks.push_back({
                {"id", "ide-workflow"},
                {"status", "info"},
                {"severity", "info"},
                {"message", "Optional: configure Rider or Visual Studio Unreal integration for smoother apply/rebuild workflow."}
            });
        } else {
            checks.push_back({
                {"id", "ide-workflow"},
                {"status", "ok"},
                {"severity", "info"},
                {"message", "Detected Rider/Visual Studio project metadata for Unreal workflow."}
            });
        }

        return checks;
    }

    LSPConfig LSPConfig::from_json(const json& j) {
        LSPConfig config;
        if (j.contains("optimization")) {
            const auto& opt = j["optimization"];
            if (opt.contains("autoApplyAll")) config.auto_apply_all = opt["autoApplyAll"].get<bool>();
            if (opt.contains("showPreviewBeforeApply")) config.show_preview_before_apply = opt["showPreviewBeforeApply"].get<bool>();
            if (opt.contains("rebuildAfterApply")) config.rebuild_after_apply = opt["rebuildAfterApply"].get<bool>();
            if (opt.contains("rollbackOnBuildFailure")) config.rollback_on_build_failure = opt["rollbackOnBuildFailure"].get<bool>();
            if (opt.contains("buildCommand")) config.build_command = opt["buildCommand"].get<std::string>();
            if (opt.contains("buildTimeout")) config.build_timeout_seconds = opt["buildTimeout"].get<int>();
            if (opt.contains("keepBackups")) config.keep_backups = opt["keepBackups"].get<bool>();
            if (opt.contains("backupDirectory")) config.backup_directory = opt["backupDirectory"].get<std::string>();
            if (opt.contains("persistTrustLoop")) config.persist_trust_loop = opt["persistTrustLoop"].get<bool>();
            if (opt.contains("allowMissingCompileCommands")) config.allow_missing_compile_commands = opt["allowMissingCompileCommands"].get<bool>();
            if (opt.contains("includeUnsafeSuggestions")) config.include_unsafe_suggestions = opt["includeUnsafeSuggestions"].get<bool>();
            if (opt.contains("minConfidence")) config.min_confidence = opt["minConfidence"].get<double>();

            if (opt.contains("pch") && opt["pch"].contains("autoApply"))
                config.per_optimization.pch_auto_apply = opt["pch"]["autoApply"].get<bool>();
            if (opt.contains("headerSplitting") && opt["headerSplitting"].contains("autoApply"))
                config.per_optimization.header_splitting_auto_apply = opt["headerSplitting"]["autoApply"].get<bool>();
            if (opt.contains("unityBuild") && opt["unityBuild"].contains("autoApply"))
                config.per_optimization.unity_build_auto_apply = opt["unityBuild"]["autoApply"].get<bool>();
            if (opt.contains("templateOptimization") && opt["templateOptimization"].contains("autoApply"))
                config.per_optimization.template_optimization_auto_apply = opt["templateOptimization"]["autoApply"].get<bool>();
            if (opt.contains("includeReduction") && opt["includeReduction"].contains("autoApply"))
                config.per_optimization.include_reduction_auto_apply = opt["includeReduction"]["autoApply"].get<bool>();
            if (opt.contains("forwardDeclaration") && opt["forwardDeclaration"].contains("autoApply"))
                config.per_optimization.forward_declaration_auto_apply = opt["forwardDeclaration"]["autoApply"].get<bool>();
            if (opt.contains("pimpl") && opt["pimpl"].contains("autoApply"))
                config.per_optimization.pimpl_auto_apply = opt["pimpl"]["autoApply"].get<bool>();
            if (opt.contains("moveToCpp") && opt["moveToCpp"].contains("autoApply"))
                config.per_optimization.move_to_cpp_auto_apply = opt["moveToCpp"]["autoApply"].get<bool>();
        }
        return config;
    }

    json LSPConfig::to_json() const {
        return {
            {"optimization", {
                {"autoApplyAll", auto_apply_all},
                {"showPreviewBeforeApply", show_preview_before_apply},
                {"rebuildAfterApply", rebuild_after_apply},
                {"rollbackOnBuildFailure", rollback_on_build_failure},
                {"buildCommand", build_command},
                {"buildTimeout", build_timeout_seconds},
                {"keepBackups", keep_backups},
                {"backupDirectory", backup_directory},
                {"persistTrustLoop", persist_trust_loop},
                {"allowMissingCompileCommands", allow_missing_compile_commands},
                {"includeUnsafeSuggestions", include_unsafe_suggestions},
                {"minConfidence", min_confidence},
                {"pch", {{"autoApply", per_optimization.pch_auto_apply}}},
                {"headerSplitting", {{"autoApply", per_optimization.header_splitting_auto_apply}}},
                {"unityBuild", {{"autoApply", per_optimization.unity_build_auto_apply}}},
                {"templateOptimization", {{"autoApply", per_optimization.template_optimization_auto_apply}}},
                {"includeReduction", {{"autoApply", per_optimization.include_reduction_auto_apply}}},
                {"forwardDeclaration", {{"autoApply", per_optimization.forward_declaration_auto_apply}}},
                {"pimpl", {{"autoApply", per_optimization.pimpl_auto_apply}}},
                {"moveToCpp", {{"autoApply", per_optimization.move_to_cpp_auto_apply}}}
            }}
        };
    }

    LSPServer::LSPServer() {
        initialize_handlers();
    }

    LSPServer::~LSPServer() {
        stop();
    }

    void LSPServer::run() {
        while (running_) {
            try {
                if (std::string message = read_message(); !message.empty()) {
                    handle_message(message);
                }
                cleanup_finished_jobs();
            } catch (const std::exception& e) {
                std::cerr << "Error processing message: " << e.what() << std::endl;
            }
        }
        cleanup_finished_jobs();
    }

    void LSPServer::stop() {
        running_ = false;

        std::vector<std::shared_ptr<AsyncJob>> jobs;
        {
            std::lock_guard lock(async_jobs_mutex_);
            for (const auto& [_, job] : async_jobs_) {
                job->cancel_requested.store(true);
                jobs.push_back(job);
            }
        }

        for (const auto& job : jobs) {
            if (job->worker.joinable()) {
                job->worker.join();
            }
        }
    }

    void LSPServer::register_request_handler(const std::string& method, RequestHandler handler) {
        request_handlers_[method] = std::move(handler);
    }

    void LSPServer::register_notification_handler(const std::string& method, NotificationHandler handler) {
        notification_handlers_[method] = std::move(handler);
    }

    void LSPServer::send_notification(const std::string& method, const json& params) {
        NotificationMessage notification;
        notification.jsonrpc = "2.0";
        notification.method = method;
        notification.params = params;

        const json j = notification;
        write_message(j.dump());
    }

    void LSPServer::send_response(const RequestId& id, const json& result) {
        ResponseMessage response;
        response.jsonrpc = "2.0";
        response.id = id;
        response.result = result;

        const json j = response;
        write_message(j.dump());
    }

    void LSPServer::send_error(const RequestId& id, ErrorCode code, const std::string& message,
                               const std::optional<json>& data) {
        ResponseMessage response;
        response.jsonrpc = "2.0";
        response.id = id;

        ResponseError error;
        error.code = static_cast<int>(code);
        error.message = message;
        error.data = data;

        response.error = error;

        const json j = response;
        write_message(j.dump());
    }

    ProgressToken LSPServer::create_progress_token() {
        return "bha-progress-" + std::to_string(++progress_token_counter_);
    }

    void LSPServer::send_progress(const ProgressToken& token, const json& value) {
        json params;
        std::visit([&params](auto&& arg) { params["token"] = arg; }, token);
        params["value"] = value;
        send_notification("$/progress", params);
    }

    std::string LSPServer::async_job_status_to_string(const AsyncJobStatus status) {
        switch (status) {
            case AsyncJobStatus::Queued: return "queued";
            case AsyncJobStatus::Running: return "running";
            case AsyncJobStatus::Completed: return "completed";
            case AsyncJobStatus::Failed: return "failed";
            case AsyncJobStatus::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    std::string LSPServer::create_async_job_id() {
        return "bha-job-" + std::to_string(++async_job_counter_);
    }

    json LSPServer::build_async_job_payload(const AsyncJob& job) const {
        std::lock_guard state_lock(job.mutex);
        const auto to_iso = [](const std::chrono::system_clock::time_point& tp) -> std::string {
            if (tp.time_since_epoch().count() == 0) {
                return "";
            }
            const std::time_t tt = std::chrono::system_clock::to_time_t(tp);
            std::tm tm{};
#ifdef _WIN32
            gmtime_s(&tm, &tt);
#else
            gmtime_r(&tt, &tm);
#endif
            std::ostringstream out;
            out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            return out.str();
        };

        json payload = {
            {"jobId", job.id},
            {"command", job.command},
            {"status", async_job_status_to_string(job.status.load())},
            {"cancelRequested", job.cancel_requested.load()},
            {"finished", job.finished.load()},
            {"createdAt", to_iso(job.created_at)},
            {"startedAt", to_iso(job.started_at)},
            {"finishedAt", to_iso(job.finished_at)}
        };

        if (job.error.has_value()) {
            payload["error"] = *job.error;
        }
        if (job.result.has_value()) {
            payload["result"] = *job.result;
        }
        return payload;
    }

    void LSPServer::cleanup_finished_jobs() {
        std::lock_guard lock(async_jobs_mutex_);
        for (auto& [_, job] : async_jobs_) {
            if (job->finished.load() && job->worker.joinable()) {
                job->worker.join();
            }
        }
    }

    json LSPServer::queue_async_command(const std::string& command, const json& args) {
        auto job = std::make_shared<AsyncJob>();
        job->id = create_async_job_id();
        job->command = command;
        job->args = args;
        job->created_at = std::chrono::system_clock::now();
        job->progress_token = create_progress_token();

        {
            std::lock_guard lock(async_jobs_mutex_);
            async_jobs_[job->id] = job;
        }

        job->worker = std::thread([this, job]() {
            run_async_job(job);
        });

        json progress_token_json;
        std::visit([&progress_token_json](auto&& value) {
            progress_token_json = value;
        }, job->progress_token);

        return {
            {"accepted", true},
            {"async", true},
            {"jobId", job->id},
            {"status", "queued"},
            {"progressToken", progress_token_json}
        };
    }

    void LSPServer::run_async_job(const std::shared_ptr<AsyncJob>& job) {
        WorkDoneProgressBegin begin;
        begin.title = "BHA " + job->command;
        begin.cancellable = true;
        begin.percentage = 0;
        json begin_json;
        to_json(begin_json, begin);
        send_progress(job->progress_token, begin_json);

        send_notification("bha/jobStarted", {
            {"jobId", job->id},
            {"command", job->command}
        });

        if (job->cancel_requested.load()) {
            job->status = AsyncJobStatus::Cancelled;
            job->finished = true;
            {
                std::lock_guard lock(job->mutex);
                job->started_at = std::chrono::system_clock::now();
                job->finished_at = job->started_at;
            }
            WorkDoneProgressEnd end;
            end.message = "Job cancelled before start";
            json end_json;
            to_json(end_json, end);
            send_progress(job->progress_token, end_json);
            send_notification("bha/jobCompleted", build_async_job_payload(*job));
            return;
        }

        job->status = AsyncJobStatus::Running;
        {
            std::lock_guard lock(job->mutex);
            job->started_at = std::chrono::system_clock::now();
        }

        WorkDoneProgressReport report;
        report.message = "Applying suggestions in background";
        report.percentage = 30;
        json report_json;
        to_json(report_json, report);
        send_progress(job->progress_token, report_json);

        try {
            json run_args = job->args;
            run_args["async"] = false;
            run_args["skipConsent"] = true;

            json result;
            if (job->command == "bha.applySuggestion") {
                result = execute_apply_suggestion(run_args);
            } else if (job->command == "bha.applyAllSuggestions") {
                result = execute_apply_all_suggestions(run_args);
            } else if (job->command == "bha.applyEdits") {
                result = execute_apply_edits(run_args);
            } else {
                throw std::runtime_error("Unsupported async command: " + job->command);
            }

            if (job->cancel_requested.load()) {
                if (result.contains("backupId") && result["backupId"].is_string()) {
                    const std::string backup_id = result["backupId"].get<std::string>();
                    if (!backup_id.empty() && suggestion_manager_) {
                        RevertResult revert_result;
                        {
                            std::lock_guard lock(suggestion_manager_mutex_);
                            revert_result = suggestion_manager_->revert_changes_detailed(backup_id);
                        }
                        result["cancelRollback"] = {
                            {"attempted", true},
                            {"success", revert_result.success},
                            {"restoredFiles", revert_result.restored_files}
                        };
                    }
                }
                job->status = AsyncJobStatus::Cancelled;
                {
                    std::lock_guard lock(job->mutex);
                    job->error = "Cancellation was requested while job was running";
                }
            } else {
                job->status = AsyncJobStatus::Completed;
            }

            {
                std::lock_guard lock(job->mutex);
                job->result = std::move(result);
            }
        } catch (const std::exception& e) {
            job->status = job->cancel_requested.load() ? AsyncJobStatus::Cancelled : AsyncJobStatus::Failed;
            {
                std::lock_guard lock(job->mutex);
                job->error = e.what();
            }
        }

        job->finished = true;
        {
            std::lock_guard lock(job->mutex);
            job->finished_at = std::chrono::system_clock::now();
        }

        WorkDoneProgressEnd end;
        if (job->status.load() == AsyncJobStatus::Completed) {
            end.message = "Background apply completed";
        } else if (job->status.load() == AsyncJobStatus::Cancelled) {
            end.message = "Background apply cancelled";
        } else {
            end.message = "Background apply failed";
        }
        json end_json;
        to_json(end_json, end);
        send_progress(job->progress_token, end_json);

        send_notification("bha/jobCompleted", build_async_job_payload(*job));
    }

    void LSPServer::handle_message(const std::string& message) {
        try {
            if (json j = json::parse(message); j.contains("id")) {
                if (j.contains("method")) {
                    auto request = j.get<RequestMessage>();
                    handle_request(request);
                } else {
                    auto response = j.get<ResponseMessage>();
                    handle_response(response);
                }
            } else {
                auto notification = j.get<NotificationMessage>();
                handle_notification(notification);
            }
        } catch (const json::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            send_error(0, ErrorCode::ParseError,
                      std::string("JSON parse error: ") + e.what());
        }
    }

    void LSPServer::handle_response(const ResponseMessage& response) {
        int id = 0;
        if (std::holds_alternative<int>(response.id)) {
            id = std::get<int>(response.id);
        } else if (std::holds_alternative<std::string>(response.id)) {
            try {
                id = std::stoi(std::get<std::string>(response.id));
            } catch (...) {
                return;
            }
        }

        std::lock_guard lock(pending_mutex_);
        if (const auto it = pending_requests_.find(id); it != pending_requests_.end()) {
            it->second.response = response.result;
            it->second.received = true;
            pending_cv_.notify_all();
        }
    }

    json LSPServer::send_request(const std::string& method, const json& params) {
        int id = ++request_id_counter_;

        {
            std::lock_guard lock(pending_mutex_);
            pending_requests_[id] = PendingRequest{};
        }

        RequestMessage request;
        request.jsonrpc = "2.0";
        request.id = id;
        request.method = method;
        request.params = params;

        const json j = request;
        write_message(j.dump());

        std::unique_lock lock(pending_mutex_);
        pending_cv_.wait(lock, [this, id] {
            return pending_requests_[id].received;
        });

        const auto result = pending_requests_[id].response;
        pending_requests_.erase(id);

        return result.value_or(json{});
    }

    LSPServer::UserConsentResult LSPServer::request_user_consent(
        const std::string& message,
        const std::vector<std::string>& actions
    ) {
        UserConsentResult result;

        if (config_.auto_apply_all) {
            result.approved = true;
            result.selected_action = actions.empty() ? "Apply" : actions[0];
            return result;
        }

        json action_items = json::array();
        for (const auto& action : actions) {
            action_items.push_back({{"title", action}});
        }

        const json params = {
            {"type", static_cast<int>(MessageType::Info)},
            {"message", message},
            {"actions", action_items}
        };

        try {
            if (json response = send_request("window/showMessageRequest", params); response.is_object() && response.contains("title")) {
                result.selected_action = response["title"].get<std::string>();
                result.approved = (result.selected_action == "Apply" ||
                                  result.selected_action == "Yes" ||
                                  result.selected_action == "Accept");
            } else if (response.is_null()) {
                result.approved = false;
                result.selected_action = "";
            }
        } catch (const std::exception&) {
            result.approved = false;
        }

        return result;
    }

    void LSPServer::handle_request(const RequestMessage& request) {
        // Lifecycle validation per LSP spec
        // Only "initialize" is allowed before initialization
        if (state_ == ServerState::Uninitialized && request.method != "initialize") {
            send_error(request.id, ErrorCode::ServerNotInitialized,
                      "Server not initialized. Call 'initialize' first.");
            return;
        }

        // After shutdown, only "exit" notification is allowed (handled elsewhere)
        if (state_ == ServerState::ShuttingDown) {
            send_error(request.id, ErrorCode::InvalidRequest,
                      "Server is shutting down. Only 'exit' notification is accepted.");
            return;
        }

        if (const auto it = request_handlers_.find(request.method); it != request_handlers_.end()) {
            try {
                const json params = request.params.value_or(json::object());
                const json result = it->second(params);
                send_response(request.id, result);
            } catch (const std::exception& e) {
                send_error(request.id, ErrorCode::InternalError, e.what());
            }
        } else {
            send_error(request.id, ErrorCode::MethodNotFound,
                      "Method not found: " + request.method);
        }
    }

    void LSPServer::handle_notification(const NotificationMessage& notification) {
        if (notification.method == "$/cancelRequest") {
            const json params = notification.params.value_or(json::object());
            if (params.contains("id") && params["id"].is_string()) {
                const std::string id = params["id"].get<std::string>();
                json cancel_args = {
                    {"jobId", id}
                };
                try {
                    execute_cancel_job(cancel_args);
                } catch (const std::exception& e) {
                    std::cerr << "Error handling cancel request: " << e.what() << std::endl;
                }
            }
            return;
        }

        if (const auto it = notification_handlers_.find(notification.method); it != notification_handlers_.end()) {
            try {
                const json params = notification.params.value_or(json::object());
                it->second(params);
            } catch (const std::exception& e) {
                std::cerr << "Error handling notification: " << e.what() << std::endl;
            }
        }
    }

    std::string LSPServer::read_message() {
        std::string header;
        int content_length = 0;

        // Read headers until empty line
        // LSP uses HTTP-style headers with CRLF line endings
        // After std::getline, the line has trailing \r (if CRLF was used)
        // An empty line will be "" (Unix) or "\r" (CRLF)
        while (std::getline(std::cin, header)) {
            // Trim trailing \r if present (from CRLF line endings)
            if (!header.empty() && header.back() == '\r') {
                header.pop_back();
            }

            if (header.empty()) {
                break;
            }

            if (header.find("Content-Length: ") == 0) {
                content_length = std::stoi(header.substr(16));
            }
        }

        if (content_length == 0) {
            return "";
        }

        std::string content(static_cast<std::string::size_type>(content_length), '\0');
        std::cin.read(&content[0], content_length);

        return content;
    }

    void LSPServer::write_message(const std::string& content) {
        std::lock_guard lock(g_lsp_write_mutex);
        std::ostringstream header;
        header << "Content-Length: " << content.length() << "\r\n\r\n";

        std::cout << header.str() << content << std::flush;
    }

    void LSPServer::initialize_handlers() {
        register_request_handler("initialize", [this](const json& params) {
            return handle_initialize(params);
        });

        register_notification_handler("initialized", [this](const json& params) {
            handle_initialized(params);
        });

        register_request_handler("shutdown", [this](const json& params) {
            return handle_shutdown(params);
        });

        register_notification_handler("exit", [this](const json& params) {
            handle_exit(params);
        });

        register_request_handler("workspace/executeCommand", [this](const json& params) {
            return handle_execute_command(params);
        });

        register_request_handler("textDocument/codeAction", [this](const json& params) {
            return handle_text_document_code_action(params);
        });

        register_notification_handler("workspace/didChangeConfiguration", [this](const json& params) {
            if (params.contains("settings")) {
                config_ = LSPConfig::from_json(params["settings"]);
            }
        });
    }

    json LSPServer::handle_initialize(const json& params) {
        if (params.contains("rootUri")) {
            const std::string root_uri = params["rootUri"].get<std::string>();
            workspace_root_ = uri::uri_to_path(root_uri).string();
        } else if (params.contains("rootPath")) {
            workspace_root_ = params["rootPath"].get<std::string>();
        }

        if (params.contains("capabilities")) {
            client_capabilities_ = params["capabilities"];
        }

        json capabilities = {
            {"textDocumentSync", 1},
            {"codeActionProvider", true},
            {"executeCommandProvider", {
                {"commands", json::array({
                    "bha.analyze",
                    "bha.applySuggestion",
                    "bha.applyEdits",
                    "bha.applyAllSuggestions",
                    "bha.getJobStatus",
                    "bha.cancelJob",
                    "bha.getSuggestionDetails",
                    "bha.revertChanges",
                    "bha.showMetrics",
                    "bha.listSuggesters",
                    "bha.runSuggester",
                    "bha.explainSuggestion"
                })}
            }},
            {"experimental", {
                {"bha", {
                    {"version", "1.0"},
                    {"features", {
                        {"suggestionManagement", true},
                        {"safeEditing", true},
                        {"buildIntegration", true},
                        {"impactAnalysis", true},
                        {"autoRollback", true},
                        {"asyncJobPipeline", true}
                    }}
                }}
            }}
        };

        json result = {
            {"capabilities", capabilities},
            {"serverInfo", {
                {"name", "bha-lsp"},
                {"version", "1.0.0"}
            }}
        };

        state_ = ServerState::Initializing;
        return result;
    }

    void LSPServer::handle_initialized(const json&) {
        state_ = ServerState::Running;

        SuggestionManagerConfig sm_config;
        sm_config.use_disk_backups = true;
        sm_config.keep_backups = config_.keep_backups;
        sm_config.backup_directory = config_.backup_directory;
        sm_config.allow_missing_compile_commands = config_.allow_missing_compile_commands;
        sm_config.include_unsafe_suggestions = config_.include_unsafe_suggestions;
        sm_config.min_confidence = config_.min_confidence;
        sm_config.enforce_forward_decl_syntax_gate = true;
        sm_config.forward_decl_validation_timeout_seconds = config_.build_timeout_seconds;
        if (!workspace_root_.empty()) {
            std::string root = workspace_root_;
            if (root.starts_with("file://")) {
                root = root.substr(7);
            }
            sm_config.workspace_root = root;
        }
        suggestion_manager_ = std::make_unique<SuggestionManager>(sm_config);

        send_notification("window/showMessage", {
            {"type", static_cast<int>(MessageType::Info)},
            {"message", "BHA LSP Server initialized"}
        });
    }

    json LSPServer::handle_shutdown(const json&) {
        state_ = ServerState::ShuttingDown;
        return nullptr;
    }

    void LSPServer::handle_exit(const json&) {
        running_ = false;
    }

    json LSPServer::handle_execute_command(const json& params) {
        if (!params.contains("command")) {
            throw std::runtime_error("Missing command in executeCommand");
        }

        const std::string command = params["command"].get<std::string>();
        const json args = params.contains("arguments") && params["arguments"].is_array() && !params["arguments"].empty()
            ? params["arguments"][0]
            : json::object();

        if (command == "bha.analyze") {
            return execute_analyze(args);
        }
        if (command == "bha.applySuggestion") {
            return execute_apply_suggestion(args);
        }
        if (command == "bha.applyEdits") {
            return execute_apply_edits(args);
        }
        if (command == "bha.applyAllSuggestions") {
            return execute_apply_all_suggestions(args);
        }
        if (command == "bha.getJobStatus") {
            return execute_get_job_status(args);
        }
        if (command == "bha.cancelJob") {
            return execute_cancel_job(args);
        }
        if (command == "bha.revertChanges") {
            return execute_revert_changes(args);
        }
        if (command == "bha.getSuggestionDetails") {
            return execute_get_suggestion_details(args);
        }
        if (command == "bha.showMetrics") {
            return execute_show_metrics(args);
        }
        if (command == "bha.listSuggesters") {
            return execute_list_suggesters(args);
        }
        if (command == "bha.runSuggester") {
            return execute_run_suggester(args);
        }
        if (command == "bha.explainSuggestion") {
            return execute_explain_suggestion(args);
        }

        throw std::runtime_error("Unknown command: " + command);
    }

    json LSPServer::handle_text_document_code_action(const json& params) const
    {
        json result = json::array();

        if (!suggestion_manager_) {
            return result;
        }

        std::string uri;
        if (params.contains("textDocument") && params["textDocument"].contains("uri")) {
            uri = params["textDocument"]["uri"].get<std::string>();
        }

        std::vector<Suggestion> suggestions;
        {
            std::lock_guard lock(suggestion_manager_mutex_);
            suggestions = suggestion_manager_->get_all_suggestions();
        }
        for (const auto& sug : suggestions) {
            if (sug.target_uri && *sug.target_uri == uri) {
                json code_action = {
                    {"title", sug.title},
                    {"kind", "refactor.rewrite"},
                    {"command", {
                        {"title", sug.title},
                        {"command", "bha.applySuggestion"},
                        {"arguments", json::array({json{{"suggestionId", sug.id}}})}
                    }}
                };

                if (sug.range) {
                    json range_json;
                    to_json(range_json, *sug.range);
                    code_action["diagnostics"] = json::array({
                        {{"range", range_json},
                         {"message", sug.description},
                         {"severity", static_cast<int>(DiagnosticSeverity::Hint)},
                         {"source", "bha"}}
                    });
                }

                result.push_back(code_action);
            }
        }

        return result;
    }

    json LSPServer::execute_analyze(const json& args) {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
        }

        std::string project_root = workspace_root_;
        if (args.contains("projectRoot")) {
            project_root = args["projectRoot"].get<std::string>();
        }

        if (project_root.starts_with("file://")) {
            project_root = project_root.substr(7);
        }

        std::optional<std::filesystem::path> build_dir;
        if (args.contains("buildDir") && !args["buildDir"].is_null()) {
            build_dir = args["buildDir"].get<std::string>();
        }

        bool rebuild = args.contains("rebuild") && args["rebuild"].get<bool>();

        AnalyzeSuggestionOptions analyze_options;
        if (args.contains("enabledTypes") && args["enabledTypes"].is_array()) {
            for (const auto& item : args["enabledTypes"]) {
                if (!item.is_string()) {
                    continue;
                }
                if (const auto parsed = parse_suggestion_type_token(item.get<std::string>()); parsed.has_value()) {
                    if (std::ranges::find(analyze_options.enabled_types, *parsed) == analyze_options.enabled_types.end()) {
                        analyze_options.enabled_types.push_back(*parsed);
                    }
                }
            }
        }
        if (args.contains("includeUnsafe") && args["includeUnsafe"].is_boolean()) {
            analyze_options.include_unsafe = args["includeUnsafe"].get<bool>();
        }
        if (args.contains("minConfidence") && args["minConfidence"].is_number()) {
            analyze_options.min_confidence = args["minConfidence"].get<double>();
        }
        if (args.contains("disableConsolidation") && args["disableConsolidation"].is_boolean()) {
            analyze_options.enable_consolidation = !args["disableConsolidation"].get<bool>();
        }
        if (args.contains("explain") && args["explain"].is_boolean() && args["explain"].get<bool>()) {
            analyze_options.relax_heuristics = true;
            analyze_options.include_unsafe = true;
            analyze_options.min_confidence = 0.0;
            analyze_options.enable_consolidation = false;
        }

        ProgressToken token = create_progress_token();

        WorkDoneProgressBegin begin_progress;
        begin_progress.title = "Analyzing project";
        begin_progress.cancellable = false;
        begin_progress.percentage = 0;
        json begin_json;
        to_json(begin_json, begin_progress);
        send_progress(token, begin_json);

        try {
            std::string analysis_id;
            std::vector<Suggestion> suggestions;
            BuildMetrics baseline_metrics;
            int files_analyzed = 0;
            int duration_ms = 0;
            {
                std::lock_guard lock(suggestion_manager_mutex_);
                auto analysis_result = suggestion_manager_->analyze_project(
                        project_root,
                        build_dir,
                        rebuild,
                        [&token](const std::string& message, int percentage) {
                            WorkDoneProgressReport report;
                            report.message = message;
                            if (percentage >= 0) {
                                report.percentage = percentage;
                            }
                            json report_json;
                            to_json(report_json, report);
                            send_progress(token, report_json);
                        },
                        analyze_options
                    );
                analysis_id = std::move(analysis_result.analysis_id);
                suggestions = std::move(analysis_result.suggestions);
                baseline_metrics = analysis_result.baseline_metrics;
                files_analyzed = analysis_result.files_analyzed;
                duration_ms = analysis_result.duration_ms;
            }

            WorkDoneProgressEnd end_progress;
            end_progress.message = "Analysis complete";
            json end_json;
            to_json(end_json, end_progress);
            send_progress(token, end_json);

            json suggestions_json = json::array();
            for (const auto& sug : suggestions) {
                json sug_json;
                to_json(sug_json, sug);
                suggestions_json.push_back(sug_json);
            }

            json metrics_json;
            to_json(metrics_json, baseline_metrics);

            json response = {
                {"analysisId", analysis_id},
                {"suggestions", suggestions_json},
                {"baselineMetrics", metrics_json},
                {"filesAnalyzed", files_analyzed},
                {"durationMs", duration_ms}
            };
            const json unreal_checks = build_unreal_environment_checks(std::filesystem::path(project_root));
            if (!unreal_checks.empty()) {
                response["unrealEnvironmentChecks"] = unreal_checks;
            }
            return response;
        } catch (const std::exception& e) {
            WorkDoneProgressEnd end_progress;
            end_progress.message = std::string("Analysis failed: ") + e.what();
            json end_json;
            to_json(end_json, end_progress);
            send_progress(token, end_json);
            throw;
        }
    }

    json LSPServer::execute_list_suggesters(const json&) const {
        json suggesters = json::array();
        for (const auto& descriptor : suggestions::list_suggester_descriptors()) {
            json supported_types = json::array();
            for (const auto type : descriptor.supported_types) {
                supported_types.push_back(suggestion_type_token(type));
            }
            json input_requirements = json::array();
            for (const auto& requirement : descriptor.input_requirements) {
                input_requirements.push_back(requirement);
            }
            suggesters.push_back({
                {"id", descriptor.id},
                {"className", descriptor.class_name},
                {"description", descriptor.description},
                {"supportedTypes", supported_types},
                {"inputRequirements", input_requirements},
                {"potentiallyAutoApplicable", descriptor.potentially_auto_applicable},
                {"supportsExplainMode", descriptor.supports_explain_mode}
            });
        }

        return {
            {"suggesters", suggesters}
        };
    }

    json LSPServer::execute_run_suggester(const json& args) {
        if (!args.contains("suggester") || !args["suggester"].is_string()) {
            throw std::runtime_error("Missing suggester");
        }

        const std::string requested = args["suggester"].get<std::string>();
        const auto descriptor = suggestions::find_suggester_descriptor(requested);
        if (!descriptor.has_value()) {
            throw std::runtime_error("Unknown suggester: " + requested);
        }

        json forwarded = args;
        json enabled_types = json::array();
        for (const auto type : descriptor->supported_types) {
            enabled_types.push_back(suggestion_type_token(type));
        }
        forwarded["enabledTypes"] = enabled_types;

        json result = execute_analyze(forwarded);
        result["requestedSuggester"] = descriptor->id;
        result["requestedSuggesterClass"] = descriptor->class_name;
        return result;
    }

    json LSPServer::execute_explain_suggestion(const json& args) const {
        if (args.contains("suggestionId") && args["suggestionId"].is_string()) {
            return execute_get_suggestion_details(args);
        }
        if (!args.contains("suggester") || !args["suggester"].is_string()) {
            throw std::runtime_error("Missing suggester");
        }

        const std::string requested = args["suggester"].get<std::string>();
        const auto descriptor = suggestions::find_suggester_descriptor(requested);
        if (!descriptor.has_value()) {
            throw std::runtime_error("Unknown suggester: " + requested);
        }

        json supported_types = json::array();
        for (const auto type : descriptor->supported_types) {
            supported_types.push_back(suggestion_type_token(type));
        }
        json input_requirements = json::array();
        for (const auto& requirement : descriptor->input_requirements) {
            input_requirements.push_back(requirement);
        }

        return {
            {"id", descriptor->id},
            {"className", descriptor->class_name},
            {"description", descriptor->description},
            {"supportedTypes", supported_types},
            {"inputRequirements", input_requirements},
            {"potentiallyAutoApplicable", descriptor->potentially_auto_applicable},
            {"supportsExplainMode", descriptor->supports_explain_mode},
            {"usage", {
                {"command", "bha.runSuggester"},
                {"arguments", {
                    {"suggester", descriptor->id},
                    {"projectRoot", "<path>"},
                    {"buildDir", "<path>"},
                    {"rebuild", false},
                    {"explain", true}
                }}
            }}
        };
    }

    json LSPServer::execute_apply_edits(const json& args) {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
        }

        if (args.contains("async") && args["async"].is_boolean() && args["async"].get<bool>()) {
            return queue_async_command("bha.applyEdits", args);
        }

        std::string project_root = workspace_root_;
        if (args.contains("projectRoot") && args["projectRoot"].is_string()) {
            project_root = args["projectRoot"].get<std::string>();
        }
        const fs::path project_root_path = project_root.empty()
            ? fs::path{}
            : (project_root.starts_with("file://") ? uri::uri_to_path(project_root) : fs::path(project_root));

        auto read_size = [](const json& item, const char* snake, const char* camel, std::size_t fallback = 0) {
            if (item.contains(snake) && item[snake].is_number_unsigned()) {
                return item[snake].get<std::size_t>();
            }
            if (item.contains(camel) && item[camel].is_number_unsigned()) {
                return item[camel].get<std::size_t>();
            }
            return fallback;
        };

        std::vector<bha::TextEdit> edits;
        auto append_edits_array = [&](const json& edit_array) {
            if (!edit_array.is_array()) {
                return;
            }
            for (const auto& item : edit_array) {
                if (!item.is_object()) {
                    continue;
                }

                std::string file;
                if (item.contains("file") && item["file"].is_string()) {
                    file = item["file"].get<std::string>();
                } else if (item.contains("path") && item["path"].is_string()) {
                    file = item["path"].get<std::string>();
                }
                if (file.empty()) {
                    continue;
                }

                bha::TextEdit edit;
                fs::path edit_file = file.starts_with("file://") ? uri::uri_to_path(file) : fs::path(file);
                if (edit_file.is_relative() && !project_root_path.empty()) {
                    edit_file = (project_root_path / edit_file).lexically_normal();
                }
                edit.file = edit_file;
                edit.start_line = read_size(item, "start_line", "startLine", 0);
                edit.start_col = read_size(item, "start_col", "startCol", 0);
                edit.end_line = read_size(item, "end_line", "endLine", edit.start_line);
                edit.end_col = read_size(item, "end_col", "endCol", edit.start_col);
                if (item.contains("new_text") && item["new_text"].is_string()) {
                    edit.new_text = item["new_text"].get<std::string>();
                } else if (item.contains("newText") && item["newText"].is_string()) {
                    edit.new_text = item["newText"].get<std::string>();
                }
                edits.push_back(std::move(edit));
            }
        };

        append_edits_array(args.value("edits", json::array()));
        append_edits_array(args.value("textEdits", json::array()));
        append_edits_array(args.value("text_edits", json::array()));
        if (args.contains("suggestions") && args["suggestions"].is_array()) {
            for (const auto& suggestion : args["suggestions"]) {
                append_edits_array(suggestion.value("edits", json::array()));
                append_edits_array(suggestion.value("textEdits", json::array()));
                append_edits_array(suggestion.value("text_edits", json::array()));
            }
        }

        if (edits.empty()) {
            throw std::runtime_error("No valid edits provided");
        }

        bool skip_rebuild = !config_.rebuild_after_apply;
        if (args.contains("skipRebuild")) {
            skip_rebuild = args["skipRebuild"].get<bool>();
        }

        ApplySuggestionResult result;
        {
            std::lock_guard lock(suggestion_manager_mutex_);
            result = suggestion_manager_->apply_edit_bundle(edits, true);
        }

        auto diagnostics_to_json = [](const std::vector<Diagnostic>& diagnostics) {
            json out = json::array();
            for (const auto& diagnostic : diagnostics) {
                json item;
                to_json(item, diagnostic);
                out.push_back(std::move(item));
            }
            return out;
        };

        const bool build_validation_requested = config_.rebuild_after_apply && !skip_rebuild;
        bool build_validation_ran = false;
        bool build_validation_success = true;
        std::vector<Diagnostic> build_errors;

        json rollback_json = {
            {"attempted", false},
            {"success", false},
            {"reason", "not-required"},
            {"restoredFiles", json::array()},
            {"errors", json::array()}
        };

        if (result.success && build_validation_requested) {
            std::optional<int> measured_rebuild_duration_ms;
            build_validation_ran = true;
            build_validation_success = run_build_validation(build_errors, measured_rebuild_duration_ms);
            if (!build_validation_success) {
                result.success = false;
                result.errors.insert(result.errors.end(), build_errors.begin(), build_errors.end());
                rollback_json["reason"] = "build-failed";

                if (!config_.rollback_on_build_failure) {
                    rollback_json["reason"] = "rollback-disabled";
                } else if (!result.backup_id.has_value() || result.backup_id->empty()) {
                    rollback_json["reason"] = "no-backup";
                } else {
                    RevertResult rollback_result;
                    {
                        std::lock_guard lock(suggestion_manager_mutex_);
                        rollback_result = suggestion_manager_->revert_changes_detailed(*result.backup_id);
                    }
                    rollback_json["attempted"] = true;
                    rollback_json["success"] = rollback_result.success;
                    rollback_json["restoredFiles"] = rollback_result.restored_files;
                    rollback_json["errors"] = diagnostics_to_json(rollback_result.errors);
                    rollback_json["reason"] = rollback_result.success ? "rollback-succeeded" : "rollback-failed";
                }
            }
        } else if (!build_validation_requested) {
            rollback_json["reason"] = "validation-skipped";
        }

        json errors_json = json::array();
        for (const auto& err : result.errors) {
            json err_json;
            to_json(err_json, err);
            errors_json.push_back(err_json);
        }

        return {
            {"success", result.success},
            {"changedFiles", result.changed_files},
            {"errors", errors_json},
            {"backupId", result.backup_id.value_or("")},
            {"buildValidation", {
                {"requested", build_validation_requested},
                {"ran", build_validation_ran},
                {"success", build_validation_success},
                {"errorCount", build_errors.size()}
            }},
            {"rollback", rollback_json},
            {"trustLoop", {
                {"available", false},
                {"reason", "manual-edits"}
            }}
        };
    }

    json LSPServer::execute_apply_suggestion(const json& args) {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
        }

        if (args.contains("async") && args["async"].is_boolean() && args["async"].get<bool>()) {
            return queue_async_command("bha.applySuggestion", args);
        }

        if (!args.contains("suggestionId")) {
            throw std::runtime_error("Missing suggestionId");
        }

        const std::string suggestion_id = args["suggestionId"].get<std::string>();
        bool skip_validation = args.contains("skipValidation") && args["skipValidation"].get<bool>();
        bool skip_rebuild = !config_.rebuild_after_apply;
        if (args.contains("skipRebuild")) {
            skip_rebuild = args["skipRebuild"].get<bool>();
        }

        std::optional<int> predicted_savings_ms;
        {
            std::lock_guard lock(suggestion_manager_mutex_);
            if (const auto suggestion = suggestion_manager_->get_suggestion(suggestion_id); suggestion.has_value()) {
                predicted_savings_ms = suggestion->estimated_impact.time_saved_ms;
            }
        }
        std::optional<int> baseline_duration_ms;
        {
            std::lock_guard lock(suggestion_manager_mutex_);
            if (const auto baseline = suggestion_manager_->get_last_baseline_metrics(); baseline.has_value()) {
                baseline_duration_ms = baseline->total_duration_ms;
            }
        }

        if (bool skip_consent = args.contains("skipConsent") && args["skipConsent"].get<bool>(); !skip_consent && config_.show_preview_before_apply && !config_.auto_apply_all) {
            DetailedSuggestion details;
            {
                std::lock_guard lock(suggestion_manager_mutex_);
                details = suggestion_manager_->get_suggestion_details(suggestion_id);
            }
            std::string consent_message = "Apply optimization: " + details.title + "?\n";
            if (!details.files_to_modify.empty()) {
                consent_message += "Files to modify: " + std::to_string(details.files_to_modify.size()) + "\n";
            }
            if (!details.files_to_create.empty()) {
                consent_message += "Files to create: " + std::to_string(details.files_to_create.size()) + "\n";
            }

            if (auto [approved, selected_action] = request_user_consent(consent_message, {"Apply", "Cancel"}); !approved) {
                return {
                    {"success", false},
                    {"changedFiles", json::array()},
                    {"errors", json::array({{{"message", "User cancelled the operation"}}})},
                    {"backupId", ""},
                    {"buildValidation", {
                        {"requested", false},
                        {"ran", false},
                        {"success", true},
                        {"errorCount", 0}
                    }},
                    {"rollback", {
                        {"attempted", false},
                        {"success", false},
                        {"reason", "not-required"},
                        {"restoredFiles", json::array()},
                        {"errors", json::array()}
                    }},
                    {"trustLoop", {
                        {"available", false},
                        {"reason", "not-applied"}
                    }}
                };
            }
        }

        ApplySuggestionResult result;
        {
            std::lock_guard lock(suggestion_manager_mutex_);
            result = suggestion_manager_->apply_suggestion(
                suggestion_id,
                skip_validation,
                true,
                true
            );
        }

        auto diagnostics_to_json = [](const std::vector<Diagnostic>& diagnostics) {
            json out = json::array();
            for (const auto& diagnostic : diagnostics) {
                json item;
                to_json(item, diagnostic);
                out.push_back(std::move(item));
            }
            return out;
        };

        const bool build_validation_requested = config_.rebuild_after_apply && !skip_rebuild;
        bool build_validation_ran = false;
        bool build_validation_success = true;
        std::optional<int> measured_rebuild_duration_ms;
        std::vector<Diagnostic> build_errors;

        json rollback_json = {
            {"attempted", false},
            {"success", false},
            {"reason", "not-required"},
            {"restoredFiles", json::array()},
            {"errors", json::array()}
        };

        if (result.success && build_validation_requested) {
            build_validation_ran = true;
            build_validation_success = run_build_validation(build_errors, measured_rebuild_duration_ms);
            if (!build_validation_success) {
                result.success = false;
                result.errors.insert(result.errors.end(), build_errors.begin(), build_errors.end());
                rollback_json["reason"] = "build-failed";

                if (!config_.rollback_on_build_failure) {
                    rollback_json["reason"] = "rollback-disabled";
                } else if (!result.backup_id.has_value() || result.backup_id->empty()) {
                    rollback_json["reason"] = "no-backup";
                } else {
                    send_notification("window/showMessage", {
                        {"type", static_cast<int>(MessageType::Warning)},
                        {"message", "Build failed after applying suggestion. Rolling back changes..."}
                    });

                    RevertResult rollback_result;
                    {
                        std::lock_guard lock(suggestion_manager_mutex_);
                        rollback_result = suggestion_manager_->revert_changes_detailed(*result.backup_id);
                    }
                    rollback_json["attempted"] = true;
                    rollback_json["success"] = rollback_result.success;
                    rollback_json["restoredFiles"] = rollback_result.restored_files;
                    rollback_json["errors"] = diagnostics_to_json(rollback_result.errors);
                    rollback_json["reason"] = rollback_result.success ? "rollback-succeeded" : "rollback-failed";

                    if (rollback_result.success) {
                        Diagnostic rollback_diag;
                        rollback_diag.severity = DiagnosticSeverity::Warning;
                        rollback_diag.source = "bha-lsp";
                        rollback_diag.message = "Build validation failed. Applied files were restored from backup.";
                        result.errors.push_back(std::move(rollback_diag));
                    } else {
                        result.errors.insert(
                            result.errors.end(),
                            rollback_result.errors.begin(),
                            rollback_result.errors.end()
                        );
                        Diagnostic rollback_diag;
                        rollback_diag.severity = DiagnosticSeverity::Error;
                        rollback_diag.source = "bha-lsp";
                        rollback_diag.message = "Build validation failed and automatic rollback did not complete.";
                        result.errors.push_back(std::move(rollback_diag));
                    }
                }
            }
        } else if (!build_validation_requested) {
            rollback_json["reason"] = "validation-skipped";
        }

        json errors_json = json::array();
        for (const auto& err : result.errors) {
            json err_json;
            to_json(err_json, err);
            errors_json.push_back(err_json);
        }

        const json trust_loop = build_trust_loop_payload(
            predicted_savings_ms,
            baseline_duration_ms,
            build_validation_ran,
            build_validation_success,
            measured_rebuild_duration_ms
        );
        persist_trust_loop_metrics(trust_loop, suggestion_id, false);

        return {
            {"success", result.success},
            {"changedFiles", result.changed_files},
            {"errors", errors_json},
            {"backupId", result.backup_id.value_or("")},
            {"buildValidation", {
                {"requested", build_validation_requested},
                {"ran", build_validation_ran},
                {"success", build_validation_success},
                {"errorCount", build_errors.size()}
            }},
            {"rollback", rollback_json},
            {"trustLoop", trust_loop}
        };
    }

    json LSPServer::execute_apply_all_suggestions(const json& args) {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
        }

        if (args.contains("async") && args["async"].is_boolean() && args["async"].get<bool>()) {
            return queue_async_command("bha.applyAllSuggestions", args);
        }

        std::optional<std::string> min_priority;
        if (args.contains("minPriority")) {
            if (args["minPriority"].is_string()) {
                min_priority = args["minPriority"].get<std::string>();
            } else if (args["minPriority"].is_number()) {
                int prio = args["minPriority"].get<int>();
                min_priority = prio == 0 ? "high" : (prio == 1 ? "medium" : "low");
            }
        }

        bool safe_only = args.contains("safeOnly") && args["safeOnly"].get<bool>();
        auto with_manager = [this](auto&& fn) -> decltype(auto) {
            std::lock_guard lock(suggestion_manager_mutex_);
            return fn();
        };

        if (bool skip_consent = args.contains("skipConsent") && args["skipConsent"].get<bool>(); !skip_consent && config_.show_preview_before_apply && !config_.auto_apply_all) {
            auto suggestions = with_manager([&]() {
                return suggestion_manager_->get_all_suggestions();
            });
            std::string consent_message = "Apply all " + std::to_string(suggestions.size()) +
                                         " optimization suggestions?";
            if (safe_only) {
                consent_message += " (safe changes only)";
            }

            if (auto [approved, selected_action] = request_user_consent(consent_message, {"Apply All", "Cancel"}); !approved) {
                return {
                    {"success", false},
                    {"appliedCount", 0},
                    {"skippedCount", 0},
                    {"failedCount", 0},
                    {"changedFiles", json::array()},
                    {"errors", json::array({{{"message", "User cancelled the operation"}}})},
                    {"backupId", ""},
                    {"buildValidation", {
                        {"requested", false},
                        {"ran", false},
                        {"success", true},
                        {"errorCount", 0}
                    }},
                    {"rollback", {
                        {"attempted", false},
                        {"success", false},
                        {"reason", "not-required"},
                        {"restoredFiles", json::array()},
                        {"errors", json::array()}
                    }},
                    {"trustLoop", {
                        {"available", false},
                        {"reason", "not-applied"}
                    }}
                };
            }
        }

        auto apply_all_result = with_manager([&]() {
            return suggestion_manager_->apply_all_suggestions(min_priority, safe_only);
        });
        bool success = apply_all_result.success;
        std::size_t applied_count = apply_all_result.applied_count;
        std::size_t skipped_count = apply_all_result.skipped_count;
        auto applied_suggestion_ids = apply_all_result.applied_suggestion_ids;
        auto changed_files = apply_all_result.changed_files;
        auto errors = apply_all_result.errors;
        const std::string backup_id = apply_all_result.backup_id;
        const bool atomic = args.contains("atomic") && args["atomic"].get<bool>();

        std::optional<int> baseline_duration_ms;
        if (const auto baseline = with_manager([&]() {
                return suggestion_manager_->get_last_baseline_metrics();
            }); baseline.has_value()) {
            baseline_duration_ms = baseline->total_duration_ms;
        }

        auto diagnostics_to_json = [](const std::vector<Diagnostic>& diagnostics) {
            json out = json::array();
            for (const auto& diagnostic : diagnostics) {
                json item;
                to_json(item, diagnostic);
                out.push_back(std::move(item));
            }
            return out;
        };

        json rollback_json = {
            {"attempted", false},
            {"success", false},
            {"reason", "not-required"},
            {"restoredFiles", json::array()},
            {"errors", json::array()}
        };

        auto append_unique_changed_files = [](std::vector<std::string>& dest, const std::vector<std::string>& src) {
            std::unordered_set<std::string> seen(dest.begin(), dest.end());
            for (const auto& file : src) {
                if (seen.insert(file).second) {
                    dest.push_back(file);
                }
            }
        };

        auto append_contextual_errors = [&errors](const std::vector<Diagnostic>& source,
                                                  const std::string& suggestion_id,
                                                  const std::string& stage) {
            for (auto diag : source) {
                if (diag.message.empty()) {
                    diag.message = "[" + suggestion_id + "][" + stage + "] apply failed";
                } else {
                    diag.message = "[" + suggestion_id + "][" + stage + "] " + diag.message;
                }
                errors.push_back(std::move(diag));
            }
        };

        if (!success && atomic && !backup_id.empty()) {
            const auto rollback_result = with_manager([&]() {
                return suggestion_manager_->revert_changes_detailed(backup_id);
            });
            rollback_json["attempted"] = true;
            rollback_json["success"] = rollback_result.success;
            rollback_json["restoredFiles"] = rollback_result.restored_files;
            rollback_json["errors"] = diagnostics_to_json(rollback_result.errors);
            rollback_json["reason"] = rollback_result.success ? "atomic-rollback-succeeded" : "atomic-rollback-failed";

            if (rollback_result.success) {
                changed_files.clear();
                applied_suggestion_ids.clear();
                applied_count = 0;
            } else {
                errors.insert(errors.end(), rollback_result.errors.begin(), rollback_result.errors.end());
            }
            success = false;
        }

        bool build_validation_requested = success && config_.rebuild_after_apply;
        bool build_validation_ran = false;
        bool build_validation_success = true;
        std::optional<int> measured_rebuild_duration_ms;
        std::vector<Diagnostic> build_errors;

        if (build_validation_requested) {
            build_validation_ran = true;
            build_validation_success = run_build_validation(build_errors, measured_rebuild_duration_ms);
            if (!build_validation_success) {
                const std::size_t error_count_before_batch_validation = errors.size();
                success = false;
                errors.insert(errors.end(), build_errors.begin(), build_errors.end());

                if (!config_.rollback_on_build_failure) {
                    rollback_json["reason"] = "rollback-disabled";
                } else if (backup_id.empty()) {
                    rollback_json["reason"] = "no-backup";
                } else if (atomic) {
                    send_notification("window/showMessage", {
                        {"type", static_cast<int>(MessageType::Warning)},
                        {"message", "Build failed after applying suggestions. Rolling back all changes..."}
                    });

                    const auto rollback_result = with_manager([&]() {
                        return suggestion_manager_->revert_changes_detailed(backup_id);
                    });
                    rollback_json["attempted"] = true;
                    rollback_json["success"] = rollback_result.success;
                    rollback_json["restoredFiles"] = rollback_result.restored_files;
                    rollback_json["errors"] = diagnostics_to_json(rollback_result.errors);
                    rollback_json["reason"] = rollback_result.success ? "rollback-succeeded" : "rollback-failed";

                    if (rollback_result.success) {
                        changed_files.clear();
                        applied_suggestion_ids.clear();
                        applied_count = 0;
                    } else {
                        errors.insert(errors.end(), rollback_result.errors.begin(), rollback_result.errors.end());
                    }
                } else {
                    // Remove the initial aggregate build-failure diagnostics before
                    // entering fault isolation; candidate-specific failures are added back
                    // with suggestion context.
                    errors.resize(error_count_before_batch_validation);

                    send_notification("window/showMessage", {
                        {"type", static_cast<int>(MessageType::Warning)},
                        {"message", "Build failed after applying suggestions. Running fault isolation to keep valid edits..."}
                    });

                    const auto rollback_result = with_manager([&]() {
                        return suggestion_manager_->revert_changes_detailed(backup_id);
                    });
                    rollback_json["attempted"] = true;
                    rollback_json["success"] = rollback_result.success;
                    rollback_json["restoredFiles"] = rollback_result.restored_files;
                    rollback_json["errors"] = diagnostics_to_json(rollback_result.errors);
                    rollback_json["reason"] = rollback_result.success ? "fault-isolation-started" : "rollback-failed";

                    if (!rollback_result.success) {
                        errors.insert(errors.end(), rollback_result.errors.begin(), rollback_result.errors.end());
                    } else {
                        std::vector<std::string> retained_ids;
                        std::vector<std::string> retained_changed_files;
                        bool isolation_aborted = false;

                        for (const auto& suggestion_id : applied_suggestion_ids) {
                            for (const auto& retained_id : retained_ids) {
                                auto retained_apply = with_manager([&]() {
                                    return suggestion_manager_->apply_suggestion(
                                        retained_id,
                                        false,
                                        true,
                                        false
                                    );
                                });
                                if (!retained_apply.success) {
                                    append_contextual_errors(retained_apply.errors, retained_id, "fault-isolation-replay");
                                    isolation_aborted = true;
                                    break;
                                }
                            }
                            if (isolation_aborted) {
                                break;
                            }

                            const auto candidate_apply = with_manager([&]() {
                                return suggestion_manager_->apply_suggestion(
                                    suggestion_id,
                                    false,
                                    true,
                                    false
                                );
                            });
                            if (!candidate_apply.success) {
                                append_contextual_errors(candidate_apply.errors, suggestion_id, "fault-isolation-apply");
                            } else {
                                std::vector<Diagnostic> candidate_build_errors;
                                std::optional<int> candidate_duration_ms;
                                const bool candidate_valid = run_build_validation(candidate_build_errors, candidate_duration_ms);
                                if (candidate_valid) {
                                    retained_ids.push_back(suggestion_id);
                                    append_unique_changed_files(retained_changed_files, candidate_apply.changed_files);
                                    measured_rebuild_duration_ms = candidate_duration_ms;
                                } else {
                                    append_contextual_errors(candidate_build_errors, suggestion_id, "fault-isolation-build");
                                }
                            }

                            const auto reset_result = with_manager([&]() {
                                return suggestion_manager_->revert_changes_detailed(backup_id);
                            });
                            if (!reset_result.success) {
                                rollback_json["success"] = false;
                                rollback_json["reason"] = "fault-isolation-reset-failed";
                                rollback_json["restoredFiles"] = reset_result.restored_files;
                                rollback_json["errors"] = diagnostics_to_json(reset_result.errors);
                                errors.insert(errors.end(), reset_result.errors.begin(), reset_result.errors.end());
                                isolation_aborted = true;
                                break;
                            }
                        }

                        if (isolation_aborted) {
                            success = false;
                            changed_files.clear();
                            applied_suggestion_ids.clear();
                            applied_count = 0;
                        } else {
                            for (const auto& retained_id : retained_ids) {
                                auto retained_apply = with_manager([&]() {
                                    return suggestion_manager_->apply_suggestion(
                                        retained_id,
                                        false,
                                        true,
                                        false
                                    );
                                });
                                if (!retained_apply.success) {
                                    append_contextual_errors(retained_apply.errors, retained_id, "fault-isolation-final-replay");
                                    isolation_aborted = true;
                                    break;
                                }
                            }

                            if (isolation_aborted) {
                                success = false;
                                rollback_json["reason"] = "fault-isolation-final-replay-failed";
                                changed_files.clear();
                                applied_suggestion_ids.clear();
                                applied_count = 0;
                            } else {
                                build_errors.clear();
                                build_validation_ran = true;
                                build_validation_success = run_build_validation(build_errors, measured_rebuild_duration_ms);

                                if (!build_validation_success) {
                                    append_contextual_errors(build_errors, "apply-all", "fault-isolation-final-build");
                                    success = false;

                                    const auto final_reset = with_manager([&]() {
                                        return suggestion_manager_->revert_changes_detailed(backup_id);
                                    });
                                    rollback_json["attempted"] = true;
                                    rollback_json["success"] = final_reset.success;
                                    rollback_json["restoredFiles"] = final_reset.restored_files;
                                    rollback_json["errors"] = diagnostics_to_json(final_reset.errors);
                                    rollback_json["reason"] = final_reset.success
                                        ? "fault-isolation-final-build-failed-rolled-back"
                                        : "fault-isolation-final-build-failed-rollback-failed";
                                    if (final_reset.success) {
                                        changed_files.clear();
                                        applied_suggestion_ids.clear();
                                        applied_count = 0;
                                    } else {
                                        errors.insert(errors.end(), final_reset.errors.begin(), final_reset.errors.end());
                                    }
                                } else {
                                    const std::size_t previous_applied_count = applied_count;
                                    applied_suggestion_ids = retained_ids;
                                    applied_count = retained_ids.size();
                                    skipped_count += previous_applied_count - applied_count;
                                    changed_files = std::move(retained_changed_files);
                                    success = errors.empty() && applied_count > 0;
                                    rollback_json["reason"] = applied_count > 0
                                        ? "fault-isolation-partial-apply"
                                        : "fault-isolation-no-survivors";
                                }
                            }
                        }
                    }
                }
            } else if (rollback_json["reason"] == "not-required") {
                rollback_json["reason"] = "build-succeeded";
            }
        } else if (rollback_json["reason"] == "not-required") {
            rollback_json["reason"] = config_.rebuild_after_apply ? "validation-skipped" : "validation-disabled";
        }

        std::optional<int> predicted_savings_ms;
        int predicted_savings_sum = 0;
        bool has_predicted_savings = false;
        for (const auto& suggestion_id : applied_suggestion_ids) {
            if (const auto suggestion = with_manager([&]() {
                    return suggestion_manager_->get_suggestion(suggestion_id);
                }); suggestion.has_value()) {
                predicted_savings_sum += suggestion->estimated_impact.time_saved_ms;
                has_predicted_savings = true;
            }
        }
        if (has_predicted_savings) {
            predicted_savings_ms = predicted_savings_sum;
        }

        json errors_json = json::array();
        for (const auto& err : errors) {
            json err_json;
            to_json(err_json, err);
            errors_json.push_back(err_json);
        }

        const json trust_loop = build_trust_loop_payload(
            predicted_savings_ms,
            baseline_duration_ms,
            build_validation_ran,
            build_validation_success,
            measured_rebuild_duration_ms
        );
        persist_trust_loop_metrics(trust_loop, std::nullopt, true);

        return {
            {"success", success},
            {"appliedCount", applied_count},
            {"skippedCount", skipped_count},
            {"failedCount", errors.size()},
            {"appliedSuggestionIds", applied_suggestion_ids},
            {"changedFiles", changed_files},
            {"errors", errors_json},
            {"backupId", backup_id},
            {"buildValidation", {
                {"requested", build_validation_requested},
                {"ran", build_validation_ran},
                {"success", build_validation_success},
                {"errorCount", build_errors.size()}
            }},
            {"rollback", rollback_json},
            {"trustLoop", trust_loop}
        };
    }

    json LSPServer::execute_get_job_status(const json& args) const {
        if (!args.contains("jobId") || !args["jobId"].is_string()) {
            throw std::runtime_error("Missing jobId");
        }

        const std::string job_id = args["jobId"].get<std::string>();
        std::lock_guard lock(async_jobs_mutex_);
        const auto it = async_jobs_.find(job_id);
        if (it == async_jobs_.end()) {
            return {
                {"found", false},
                {"jobId", job_id}
            };
        }
        json payload = build_async_job_payload(*it->second);
        payload["found"] = true;
        return payload;
    }

    json LSPServer::execute_cancel_job(const json& args) {
        if (!args.contains("jobId") || !args["jobId"].is_string()) {
            throw std::runtime_error("Missing jobId");
        }

        const std::string job_id = args["jobId"].get<std::string>();
        std::shared_ptr<AsyncJob> job;
        {
            std::lock_guard lock(async_jobs_mutex_);
            const auto it = async_jobs_.find(job_id);
            if (it == async_jobs_.end()) {
                return {
                    {"found", false},
                    {"jobId", job_id},
                    {"cancelRequested", false}
                };
            }
            job = it->second;
            job->cancel_requested.store(true);
        }

        send_notification("bha/jobCancellationRequested", {
            {"jobId", job_id},
            {"status", async_job_status_to_string(job->status.load())}
        });

        return {
            {"found", true},
            {"jobId", job_id},
            {"cancelRequested", true},
            {"status", async_job_status_to_string(job->status.load())}
        };
    }

    json LSPServer::execute_revert_changes(const json& args) const
    {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
        }

        if (!args.contains("backupId")) {
            throw std::runtime_error("Missing backupId");
        }

        const std::string backup_id = args["backupId"].get<std::string>();
        RevertResult revert_result;
        {
            std::lock_guard lock(suggestion_manager_mutex_);
            revert_result = suggestion_manager_->revert_changes_detailed(backup_id);
        }
        auto [success, restored_files, errors] = revert_result;

        json errors_json = json::array();
        for (const auto& err : errors) {
            json err_json;
            to_json(err_json, err);
            errors_json.push_back(err_json);
        }

        return {
            {"success", success},
            {"restoredFiles", restored_files},
            {"errors", errors_json}
        };
    }

    json LSPServer::execute_get_suggestion_details(const json& args) const
    {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
        }

        if (!args.contains("suggestionId")) {
            throw std::runtime_error("Missing suggestionId");
        }

        const std::string suggestion_id = args["suggestionId"].get<std::string>();
        DetailedSuggestion details;
        {
            std::lock_guard lock(suggestion_manager_mutex_);
            details = suggestion_manager_->get_suggestion_details(suggestion_id);
        }

        json result;
        to_json(result, static_cast<const Suggestion&>(details));
        result["rationale"] = details.rationale;
        result["filesToCreate"] = details.files_to_create;
        result["filesToModify"] = details.files_to_modify;
        result["dependencies"] = details.dependencies;
        if (details.application_summary) {
            result["applicationSummary"] = *details.application_summary;
        }
        if (details.application_guidance) {
            result["applicationGuidance"] = *details.application_guidance;
        }
        if (details.auto_apply_blocked_reason) {
            result["autoApplyBlockedReason"] = *details.auto_apply_blocked_reason;
        }
        json text_edits = json::array();
        const bha::Suggestion* bha_suggestion = nullptr;
        {
            std::lock_guard lock(suggestion_manager_mutex_);
            bha_suggestion = suggestion_manager_->get_bha_suggestion(suggestion_id);
        }
        if (bha_suggestion != nullptr) {
            for (const auto& edit : bha_suggestion->edits) {
                json edit_json;
                edit_json["file"] = edit.file.string();
                edit_json["startLine"] = edit.start_line;
                edit_json["startCol"] = edit.start_col;
                edit_json["endLine"] = edit.end_line;
                edit_json["endCol"] = edit.end_col;
                edit_json["newText"] = edit.new_text;
                text_edits.push_back(edit_json);
            }
        }
        result["textEdits"] = text_edits;
        result["text_edits"] = text_edits;

        return result;
    }

    json LSPServer::execute_show_metrics(const json&) const
    {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
        }

        std::vector<Suggestion> suggestions;
        {
            std::lock_guard lock(suggestion_manager_mutex_);
            suggestions = suggestion_manager_->get_all_suggestions();
        }

        json suggestions_json = json::array();
        for (const auto& sug : suggestions) {
            json sug_json;
            to_json(sug_json, sug);
            suggestions_json.push_back(sug_json);
        }

        return {
            {"suggestions", suggestions_json}
        };
    }

    void LSPServer::persist_trust_loop_metrics(
        const json& trust_loop,
        const std::optional<std::string>& suggestion_id,
        const bool apply_all
    ) const {
        if (!config_.persist_trust_loop) {
            return;
        }

        const auto workspace_path = workspace_path_from_uri_or_path(workspace_root_);
        if (!workspace_path.has_value()) {
            return;
        }

        std::filesystem::path output_dir(config_.backup_directory);
        if (output_dir.empty()) {
            return;
        }
        if (output_dir.is_relative()) {
            output_dir = *workspace_path / output_dir;
        }

        std::error_code ec;
        std::filesystem::create_directories(output_dir, ec);
        if (ec) {
            return;
        }

        const std::filesystem::path log_path = output_dir / "trust_loop.jsonl";
        std::ofstream out(log_path, std::ios::app);
        if (!out) {
            return;
        }

        json record = {
            {"timestamp", utc_timestamp_iso8601()},
            {"applyAll", apply_all},
            {"workspaceRoot", workspace_path->string()},
            {"trustLoop", trust_loop}
        };
        if (suggestion_id.has_value() && !suggestion_id->empty()) {
            record["suggestionId"] = *suggestion_id;
        }
        out << record.dump() << "\n";
    }

    bool LSPServer::run_build_validation(
        std::vector<Diagnostic>& errors,
        std::optional<int>& measured_duration_ms
    ) const
    {
        std::optional<std::filesystem::path> workspace_path;
        if (!workspace_root_.empty()) {
            std::string root = workspace_root_;
            if (root.starts_with("file://")) {
                root = root.substr(7);
            }
            if (!root.empty()) {
                workspace_path = std::filesystem::path(root);
            }
        }

        std::string build_cmd = config_.build_command;
        if (build_cmd.empty() && workspace_path.has_value()) {
            auto& registry = build_systems::BuildSystemRegistry::instance();
            if (auto* adapter = registry.detect(*workspace_path); adapter != nullptr) {
                send_notification("window/logMessage", {
                    {"type", static_cast<int>(MessageType::Info)},
                    {"message", "Running build validation via adapter: " + adapter->name()}
                });

                build_systems::BuildOptions options;
                options.build_type = "Development";
                options.enable_tracing = false;
                options.enable_memory_profiling = false;
                options.clean_first = false;
                options.verbose = false;

                const auto started = std::chrono::steady_clock::now();
                auto build_result = adapter->build(*workspace_path, options);
                const auto ended = std::chrono::steady_clock::now();
                measured_duration_ms = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(ended - started).count()
                );

                if (build_result.is_err()) {
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.source = "bha-lsp";
                    diag.message = "Build adapter failed: " + build_result.error().message();
                    errors.push_back(std::move(diag));
                    return false;
                }

                const auto& build = build_result.value();
                if (build.success) {
                    return true;
                }

                std::regex error_regex(R"(([^:]+):(\d+):(\d+):\s*(error|warning):\s*(.*))");
                std::smatch match;
                auto search_start = build.output.cbegin();
                while (std::regex_search(search_start, build.output.cend(), match, error_regex)) {
                    Diagnostic diag;
                    diag.range.start.line = std::stoi(match[2]) - 1;
                    diag.range.start.character = std::stoi(match[3]) - 1;
                    diag.range.end = diag.range.start;
                    diag.severity = (match[4] == "error") ? DiagnosticSeverity::Error : DiagnosticSeverity::Warning;
                    diag.message = match[5];
                    diag.source = "compiler";
                    errors.push_back(std::move(diag));
                    search_start = match.suffix().first;
                }

                if (errors.empty()) {
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.source = "bha-lsp";
                    if (!build.error_message.empty()) {
                        diag.message = build.error_message;
                    } else {
                        diag.message = "Build failed via adapter: " + adapter->name();
                    }
                    errors.push_back(std::move(diag));
                }
                return false;
            }
            build_cmd = detect_build_command(*workspace_path);
        }

        if (build_cmd.empty()) {
            measured_duration_ms = 0;
            return true;
        }

        std::string effective_build_cmd = build_cmd;
#ifndef _WIN32
        if (config_.build_timeout_seconds > 0) {
            if (std::system("command -v timeout >/dev/null 2>&1") == 0) {
                effective_build_cmd = "timeout --signal=TERM " +
                    std::to_string(config_.build_timeout_seconds) +
                    "s /bin/bash -lc " + shell_quote_for_shell(build_cmd);
            }
        }
#endif

        send_notification("window/logMessage", {
            {"type", static_cast<int>(MessageType::Info)},
            {"message", "Running build validation: " + build_cmd}
        });

        std::array<char, 4096> buffer;
        std::string output;

        const auto start = std::chrono::steady_clock::now();
        FILE* pipe = popen((effective_build_cmd + " 2>&1").c_str(), "r");
        if (!pipe) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Failed to execute build command";
            errors.push_back(diag);
            return false;
        }

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int raw_status = pclose(pipe);
        const auto end = std::chrono::steady_clock::now();
        measured_duration_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
        );
        int exit_code = raw_status;
#ifndef _WIN32
        if (WIFEXITED(raw_status)) {
            exit_code = WEXITSTATUS(raw_status);
        }
#endif
        bool success = (exit_code == 0);

        if (!success) {
            std::regex error_regex(R"(([^:]+):(\d+):(\d+):\s*(error|warning):\s*(.*))");
            std::smatch match;
            auto search_start = output.cbegin();

            while (std::regex_search(search_start, output.cend(), match, error_regex)) {
                Diagnostic diag;
                diag.range.start.line = std::stoi(match[2]) - 1;
                diag.range.start.character = std::stoi(match[3]) - 1;
                diag.range.end = diag.range.start;
                diag.severity = (match[4] == "error") ? DiagnosticSeverity::Error : DiagnosticSeverity::Warning;
                diag.message = match[5];
                diag.source = "compiler";
                errors.push_back(diag);
                search_start = match.suffix().first;
            }

            if (errors.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                if (config_.build_timeout_seconds > 0 && exit_code == 124) {
                    diag.message = "Build validation timed out after " +
                        std::to_string(config_.build_timeout_seconds) + " seconds";
                } else {
                    diag.message = "Build failed with exit code " + std::to_string(exit_code);
                }
                errors.push_back(diag);
            }
        }

        return success;
    }

    std::string LSPServer::detect_build_command(const std::filesystem::path& project_root) {
        namespace fs = std::filesystem;

        bool has_unreal_markers = false;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(project_root, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_regular_file() && entry.path().extension() == ".uproject") {
                has_unreal_markers = true;
                break;
            }
        }
        if (!has_unreal_markers && fs::exists(project_root / "Source")) {
            std::size_t scanned = 0;
            for (const auto& entry : fs::recursive_directory_iterator(project_root / "Source", ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (++scanned > 4000) {
                    break;
                }
                const std::string name = entry.path().filename().string();
                if (name.ends_with(".Build.cs") || name.ends_with(".Target.cs")) {
                    has_unreal_markers = true;
                    break;
                }
            }
        }
        if (has_unreal_markers) {
            return "bha build --build-system unreal";
        }

        if (fs::exists(project_root / "CMakeLists.txt") ||
            fs::exists(project_root / "build" / "build.ninja") ||
            fs::exists(project_root / "build" / "Makefile") ||
            fs::exists(project_root / "meson.build") ||
            fs::exists(project_root / "Makefile") ||
            fs::exists(project_root / "WORKSPACE") ||
            fs::exists(project_root / "WORKSPACE.bazel")) {
            return "bha build --build-dir " + (project_root / "build").string();
        }

        return "bha build";
    }
}
