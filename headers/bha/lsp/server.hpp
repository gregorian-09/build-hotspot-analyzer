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
    /**
     * LSP server lifecycle states.
     * Per LSP spec, requests before initialize should return ServerNotInitialized.
     */
    enum class ServerState {
        Uninitialized,  ///< Before initialize request
        Initializing,   ///< Processing initialize request
        Running,        ///< After initialized notification
        ShuttingDown    ///< After shutdown request, before exit
    };

    /**
     * Language Server Protocol (LSP) server implementation.
     *
     * Thread Safety:
     * This server uses a single-threaded, synchronous message processing model.
     * The run() method blocks on stdin and processes messages sequentially.
     * All handler callbacks execute on the same thread as run().
     *
     * This design is intentional for LSP servers which typically:
     * - Have a single client connection
     * - Process requests in order
     * - Don't require concurrent request handling
     *
     * If async operations are needed in the future:
     * - Use std::async with progress notifications
     * - Or implement a work queue with background thread
     * - Ensure handlers are thread-safe if shared state is accessed
     *
     * Protocol Compliance:
     * - JSON-RPC 2.0 over stdio
     * - HTTP-style Content-Length headers
     * - Supports both CRLF and LF line endings
     */
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

        static void send_notification(const std::string& method, const json& params);
        static void send_response(const RequestId& id, const json& result);
        static void send_error(const RequestId& id, ErrorCode code, const std::string& message,
                       const std::optional<json>& data = std::nullopt);

    private:
        void handle_message(const std::string& message);
        void handle_request(const RequestMessage& request);
        void handle_notification(const NotificationMessage& notification);

        static std::string read_message();
        static void write_message(const std::string& content);

        void initialize_handlers();
        json handle_initialize(const json& params);
        void handle_initialized(const json& params);
        json handle_shutdown(const json& params);
        void handle_exit(const json& params);

        std::map<std::string, RequestHandler> request_handlers_;
        std::map<std::string, NotificationHandler> notification_handlers_;

        ServerState state_{ServerState::Uninitialized};
        bool running_{true};

        std::string workspace_root_;
        json client_capabilities_;
    };
}
