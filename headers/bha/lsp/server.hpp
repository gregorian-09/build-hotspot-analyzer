#pragma once

#include "protocol.hpp"
#include "types.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <iostream>

namespace bha::lsp
{
    class LSPServer {
    public:
        using RequestHandler = std::function<json(const json& params)>;
        using NotificationHandler = std::function<void(const json& params)>;

        LSPServer();
        ~LSPServer() = default;

        void run();
        void stop();

        void register_request_handler(const std::string& method, RequestHandler handler);
        void register_notification_handler(const std::string& method, NotificationHandler handler);

        void send_notification(const std::string& method, const json& params);
        void send_response(const RequestId& id, const json& result);
        void send_error(const RequestId& id, ErrorCode code, const std::string& message,
                       const std::optional<json>& data = std::nullopt);

    private:
        void handle_message(const std::string& message);
        void handle_request(const RequestMessage& request);
        void handle_notification(const NotificationMessage& notification);

        std::string read_message();
        void write_message(const std::string& content);

        void initialize_handlers();
        json handle_initialize(const json& params);
        void handle_initialized(const json& params);
        json handle_shutdown(const json& params);
        void handle_exit(const json& params);

        std::map<std::string, RequestHandler> request_handlers_;
        std::map<std::string, NotificationHandler> notification_handlers_;

        bool initialized_{false};
        bool shutdown_requested_{false};
        bool running_{true};

        std::string workspace_root_;
        json client_capabilities_;
    };
}
