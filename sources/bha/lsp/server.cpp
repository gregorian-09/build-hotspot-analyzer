#include "bha/lsp/server.hpp"
#include <sstream>
#include <iostream>
#include <cstring>

namespace bha::lsp
{
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

    void LSPServer::handle_message(const std::string& message) {
        try {
            if (json j = json::parse(message); j.contains("id")) {
                if (j.contains("method")) {
                    auto request = j.get<RequestMessage>();
                    handle_request(request);
                } else {
                    // This is a response to a request sent (client->server response)
                    // Currently not handled, but could be used for async operations
                    auto response = j.get<ResponseMessage>();
                    (void)response;  // Suppress unused warning
                }
            } else {
                auto notification = j.get<NotificationMessage>();
                handle_notification(notification);
            }
        } catch (const json::exception& e) {
            // Send ParseError response per JSON-RPC spec
            // Use a synthetic ID since request can't be parsed
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            send_error(0, ErrorCode::ParseError,
                      std::string("JSON parse error: ") + e.what());
        }
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
}
