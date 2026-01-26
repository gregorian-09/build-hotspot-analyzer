#include "bha/lsp/server.hpp"
#include "bha/lsp/suggestion_manager.hpp"
#include "bha/build_systems/adapter.hpp"
#include <iostream>
#include <memory>

int main() {
    try {
        bha::build_systems::register_all_adapters();

        bha::lsp::LSPServer server;
        auto suggestion_mgr = std::make_shared<bha::lsp::SuggestionManager>();

        server.register_request_handler("workspace/executeCommand",
            [&suggestion_mgr](const nlohmann::json& params) -> nlohmann::json {
                std::string command = params["command"].get<std::string>();

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

                    auto result = suggestion_mgr->analyze_project(project_root, build_dir, rebuild);

                    nlohmann::json response;
                    response["analysisId"] = result.analysis_id;
                    response["suggestions"] = result.suggestions;
                    response["baselineMetrics"] = result.baseline_metrics;
                    response["filesAnalyzed"] = result.files_analyzed;
                    response["duration"] = result.duration_ms;
                    return response;
                }
                else if (command == "bha.getSuggestionDetails") {
                    std::string suggestion_id;
                    if (params.contains("arguments") && !params["arguments"].empty()) {
                        auto& arg = params["arguments"][0];
                        if (arg.is_string()) {
                            suggestion_id = arg.get<std::string>();
                        } else if (arg.contains("suggestionId")) {
                            suggestion_id = arg["suggestionId"].get<std::string>();
                        }
                    }

                    auto detailed = suggestion_mgr->get_suggestion_details(suggestion_id);

                    nlohmann::json response;
                    response["suggestion"] = static_cast<const bha::lsp::Suggestion&>(detailed);
                    response["rationale"] = detailed.rationale;
                    response["filesToCreate"] = detailed.files_to_create;
                    response["filesToModify"] = detailed.files_to_modify;
                    response["dependencies"] = detailed.dependencies;
                    return response;
                }
                else if (command == "bha.applySuggestion") {
                    auto args = params["arguments"][0];
                    std::string suggestion_id = args["suggestionId"].get<std::string>();

                    auto result = suggestion_mgr->apply_suggestion(suggestion_id);

                    nlohmann::json response;
                    response["success"] = result.success;
                    response["changedFiles"] = result.changed_files;
                    response["errors"] = result.errors;
                    return response;
                }
                else if (command == "bha.showMetrics") {
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
