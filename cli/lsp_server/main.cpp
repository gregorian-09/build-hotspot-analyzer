#include "bha/lsp/server.hpp"
#include "bha/lsp/suggestion_manager.hpp"
#include "bha/build_systems/adapter.hpp"
#include <iostream>
#include <memory>
#include <atomic>

int main() {
    try {
        bha::build_systems::register_all_adapters();

        bha::lsp::LSPServer server;
        auto suggestion_mgr = std::make_shared<bha::lsp::SuggestionManager>();
        std::atomic<int> progress_token_counter{0};

        server.register_request_handler("workspace/executeCommand",
            [&server, &suggestion_mgr, &progress_token_counter](const nlohmann::json& params) -> nlohmann::json {
                auto command = params["command"].get<std::string>();

                if (command == "bha.analyze") {
                    auto args = params.contains("arguments") && !params["arguments"].empty()
                                   ? params["arguments"][0]
                                   : nlohmann::json::object();

                    std::string project_root = args.value("projectRoot", ".");
                    bool rebuild = args.value("rebuild", false);

                    std::optional<std::filesystem::path> build_dir;
                    if (args.contains("buildDir")) {
                        build_dir = args["buildDir"].get<std::string>();
                    }

                    std::string token = "bha-analyze-" + std::to_string(++progress_token_counter);

                    server.send_notification("$/progress", {
                        {"token", token},
                        {"value", {
                            {"kind", "begin"},
                            {"title", "Build Hotspot Analysis"},
                            {"cancellable", false},
                            {"percentage", 0}
                        }}
                    });

                    auto on_progress = [&server, &token](const std::string& message, int percentage) {
                        server.send_notification("$/progress", {
                            {"token", token},
                            {"value", {
                                {"kind", "report"},
                                {"message", message},
                                {"percentage", percentage}
                            }}
                        });
                    };

                    try {
                        auto [analysis_id, suggestions, baseline_metrics, files_analyzed, duration_ms] = suggestion_mgr->analyze_project(project_root, build_dir, rebuild, on_progress);

                        server.send_notification("$/progress", {
                            {"token", token},
                            {"value", {
                                {"kind", "end"},
                                {"message", "Analysis complete"}
                            }}
                        });

                        nlohmann::json response;
                        response["analysisId"] = analysis_id;
                        response["suggestions"] = suggestions;
                        response["baselineMetrics"] = baseline_metrics;
                        response["filesAnalyzed"] = files_analyzed;
                        response["duration"] = duration_ms;
                        return response;
                    } catch (...) {
                        // Send progress end on error
                        server.send_notification("$/progress", {
                            {"token", token},
                            {"value", {
                                {"kind", "end"},
                                {"message", "Analysis failed"}
                            }}
                        });
                        throw;
                    }
                }
                if (command == "bha.getSuggestionDetails") {
                    std::string suggestion_id;
                    if (params.contains("arguments") && !params["arguments"].empty()) {
                        if (auto& arg = params["arguments"][0]; arg.is_string()) {
                            suggestion_id = arg.get<std::string>();
                        } else if (arg.contains("suggestionId")) {
                            suggestion_id = arg["suggestionId"].get<std::string>();
                        }
                    }

                    auto detailed = suggestion_mgr->get_suggestion_details(suggestion_id);

                    nlohmann::json response;
                    response["suggestion"] = detailed;
                    response["rationale"] = detailed.rationale;
                    response["filesToCreate"] = detailed.files_to_create;
                    response["filesToModify"] = detailed.files_to_modify;
                    response["dependencies"] = detailed.dependencies;
                    return response;
                }
                if (command == "bha.applySuggestion") {
                    auto args = params["arguments"][0];
                    auto suggestion_id = args["suggestionId"].get<std::string>();

                    auto result = suggestion_mgr->apply_suggestion(suggestion_id);

                    nlohmann::json response;
                    response["success"] = result.success;
                    response["changedFiles"] = result.changed_files;
                    response["errors"] = result.errors;
                    return response;
                }
                if (command == "bha.applyAllSuggestions") {
                    auto args = params.contains("arguments") && !params["arguments"].empty()
                                    ? params["arguments"][0]
                                    : nlohmann::json::object();

                    // Optional: filter by priority or type
                    std::optional<std::string> min_priority;
                    if (args.contains("minPriority")) {
                        min_priority = args["minPriority"].get<std::string>();
                    }

                    bool safe_only = args.value("safeOnly", true);

                    auto [success, applied_count, skipped_count, changed_files, errors, backup_id] = suggestion_mgr->apply_all_suggestions(min_priority, safe_only);

                    nlohmann::json response;
                    response["success"] = success;
                    response["appliedCount"] = applied_count;
                    response["skippedCount"] = skipped_count;
                    response["changedFiles"] = changed_files;
                    response["errors"] = errors;
                    if (!backup_id.empty()) {
                        response["backupId"] = backup_id;
                    }
                    return response;
                }
                if (command == "bha.revertChanges") {
                    auto args = params["arguments"][0];
                    auto backup_id = args["backupId"].get<std::string>();

                    auto [success, restored_files, errors] = suggestion_mgr->revert_changes_detailed(backup_id);

                    nlohmann::json response;
                    response["success"] = success;
                    response["restoredFiles"] = restored_files;
                    response["errors"] = errors;
                    return response;
                }
                if (command == "bha.showMetrics") {
                    auto suggestions = suggestion_mgr->get_all_suggestions();
                    nlohmann::json result;
                    result["suggestions"] = suggestions;
                    return result;
                }

                throw std::runtime_error("Unknown command: " + command);
            }
        );

        std::cerr << "BHA LSP Server starting..." << std::endl;
        server.run();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
