#pragma once

#include "protocol.hpp"
#include "types.hpp"
#include "suggestion_manager.hpp"
#include "bha/build_systems/adapter.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <iostream>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <thread>
#include <unordered_map>

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
        bool persist_trust_loop = true;
        bool allow_missing_compile_commands = true;
        bool include_unsafe_suggestions = false;
        bool enable_expensive_include_cleanup_fallbacks = false;
        double min_confidence = 0.5;

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
        ~LSPServer();

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

        json execute_record_build_traces(const json& args);
        json execute_analyze(const json& args);
        json execute_apply_suggestion(const json& args);
        json execute_apply_edits(const json& args);
        json execute_apply_all_suggestions(const json& args);
        json execute_get_job_status(const json& args) const;
        json execute_cancel_job(const json& args);
        json execute_revert_changes(const json& args) const;
        json execute_list_backups(const json& args) const;
        json execute_get_suggestion_details(const json& args) const;
        json execute_show_metrics(const json& args) const;
        json execute_list_suggesters(const json& args) const;
        json execute_run_suggester(const json& args);
        json execute_explain_suggestion(const json& args) const;
        [[nodiscard]] bool is_async_job_cancel_requested(const std::string& job_id) const;
        void send_job_log(const std::string& job_id, const std::string& category, const std::string& message) const;

        bool run_build_validation(
            std::vector<Diagnostic>& errors,
            std::optional<int>& measured_duration_ms,
            const std::string& job_id = {},
            const std::string& stage = {}
        ) const;
        static std::string detect_build_command(const std::filesystem::path& project_root);
        void remember_build_profile(
            const std::filesystem::path& project_root,
            const std::string& build_system,
            const build_systems::BuildOptions& options,
            std::optional<int> recorded_build_time_ms = std::nullopt
        );
        void update_build_profile_from_json(
            const std::filesystem::path& project_root,
            const json& build_profile_json
        );
        [[nodiscard]] std::pair<std::optional<int>, std::optional<std::string>> resolve_trust_loop_baseline(
            const std::optional<std::filesystem::path>& project_root_hint = std::nullopt
        ) const;
        void persist_trust_loop_metrics(
            const json& trust_loop,
            const std::optional<std::string>& suggestion_id,
            bool apply_all
        ) const;

        struct LastBuildProfile {
            std::filesystem::path project_root;
            std::string build_system;
            build_systems::BuildOptions options;
            std::optional<int> recorded_build_time_ms;
        };

        std::map<std::string, RequestHandler> request_handlers_;
        std::map<std::string, NotificationHandler> notification_handlers_;

        ServerState state_{ServerState::Uninitialized};
        bool running_{true};

        std::string workspace_root_;
        json client_capabilities_;

        std::unique_ptr<SuggestionManager> suggestion_manager_;
        LSPConfig config_;
        mutable std::mutex build_profile_mutex_;
        std::optional<LastBuildProfile> last_build_profile_;
        std::atomic<int> progress_token_counter_{0};
        std::atomic<int> request_id_counter_{0};

        struct PendingRequest {
            std::optional<json> response;
            bool received = false;
        };
        std::map<int, PendingRequest> pending_requests_;
        std::mutex pending_mutex_;
        std::condition_variable pending_cv_;
        mutable std::mutex suggestion_manager_mutex_;

        enum class AsyncJobStatus {
            Queued,
            Running,
            Completed,
            Failed,
            Cancelled
        };

        struct AsyncJob {
            std::string id;
            std::string command;
            json args;
            ProgressToken progress_token;
            mutable std::mutex mutex;
            std::atomic<bool> cancel_requested{false};
            std::atomic<bool> finished{false};
            std::atomic<AsyncJobStatus> status{AsyncJobStatus::Queued};
            std::optional<json> result;
            std::optional<std::string> error;
            std::chrono::system_clock::time_point created_at;
            std::chrono::system_clock::time_point started_at;
            std::chrono::system_clock::time_point finished_at;
            std::thread worker;
        };

        std::string create_async_job_id();
        json queue_async_command(const std::string& command, const json& args);
        void run_async_job(const std::shared_ptr<AsyncJob>& job);
        void cleanup_finished_jobs();
        static std::string async_job_status_to_string(AsyncJobStatus status);
        json build_async_job_payload(const AsyncJob& job) const;

        std::unordered_map<std::string, std::shared_ptr<AsyncJob>> async_jobs_;
        mutable std::mutex async_jobs_mutex_;
        std::atomic<int> async_job_counter_{0};

        void handle_response(const ResponseMessage& response);
        json send_request(const std::string& method, const json& params);
    };
}
