#include "bha/lsp/server.hpp"
#include <sstream>
#include <iostream>
#include <cstring>
#include <array>
#include <cstdio>
#include <regex>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace bha::lsp
{
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

    void LSPServer::run() {
        while (running_) {
            try {
                if (std::string message = read_message(); !message.empty()) {
                    handle_message(message);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error processing message: " << e.what() << std::endl;
            }
        }
    }

    void LSPServer::stop() {
        running_ = false;
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
            workspace_root_ = params["rootUri"].get<std::string>();
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
                    "bha.applyAllSuggestions",
                    "bha.getSuggestionDetails",
                    "bha.revertChanges",
                    "bha.showMetrics"
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
                        {"autoRollback", true}
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
        if (command == "bha.applyAllSuggestions") {
            return execute_apply_all_suggestions(args);
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

        auto suggestions = suggestion_manager_->get_all_suggestions();
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

        ProgressToken token = create_progress_token();

        WorkDoneProgressBegin begin_progress;
        begin_progress.title = "Analyzing project";
        begin_progress.cancellable = false;
        begin_progress.percentage = 0;
        json begin_json;
        to_json(begin_json, begin_progress);
        send_progress(token, begin_json);

        try {
            auto [analysis_id, suggestions, baseline_metrics, files_analyzed, duration_ms] = suggestion_manager_->analyze_project(
                project_root,
                build_dir,
                rebuild,
                [this, &token](const std::string& message, int percentage) {
                    WorkDoneProgressReport report;
                    report.message = message;
                    if (percentage >= 0) {
                        report.percentage = percentage;
                    }
                    json report_json;
                    to_json(report_json, report);
                    send_progress(token, report_json);
                }
            );

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

            return {
                {"analysisId", analysis_id},
                {"suggestions", suggestions_json},
                {"baselineMetrics", metrics_json},
                {"filesAnalyzed", files_analyzed},
                {"durationMs", duration_ms}
            };
        } catch (const std::exception& e) {
            WorkDoneProgressEnd end_progress;
            end_progress.message = std::string("Analysis failed: ") + e.what();
            json end_json;
            to_json(end_json, end_progress);
            send_progress(token, end_json);
            throw;
        }
    }

    json LSPServer::execute_apply_suggestion(const json& args) {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
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

        if (bool skip_consent = args.contains("skipConsent") && args["skipConsent"].get<bool>(); !skip_consent && config_.show_preview_before_apply && !config_.auto_apply_all) {
            auto details = suggestion_manager_->get_suggestion_details(suggestion_id);
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
                    {"backupId", ""}
                };
            }
        }

        auto result = suggestion_manager_->apply_suggestion(
            suggestion_id,
            skip_validation,
            skip_rebuild,
            true
        );

        if (result.success && config_.rebuild_after_apply && !skip_rebuild) {
            std::vector<Diagnostic> build_errors;

            if (bool build_success = run_build_validation(build_errors); !build_success && config_.rollback_on_build_failure && result.backup_id) {
                send_notification("window/showMessage", {
                    {"type", static_cast<int>(MessageType::Warning)},
                    {"message", "Build failed after applying suggestion. Rolling back changes..."}
                });

                suggestion_manager_->revert_changes(*result.backup_id);

                result.success = false;
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "Build failed after applying suggestion. Changes rolled back.";
                result.errors.push_back(diag);
                result.errors.insert(result.errors.end(), build_errors.begin(), build_errors.end());
            }
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
            {"backupId", result.backup_id.value_or("")}
        };
    }

    json LSPServer::execute_apply_all_suggestions(const json& args) {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
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

        if (bool skip_consent = args.contains("skipConsent") && args["skipConsent"].get<bool>(); !skip_consent && config_.show_preview_before_apply && !config_.auto_apply_all) {
            auto suggestions = suggestion_manager_->get_all_suggestions();
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
                    {"backupId", ""}
                };
            }
        }

        auto [success, applied_count, skipped_count, changed_files, errors, backup_id] = suggestion_manager_->apply_all_suggestions(min_priority, safe_only);

        if (success && config_.rebuild_after_apply) {
            std::vector<Diagnostic> build_errors;

            if (bool build_success = run_build_validation(build_errors); !build_success && config_.rollback_on_build_failure && !backup_id.empty()) {
                send_notification("window/showMessage", {
                    {"type", static_cast<int>(MessageType::Warning)},
                    {"message", "Build failed after applying suggestions. Rolling back all changes..."}
                });

                suggestion_manager_->revert_changes(backup_id);

                success = false;
                errors.insert(errors.end(), build_errors.begin(), build_errors.end());
            }
        }

        json errors_json = json::array();
        for (const auto& err : errors) {
            json err_json;
            to_json(err_json, err);
            errors_json.push_back(err_json);
        }

        return {
            {"success", success},
            {"appliedCount", applied_count},
            {"skippedCount", skipped_count},
            {"failedCount", errors.size()},
            {"changedFiles", changed_files},
            {"errors", errors_json},
            {"backupId", backup_id}
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
        auto [success, restored_files, errors] = suggestion_manager_->revert_changes_detailed(backup_id);

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
        auto details = suggestion_manager_->get_suggestion_details(suggestion_id);

        json result;
        to_json(result, static_cast<const Suggestion&>(details));
        result["rationale"] = details.rationale;
        result["filesToCreate"] = details.files_to_create;
        result["filesToModify"] = details.files_to_modify;
        result["dependencies"] = details.dependencies;

        return result;
    }

    json LSPServer::execute_show_metrics(const json&) const
    {
        if (!suggestion_manager_) {
            throw std::runtime_error("Server not fully initialized");
        }

        auto suggestions = suggestion_manager_->get_all_suggestions();

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

    bool LSPServer::run_build_validation(std::vector<Diagnostic>& errors) const
    {
        std::string build_cmd = config_.build_command;

        if (build_cmd.empty() && !workspace_root_.empty()) {
            std::string root = workspace_root_;
            if (root.starts_with("file://")) {
                root = root.substr(7);
            }
            build_cmd = detect_build_command(root);
        }

        if (build_cmd.empty()) {
            return true;
        }

        send_notification("window/logMessage", {
            {"type", static_cast<int>(MessageType::Info)},
            {"message", "Running build validation: " + build_cmd}
        });

        std::array<char, 4096> buffer;
        std::string output;

        FILE* pipe = popen((build_cmd + " 2>&1").c_str(), "r");
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

        int exit_code = pclose(pipe);
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
                diag.message = "Build failed with exit code " + std::to_string(exit_code);
                errors.push_back(diag);
            }
        }

        return success;
    }

    std::string LSPServer::detect_build_command(const std::filesystem::path& project_root) {
        namespace fs = std::filesystem;

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
