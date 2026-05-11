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
    /**
     * @brief Runtime configuration for LSP command handling and apply behavior.
     */
    struct LSPConfig {
        /// Automatically apply all suggestions after analysis when allowed.
        bool auto_apply_all = false;
        /// Show preview to the client before applying edits.
        bool show_preview_before_apply = true;
        /// Trigger a rebuild validation after apply operations.
        bool rebuild_after_apply = true;
        /// Roll back edits automatically when rebuild validation fails.
        bool rollback_on_build_failure = true;
        /// Fallback build command used when no project-specific profile exists.
        std::string build_command = "bha build";
        /// Build validation timeout in seconds.
        int build_timeout_seconds = 300;
        /// Keep backups after successful operations.
        bool keep_backups = false;
        /// Backup directory path relative to workspace root.
        std::string backup_directory = ".lsp-optimization-backup";
        /// Persist trust-loop metrics across sessions.
        bool persist_trust_loop = true;
        /// Allow analysis/apply paths when compile_commands are missing.
        bool allow_missing_compile_commands = true;
        /// Allow suggesters marked unsafe/advisory.
        bool include_unsafe_suggestions = false;
        /// Enable slower fallback passes for include-cleanup validation.
        bool enable_expensive_include_cleanup_fallbacks = false;
        /// Global confidence gate for returned suggestions.
        double min_confidence = 0.5;

        /**
         * @brief Per-suggester auto-apply feature switches.
         */
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

        /**
         * @brief Parse configuration from a JSON object.
         *
         * Missing keys retain defaults.
         */
        static LSPConfig from_json(const json& j);
        /**
         * @brief Serialize current configuration to JSON.
         */
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
        /// Handler for JSON-RPC request methods (expects a JSON result).
        using RequestHandler = std::function<json(const json& params)>;
        /// Handler for JSON-RPC notifications (fire-and-forget).
        using NotificationHandler = std::function<void(const json& params)>;

        /// Construct server instance with default handler/config wiring.
        LSPServer();
        /// Gracefully stop workers and release owned resources.
        ~LSPServer();

        /**
         * @brief Enter the main stdio JSON-RPC processing loop.
         *
         * Blocks until shutdown/exit or `stop()` is requested.
         */
        void run();
        /**
         * @brief Request loop termination.
         *
         * Safe to call from signal/worker context.
         */
        void stop();

        /**
         * @brief Register a request handler for an LSP method.
         *
         * Existing handlers for the same method are replaced.
         */
        void register_request_handler(const std::string& method, RequestHandler handler);
        /**
         * @brief Register a notification handler for an LSP method.
         *
         * Existing handlers for the same method are replaced.
         */
        void register_notification_handler(const std::string& method, NotificationHandler handler);

        /// Emit JSON-RPC notification to the connected client.
        static void send_notification(const std::string& method, const json& params);
        /// Emit JSON-RPC success response for a request id.
        static void send_response(const RequestId& id, const json& result);
        /// Emit JSON-RPC error response for a request id.
        static void send_error(const RequestId& id, ErrorCode code, const std::string& message,
                       const std::optional<json>& data = std::nullopt);

        /// Emit a `$/progress` notification for a progress token.
        static void send_progress(const ProgressToken& token, const json& value);
        /// Allocate a new progress token unique within this process.
        ProgressToken create_progress_token();

        /**
         * @brief Result of an interactive user-consent request.
         */
        struct UserConsentResult {
            /// Whether the user approved the operation.
            bool approved = false;
            /// Action label selected by the user when approved.
            std::string selected_action;
        };

        /**
         * @brief Ask the client for explicit approval before continuing.
         *
         * @param message Prompt text shown in the client.
         * @param actions Available action labels (first value is treated as approve).
         * @return User choice state.
         */
        UserConsentResult request_user_consent(
            const std::string& message,
            const std::vector<std::string>& actions = {"Apply", "Cancel"}
        );

        [[nodiscard]] const LSPConfig& config() const { return config_; }
        void set_config(const LSPConfig& config) { config_ = config; }

    private:
        /// Parse one incoming raw JSON-RPC message and dispatch by type.
        void handle_message(const std::string& message);
        /// Dispatch a request message and emit a response.
        void handle_request(const RequestMessage& request);
        /// Dispatch a notification message.
        void handle_notification(const NotificationMessage& notification);

        /// Read one framed JSON-RPC payload from stdin.
        static std::string read_message();
        /// Write one framed JSON-RPC payload to stdout.
        static void write_message(const std::string& content);

        /// Register builtin protocol and command handlers.
        void initialize_handlers();
        /// Handle `initialize` request and return server capabilities.
        json handle_initialize(const json& params);
        /// Handle `initialized` notification.
        void handle_initialized(const json& params);
        /// Handle `shutdown` request.
        json handle_shutdown(const json& params);
        /// Handle `exit` notification and finalize process state.
        void handle_exit(const json& params);

        /// Dispatch extension command execution entrypoint.
        json handle_execute_command(const json& params);
        /// Build code-action payload for a text document.
        json handle_text_document_code_action(const json& params) const;

        /// Record compile traces by invoking project build pipeline.
        json execute_record_build_traces(const json& args);
        /// Run project analysis and return suggestions/metrics.
        json execute_analyze(const json& args);
        /// Apply one suggestion and optionally validate build health.
        json execute_apply_suggestion(const json& args);
        /// Apply direct edit bundle from a client-provided payload.
        json execute_apply_edits(const json& args);
        /// Apply all filtered suggestions in one operation.
        json execute_apply_all_suggestions(const json& args);
        /// Return async job status snapshot.
        json execute_get_job_status(const json& args) const;
        /// Request cancellation for one running async job.
        json execute_cancel_job(const json& args);
        /// Revert one backup set by identifier.
        json execute_revert_changes(const json& args) const;
        /// List available in-memory and disk backups.
        json execute_list_backups(const json& args) const;
        /// Return detailed payload for one suggestion identifier.
        json execute_get_suggestion_details(const json& args) const;
        /// Return current/last build metrics summary.
        json execute_show_metrics(const json& args) const;
        /// Enumerate registered suggesters and metadata.
        json execute_list_suggesters(const json& args) const;
        /// Execute one selected suggester with explicit filters.
        json execute_run_suggester(const json& args);
        /// Return explanation payload for one suggestion.
        json execute_explain_suggestion(const json& args) const;
        /// Check if cancellation flag is set for an async job id.
        [[nodiscard]] bool is_async_job_cancel_requested(const std::string& job_id) const;
        /// Emit categorized log line for a background job.
        void send_job_log(const std::string& job_id, const std::string& category, const std::string& message) const;

        /**
         * @brief Run rebuild validation and collect diagnostics.
         *
         * @param errors Output diagnostics populated on failure.
         * @param measured_duration_ms Optional measured duration output.
         * @param job_id Optional async job identifier for log scoping.
         * @param stage Optional stage label for structured logs.
         * @return True on successful validation build.
         */
        bool run_build_validation(
            std::vector<Diagnostic>& errors,
            std::optional<int>& measured_duration_ms,
            const std::string& job_id = {},
            const std::string& stage = {}
        ) const;
        /// Infer a build command from project structure/build system markers.
        static std::string detect_build_command(const std::filesystem::path& project_root);
        /// Store the last known build profile used for validation/reuse.
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
        /// Resolve trust-loop baseline build time and provenance.
        [[nodiscard]] std::pair<std::optional<int>, std::optional<std::string>> resolve_trust_loop_baseline(
            const std::optional<std::filesystem::path>& project_root_hint = std::nullopt
        ) const;
        /// Persist trust-loop comparison metrics after apply operations.
        void persist_trust_loop_metrics(
            const json& trust_loop,
            const std::optional<std::string>& suggestion_id,
            bool apply_all
        ) const;

        /**
         * @brief Cached build profile from recent record/apply operations.
         */
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

        /**
         * @brief Tracks outstanding request/response pairs initiated by server.
         */
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

        /**
         * @brief State container for asynchronous command execution.
         */
        struct AsyncJob {
            /// Stable job identifier.
            std::string id;
            /// Command name (executeCommand semantic command id).
            std::string command;
            /// Original command arguments.
            json args;
            /// Progress token used for `$ /progress` notifications.
            ProgressToken progress_token;
            /// Per-job synchronization primitive for mutable fields.
            mutable std::mutex mutex;
            /// Cooperative cancellation flag.
            std::atomic<bool> cancel_requested{false};
            /// Completion indicator.
            std::atomic<bool> finished{false};
            /// Current lifecycle status.
            std::atomic<AsyncJobStatus> status{AsyncJobStatus::Queued};
            /// Success result payload.
            std::optional<json> result;
            /// Failure message payload.
            std::optional<std::string> error;
            /// Creation timestamp.
            std::chrono::system_clock::time_point created_at;
            /// Execution start timestamp.
            std::chrono::system_clock::time_point started_at;
            /// Execution completion timestamp.
            std::chrono::system_clock::time_point finished_at;
            /// Worker thread running the job body.
            std::thread worker;
        };

        /// Create monotonically unique async job identifier.
        std::string create_async_job_id();
        /// Enqueue an async command and return immediate acknowledgment payload.
        json queue_async_command(const std::string& command, const json& args);
        /// Execute one async job lifecycle on worker thread.
        void run_async_job(const std::shared_ptr<AsyncJob>& job);
        /// Reclaim completed/cancelled jobs and worker resources.
        void cleanup_finished_jobs();
        /// Convert async status enum to wire-format string.
        static std::string async_job_status_to_string(AsyncJobStatus status);
        /// Build JSON snapshot for one async job.
        json build_async_job_payload(const AsyncJob& job) const;

        std::unordered_map<std::string, std::shared_ptr<AsyncJob>> async_jobs_;
        mutable std::mutex async_jobs_mutex_;
        std::atomic<int> async_job_counter_{0};

        /// Handle response messages for server-originated requests.
        void handle_response(const ResponseMessage& response);
        /// Send a JSON-RPC request and block until reply or timeout.
        json send_request(const std::string& method, const json& params);
    };
}
