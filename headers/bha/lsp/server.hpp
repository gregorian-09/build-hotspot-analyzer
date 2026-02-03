#pragma once

#include "protocol.hpp"
#include "types.hpp"
#include "suggestion_manager.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <iostream>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace bha::lsp
{
    struct LSPConfig {
        bool auto_apply_all = false;
        bool show_preview_before_apply = true;
        bool rebuild_after_apply = true;
        bool rollback_on_build_failure = true;
        std::string build_command = "bha build";
        int build_timeout_seconds = 300;
        bool keep_backups = false;
        std::string backup_directory = ".lsp-optimization-backup";

        struct PerOptimization {
            bool pch_auto_apply = false;
            bool header_splitting_auto_apply = false;
            bool unity_build_auto_apply = false;
            bool template_optimization_auto_apply = false;
            bool include_reduction_auto_apply = false;
            bool forward_declaration_auto_apply = false;
            bool pimpl_auto_apply = false;
            bool move_to_cpp_auto_apply = false;
        } per_optimization;

        static LSPConfig from_json(const json& j);
        [[nodiscard]] json to_json() const;
    };
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

        static void send_progress(const ProgressToken& token, const json& value);
        ProgressToken create_progress_token();

        struct UserConsentResult {
            bool approved = false;
            std::string selected_action;
        };

        UserConsentResult request_user_consent(
            const std::string& message,
            const std::vector<std::string>& actions = {"Apply", "Cancel"}
        );

        [[nodiscard]] const LSPConfig& config() const { return config_; }
        void set_config(const LSPConfig& config) { config_ = config; }

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

        json handle_execute_command(const json& params);
        json handle_text_document_code_action(const json& params) const;

        json execute_analyze(const json& args);
        json execute_apply_suggestion(const json& args);
        json execute_apply_all_suggestions(const json& args);
        json execute_revert_changes(const json& args) const;
        json execute_get_suggestion_details(const json& args) const;
        json execute_show_metrics(const json& args) const;

        bool run_build_validation(std::vector<Diagnostic>& errors) const;
        static std::string detect_build_command(const std::filesystem::path& project_root);

        std::map<std::string, RequestHandler> request_handlers_;
        std::map<std::string, NotificationHandler> notification_handlers_;

        ServerState state_{ServerState::Uninitialized};
        bool running_{true};

        std::string workspace_root_;
        json client_capabilities_;

        std::unique_ptr<SuggestionManager> suggestion_manager_;
        LSPConfig config_;
        std::atomic<int> progress_token_counter_{0};
        std::atomic<int> request_id_counter_{0};

        struct PendingRequest {
            std::optional<json> response;
            bool received = false;
        };
        std::map<int, PendingRequest> pending_requests_;
        std::mutex pending_mutex_;
        std::condition_variable pending_cv_;

        void handle_response(const ResponseMessage& response);
        json send_request(const std::string& method, const json& params);
    };
}
