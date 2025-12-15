//
// Created by gregorian on 14/12/2025.
//

#include "server.hpp"
#include <sstream>
#include <fstream>
#include <iostream>
#include <utility>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <map>
#include <queue>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <cerrno>
#endif

using namespace bha::core;

namespace bha::dashboard {

    class Server::ThreadPool {
    public:
        explicit ThreadPool(const size_t num_threads) : stop_(false) {
            for (size_t i = 0; i < num_threads; ++i) {
                workers_.emplace_back([this] { this->worker_thread(); });
            }
        }

        ~ThreadPool() {
            {
                std::unique_lock lock(queue_mutex_);
                stop_ = true;
            }
            condition_.notify_all();

            for (auto& worker : workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
        }

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        template<typename F>
        void enqueue(F&& task) {
            {
                std::unique_lock lock(queue_mutex_);
                if (stop_) {
                    return;
                }
                tasks_.emplace(std::forward<F>(task));
            }
            condition_.notify_one();
        }

        [[nodiscard]] size_t pending_tasks() const {
            std::unique_lock lock(queue_mutex_);
            return tasks_.size();
        }

    private:
        void worker_thread() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock lock(queue_mutex_);
                    condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                    if (stop_ && tasks_.empty()) {
                        return;
                    }

                    if (!tasks_.empty()) {
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                }

                if (task) {
                    try {
                        task();
                    } catch (const std::exception& e) {
                        std::cerr << "Worker thread exception: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "Worker thread unknown exception" << std::endl;
                    }
                }
            }
        }

        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        mutable std::mutex queue_mutex_;
        std::condition_variable condition_;
        bool stop_;
    };

    class HttpRequest {
    public:
        std::string method;
        std::string path;
        std::string version;
        std::map<std::string, std::string> headers;
        std::string body;

        [[nodiscard]] bool parse(const std::string& raw_request) {
            std::istringstream stream(raw_request);

            if (!(stream >> method >> path >> version)) {
                return false;
            }

            std::string line;
            std::getline(stream, line);

            while (std::getline(stream, line) && line != "\r" && !line.empty()) {
                if (line.back() == '\r') {
                    line.pop_back();
                }

                if (const auto colon_pos = line.find(':'); colon_pos != std::string::npos) {
                    std::string key = line.substr(0, colon_pos);
                    std::string value = line.substr(colon_pos + 1);

                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t\r\n") + 1);

                    std::ranges::transform(key, key.begin(), ::tolower);
                    headers[key] = value;
                }
            }

            if (headers.contains("content-length")) {
                try {
                    if (const size_t content_length = std::stoull(headers["content-length"]); content_length > 0) {
                        body.resize(content_length);
                        stream.read(&body[0], static_cast<std::streamsize>(content_length));
                    }
                } catch (...) {
                    return false;
                }
            }

            return !method.empty() && !path.empty();
        }
    };

    class HttpResponse {
    public:
        static std::string ok(const std::string& content, const std::string& content_type = "application/json") {
            std::ostringstream oss;
            oss << "HTTP/1.1 200 OK\r\n";
            oss << "Content-Type: " << content_type << "\r\n";
            oss << "Content-Length: " << content.length() << "\r\n";
            oss << "Access-Control-Allow-Origin: *\r\n";
            oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
            oss << "Access-Control-Allow-Headers: Content-Type\r\n";
            oss << "Connection: close\r\n\r\n";
            oss << content;
            return oss.str();
        }

        static std::string options() {
            std::ostringstream oss;
            oss << "HTTP/1.1 204 No Content\r\n";
            oss << "Access-Control-Allow-Origin: *\r\n";
            oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
            oss << "Access-Control-Allow-Headers: Content-Type\r\n";
            oss << "Connection: close\r\n\r\n";
            return oss.str();
        }

        static std::string not_found() {
            return ok(R"({"error": "Not Found"})");
        }

        static std::string bad_request(const std::string& message = "Bad Request") {
            std::ostringstream oss;
            oss << "HTTP/1.1 400 Bad Request\r\n";
            oss << "Content-Type: application/json\r\n";
            oss << "Connection: close\r\n\r\n";
            oss << R"({"error": ")" << escape_json(message) << R"("})";
            return oss.str();
        }

        static std::string internal_error(const std::string& message = "Internal Server Error") {
            std::ostringstream oss;
            oss << "HTTP/1.1 500 Internal Server Error\r\n";
            oss << "Content-Type: application/json\r\n";
            oss << "Connection: close\r\n\r\n";
            oss << R"({"error": ")" << escape_json(message) << R"("})";
            return oss.str();
        }

    private:
        static std::string escape_json(const std::string& str) {
            std::ostringstream oss;
            for (const char c : str) {
                switch (c) {
                    case '"': oss << "\\\""; break;
                    case '\\': oss << "\\\\"; break;
                    case '\n': oss << "\\n"; break;
                    case '\r': oss << "\\r"; break;
                    case '\t': oss << "\\t"; break;
                    default: oss << c; break;
                }
            }
            return oss.str();
        }
    };

    class SocketGuard {
    public:
        explicit SocketGuard(const int fd) : fd_(fd) {}

        ~SocketGuard() {
            if (fd_ >= 0) {
#ifdef _WIN32
                shutdown(fd_, SD_BOTH);
                closesocket(fd_);
#else
                shutdown(fd_, SHUT_RDWR);
                ::close(fd_);
#endif
            }
        }

        SocketGuard(const SocketGuard&) = delete;
        SocketGuard& operator=(const SocketGuard&) = delete;

        [[nodiscard]] int get() const { return fd_; }

        int release() {
            const int temp = fd_;
            fd_ = -1;
            return temp;
        }

    private:
        int fd_;
    };

    class Server::Impl {
    public:
        int server_socket = -1;

        static bool set_socket_timeout(int socket_fd, int timeout_sec, bool for_recv) {
#ifdef _WIN32
            const DWORD timeout_ms = timeout_sec * 1000;
            return setsockopt(socket_fd, SOL_SOCKET,
                            for_recv ? SO_RCVTIMEO : SO_SNDTIMEO,
                            reinterpret_cast<const char*>(&timeout_ms),
                            sizeof(timeout_ms)) == 0;
#else
            struct timeval tv;
            tv.tv_sec = timeout_sec;
            tv.tv_usec = 0;
            return setsockopt(socket_fd, SOL_SOCKET,
                            for_recv ? SO_RCVTIMEO : SO_SNDTIMEO,
                            &tv, sizeof(tv)) == 0;
#endif
        }

        static bool set_socket_nonblocking(int socket_fd) {
#ifdef _WIN32
            u_long mode = 1;
            return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
            int flags = fcntl(socket_fd, F_GETFL, 0);
            if (flags == -1) return false;
            return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
        }

        void close_socket() {
            if (server_socket >= 0) {
#ifdef _WIN32
                shutdown(server_socket, SD_BOTH);
                closesocket(server_socket);
#else
                shutdown(server_socket, SHUT_RDWR);
                ::close(server_socket);
#endif
                server_socket = -1;
            }
        }
    };

    Server::Server(Options opts)
        : impl_(std::make_unique<Impl>()), options_(std::move(opts)) {

        if (options_.thread_pool_size == 0) {
            options_.thread_pool_size = std::max(2u, std::thread::hardware_concurrency());
        }

        thread_pool_ = std::make_unique<ThreadPool>(options_.thread_pool_size);

#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    Server::~Server() {
        stop();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void Server::set_trace(const BuildTrace& trace) {
        trace_ = std::make_shared<BuildTrace>(trace);
    }

    void Server::set_suggestions(const std::vector<Suggestion>& suggestions) {
        suggestions_ = std::make_shared<std::vector<Suggestion>>(suggestions);
    }

    Result<void> Server::start() {
        if (running_) {
            return Result<void>::failure(ErrorCode::INVALID_STATE, "Server already running");
        }

        impl_->server_socket = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
        if (impl_->server_socket < 0) {
            return Result<void>::failure(ErrorCode::NETWORK_ERROR, "Failed to create socket");
        }

        int opt = 1;
#ifdef _WIN32
        setsockopt(impl_->server_socket, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(impl_->server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(impl_->server_socket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
#endif

        Impl::set_socket_nonblocking(impl_->server_socket);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(options_.host.c_str());
        addr.sin_port = htons(options_.port);

        if (bind(impl_->server_socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            impl_->close_socket();
            return Result<void>::failure(ErrorCode::NETWORK_ERROR, "Bind failed");
        }

        if (listen(impl_->server_socket, options_.max_connections) < 0) {
            impl_->close_socket();
            return Result<void>::failure(ErrorCode::NETWORK_ERROR, "Listen failed");
        }

        running_ = true;
        std::cout << "BHA Dashboard running at " << get_url() << std::endl;
        std::cout << "Thread pool size: " << options_.thread_pool_size << std::endl;

        while (running_) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(impl_->server_socket, &read_fds);

            timeval timeout{};
            timeout.tv_sec = options_.accept_timeout_sec;
            timeout.tv_usec = 0;

            const int activity = select(impl_->server_socket + 1, &read_fds, nullptr, nullptr, &timeout);

            if (activity < 0 && running_) {
#ifdef _WIN32
                if (WSAGetLastError() != WSAEINTR) {
#else
                if (errno != EINTR) {
#endif
                    std::cerr << "Select error" << std::endl;
                }
                continue;
            }

            if (activity == 0) {
                continue;
            }

            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_socket = static_cast<int>(accept(impl_->server_socket,
                                                        reinterpret_cast<struct sockaddr*>(&client_addr),
                                                        &client_len));

            if (client_socket < 0) {
                if (running_) {
#ifdef _WIN32
                    if (const int err = WSAGetLastError(); err != WSAEWOULDBLOCK) {
#else
                    if (errno != EWOULDBLOCK && errno != EAGAIN) {
#endif
                        std::cerr << "Accept failed" << std::endl;
                    }
                }
                continue;
            }

            Impl::set_socket_timeout(client_socket, options_.read_timeout_sec, true);
            Impl::set_socket_timeout(client_socket, options_.write_timeout_sec, false);

            thread_pool_->enqueue([this, client_socket]() {
                this->handle_client(client_socket);
            });
        }

        impl_->close_socket();
        return {};
    }

    void Server::handle_client(int client_socket) const
    {
        SocketGuard guard(client_socket);

        std::vector<char> buffer(options_.max_request_size);

#ifdef _WIN32
        int bytes_read = recv(client_socket, buffer.data(), static_cast<int>(buffer.size() - 1), 0);
#else
        ssize_t bytes_read = read(client_socket, buffer.data(), buffer.size() - 1);
#endif

        if (bytes_read <= 0) {
            return;
        }

        const std::string raw_request(buffer.data(), bytes_read);
        HttpRequest request;

        if (!request.parse(raw_request)) {
            std::string response = HttpResponse::bad_request();
#ifdef _WIN32
            send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
#else
            write(client_socket, response.c_str(), response.size());
#endif
            return;
        }

        if (options_.verbose_logging) {
            std::cout << request.method << " " << request.path << std::endl;
        }

        std::string response;
        try {
            response = route_request(request.method, request.path);
        } catch (const std::exception& e) {
            std::cerr << "Request handling exception: " << e.what() << std::endl;
            response = HttpResponse::internal_error();
        }

#ifdef _WIN32
        send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
#else
        write(client_socket, response.c_str(), response.size());
#endif
    }

    std::string Server::route_request(const std::string& method, const std::string& path) const {
        if (method == "OPTIONS") {
            return HttpResponse::options();
        }

        if (method != "GET") {
            return HttpResponse::not_found();
        }

        if (path == "/api/trace") {
            return HttpResponse::ok(handle_get_trace());
        } else if (path == "/api/hotspots") {
            return HttpResponse::ok(handle_get_hotspots());
        } else if (path == "/api/suggestions") {
            return HttpResponse::ok(handle_get_suggestions());
        } else if (path == "/api/metrics") {
            return HttpResponse::ok(handle_get_metrics());
        } else if (path == "/api/graph") {
            return HttpResponse::ok(handle_get_graph());
        } else if (path == "/" || path == "/index.html") {
            if (std::ifstream file(options_.static_dir + "/index.html"); file) {
                const std::string content((std::istreambuf_iterator(file)),
                                  std::istreambuf_iterator<char>());
                return HttpResponse::ok(content, "text/html");
            }
            return HttpResponse::not_found();
        }

        return HttpResponse::not_found();
    }

    Result<void> Server::start_async() {
        if (running_) {
            return Result<void>::failure(ErrorCode::INVALID_STATE, "Server is already running");
        }

        server_thread_ = std::make_unique<std::thread>([this]() {
            if (const auto result = this->start(); !result && options_.verbose_logging) {
                std::cerr << "Server thread error: " << result.is_failure() << std::endl;
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return {};
    }

    void Server::stop() {
        if (running_) {
            running_ = false;
            impl_->close_socket();

            if (server_thread_ && server_thread_->joinable()) {
                server_thread_->join();
            }

            std::cout << "Server stopped." << std::endl;
        }
    }

    bool Server::is_running() const {
        return running_;
    }

    std::string Server::get_url() const {
        return "http://" + options_.host + ":" + std::to_string(options_.port);
    }

    std::string Server::handle_get_trace() const {
        if (!trace_) {
            return R"({"error": "No trace data available"})";
        }

        std::ostringstream oss;
        oss << "{\n";
        oss << R"(  "trace_id": ")" << trace_->trace_id << "\",\n";
        oss << "  \"total_build_time_ms\": " << trace_->total_build_time_ms << ",\n";
        oss << R"(  "build_system": ")" << trace_->build_system << "\",\n";
        oss << R"(  "platform": ")" << trace_->platform << "\",\n";
        oss << "  \"compilation_units_count\": " << trace_->compilation_units.size() << ",\n";
        oss << "  \"is_clean_build\": " << (trace_->is_clean_build ? "true" : "false") << "\n";
        oss << "}";

        return oss.str();
    }

    std::string Server::handle_get_hotspots() const {
        if (!trace_) {
            return R"({"error": "No trace data available"})";
        }

        std::ostringstream oss;
        oss << "{\n  \"hotspots\": [\n";

        const auto& hotspots = trace_->metrics.top_slow_files;
        for (size_t i = 0; i < hotspots.size(); ++i) {
            const auto& h = hotspots[i];
            if (i > 0) oss << ",\n";
            oss << "    {\n";
            oss << R"(      "file_path": ")" << h.file_path << "\",\n";
            oss << "      \"time_ms\": " << h.time_ms << ",\n";
            oss << "      \"impact_score\": " << h.impact_score << ",\n";
            oss << "      \"num_dependent_files\": " << h.num_dependent_files << ",\n";
            oss << R"(      "category": ")" << h.category << "\"\n";
            oss << "    }";
        }

        oss << "\n  ]\n}";
        return oss.str();
    }

    std::string Server::handle_get_suggestions() const {
        if (!suggestions_) {
            return R"({"suggestions": []})";
        }

        std::ostringstream oss;
        oss << "{\n  \"suggestions\": [\n";

        for (size_t i = 0; i < suggestions_->size(); ++i) {
            const auto& s = (*suggestions_)[i];
            if (i > 0) oss << ",\n";
            oss << "    {\n";
            oss << R"(      "id": ")" << s.id << "\",\n";
            oss << R"(      "type": ")" << to_string(s.type) << "\",\n";
            oss << R"(      "priority": ")" << to_string(s.priority) << "\",\n";
            oss << "      \"confidence\": " << s.confidence << ",\n";
            oss << R"(      "title": ")" << s.title << "\",\n";
            oss << R"(      "description": ")" << s.description << "\",\n";
            oss << R"(      "file_path": ")" << s.file_path << "\",\n";
            oss << "      \"estimated_time_savings_ms\": " << s.estimated_time_savings_ms << ",\n";
            oss << "      \"is_safe\": " << (s.is_safe ? "true" : "false") << "\n";
            oss << "    }";
        }

        oss << "\n  ]\n}";
        return oss.str();
    }

    std::string Server::handle_get_metrics() const {
        if (!trace_) {
            return R"({"error": "No trace data available"})";
        }

        const auto& m = trace_->metrics;
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"total_files_compiled\": " << m.total_files_compiled << ",\n";
        oss << "  \"total_headers_parsed\": " << m.total_headers_parsed << ",\n";
        oss << "  \"average_file_time_ms\": " << m.average_file_time_ms << ",\n";
        oss << "  \"median_file_time_ms\": " << m.median_file_time_ms << ",\n";
        oss << "  \"p95_file_time_ms\": " << m.p95_file_time_ms << ",\n";
        oss << "  \"p99_file_time_ms\": " << m.p99_file_time_ms << ",\n";
        oss << "  \"total_dependencies\": " << m.total_dependencies << ",\n";
        oss << "  \"average_include_depth\": " << m.average_include_depth << ",\n";
        oss << "  \"max_include_depth\": " << m.max_include_depth << "\n";
        oss << "}";

        return oss.str();
    }

    std::string Server::handle_get_graph() const {
        if (!trace_) {
            return R"({"nodes": [], "edges": []})";
        }

        const auto& graph = trace_->dependency_graph;
        const auto& adj_list = graph.get_adjacency_list();

        std::ostringstream oss;
        oss << "{\n  \"nodes\": [\n";

        const auto nodes = graph.get_all_nodes();
        for (size_t i = 0; i < nodes.size() && i < 100; ++i) {
            if (i > 0) oss << ",\n";
            oss << R"(    {"id": ")" << nodes[i] << "\"}";
        }

        oss << "\n  ],\n  \"edges\": [\n";

        bool first = true;
        size_t edge_count = 0;
        for (const auto& [source, edges] : adj_list) {
            for (const auto& edge : edges) {
                if (edge_count++ >= 500) break;
                if (!first) oss << ",\n";
                first = false;
                oss << R"(    {"source": ")" << source << R"(", "target": ")" << edge.target << "\"}";
            }
            if (edge_count >= 500) break;
        }

        oss << "\n  ]\n}";
        return oss.str();
    }

    std::string Server::handle_get_templates() const {
        if (!trace_) {
            return R"({"templates": []})";
        }

        const auto& templates = trace_->metrics.expensive_templates;
        std::ostringstream oss;
        oss << "{\n  \"templates\": [\n";

        for (size_t i = 0; i < templates.size(); ++i) {
            const auto& t = templates[i];
            if (i > 0) oss << ",\n";
            oss << "    {\n";
            oss << R"(      "template_name": ")" << t.template_name << "\",\n";
            oss << "      \"time_ms\": " << t.time_ms << ",\n";
            oss << "      \"instantiation_count\": " << t.instantiation_count << "\n";
            oss << "    }";
        }

        oss << "\n  ]\n}";
        return oss.str();
    }

    std::string Server::handle_get_summary() const {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"server_version\": \"1.0.0\",\n";
        oss << "  \"has_trace\": " << (trace_ ? "true" : "false") << ",\n";
        oss << "  \"has_suggestions\": " << (suggestions_ ? "true" : "false") << ",\n";
        if (trace_) {
            oss << R"(  "trace_id": ")" << trace_->trace_id << "\",\n";
            oss << "  \"total_build_time_ms\": " << trace_->total_build_time_ms << ",\n";
            oss << "  \"compilation_units\": " << trace_->compilation_units.size() << ",\n";
        }
        if (suggestions_) {
            oss << "  \"suggestions_count\": " << suggestions_->size() << ",\n";
        }
        oss << "  \"status\": \"ready\"\n";
        oss << "}";
        return oss.str();
    }

} // namespace bha::dashboard