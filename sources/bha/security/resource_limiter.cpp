//
// Created by gregorian on 28/10/2025.
//

#include "bha/security/resource_limiter.h"
#include "bha/core/error.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace bha::security {

    ResourceLimiter::ResourceLimiter(const Limits& limits)
        : limits_(limits) {}

    void ResourceLimiter::start_timer() {
        start_time_ = std::chrono::steady_clock::now();
        timer_started_ = true;
    }

    core::Result<void> ResourceLimiter::check_memory_limit() const
    {
        if (const size_t current_usage = get_current_memory_usage(); current_usage > limits_.max_memory_bytes) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::RESOURCE_EXHAUSTED,
                "Memory limit exceeded: " +
                          std::to_string(current_usage / (1024 * 1024)) + "MB / " +
                          std::to_string(limits_.max_memory_bytes / (1024 * 1024)) + "MB"
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> ResourceLimiter::check_time_limit() const
    {
        if (!timer_started_) {
            return core::Result<void>::success();
        }

        if (const auto elapsed = get_elapsed_time(); elapsed > limits_.max_execution_time) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::TIMEOUT,
                "Execution time limit exceeded: " +
                          std::to_string(elapsed.count()) + "s / " +
                          std::to_string(limits_.max_execution_time.count()) + "s"
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> ResourceLimiter::check_graph_size_limit(const size_t nodes, const size_t edges) const
    {
        if (nodes > limits_.max_graph_nodes) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::RESOURCE_EXHAUSTED,
                "Graph node limit exceeded: " +
                          std::to_string(nodes) + " / " +
                          std::to_string(limits_.max_graph_nodes)
            });
        }

        if (edges > limits_.max_graph_edges) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::RESOURCE_EXHAUSTED,
                "Graph edge limit exceeded: " +
                          std::to_string(edges) + " / " +
                          std::to_string(limits_.max_graph_edges)
            });
        }

        return core::Result<void>::success();
    }

    core::Result<void> ResourceLimiter::check_compilation_units_limit(const size_t count) const
    {
        if (count > limits_.max_compilation_units) {
            return core::Result<void>::failure(core::Error{
                core::ErrorCode::RESOURCE_EXHAUSTED,
                "Compilation units limit exceeded: " +
                          std::to_string(count) + " / " +
                          std::to_string(limits_.max_compilation_units)
            });
        }

        return core::Result<void>::success();
    }

    void ResourceLimiter::reset() {
        timer_started_ = false;
        start_time_ = std::chrono::steady_clock::time_point{};
    }

    size_t ResourceLimiter::get_current_memory_usage()
    {
    #ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                                sizeof(pmc))) {
            return pmc.WorkingSetSize;
        }
        return 0;
    #else
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
    #ifdef __APPLE__
            return usage.ru_maxrss;
    #else
            return usage.ru_maxrss * 1024;
    #endif
        }
        return 0;
    #endif
    }

    std::chrono::seconds ResourceLimiter::get_elapsed_time() const {
        if (!timer_started_) {
            return std::chrono::seconds{0};
        }

        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    }

} // namespace bha::security