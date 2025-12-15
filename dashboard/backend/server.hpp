//
// Created by gregorian on 14/12/2025.
//

#ifndef SERVER_HPP
#define SERVER_HPP

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

namespace bha::dashboard {

    class Server {
    public:
        struct Options {
            std::string host = "127.0.0.1";
            int port = 8080;
            std::string static_dir = "./dashboard/frontend/public";
            bool enable_cors = true;
            int max_connections = 100;
            bool verbose_logging = false;
            size_t thread_pool_size = 0;
            int accept_timeout_sec = 1;
            int read_timeout_sec = 30;
            int write_timeout_sec = 30;
            size_t max_request_size = 8192;
        };

        explicit Server(Options opts);
        ~Server();

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;
        Server(Server&&) = delete;
        Server& operator=(Server&&) = delete;

        void set_trace(const core::BuildTrace& trace);
        void set_suggestions(const std::vector<core::Suggestion>& suggestions);

        core::Result<void> start();
        core::Result<void> start_async();
        void stop();
        [[nodiscard]] bool is_running() const;
        [[nodiscard]] std::string get_url() const;

    private:
        class ThreadPool;
        class Impl;

        std::unique_ptr<Impl> impl_;
        std::unique_ptr<ThreadPool> thread_pool_;

        Options options_;
        std::shared_ptr<core::BuildTrace> trace_;
        std::shared_ptr<std::vector<core::Suggestion>> suggestions_;
        std::atomic<bool> running_{false};
        std::unique_ptr<std::thread> server_thread_;

        [[nodiscard]] std::string handle_get_trace() const;
        [[nodiscard]] std::string handle_get_hotspots() const;
        [[nodiscard]] std::string handle_get_suggestions() const;
        [[nodiscard]] std::string handle_get_metrics() const;
        [[nodiscard]] std::string handle_get_graph() const;
        [[nodiscard]] std::string handle_get_templates() const;
        [[nodiscard]] std::string handle_get_summary() const;

        void handle_client(int client_socket) const;
        [[nodiscard]] std::string route_request(const std::string& method, const std::string& path) const;
    };

} // namespace bha::dashboard

#endif //SERVER_HPP
