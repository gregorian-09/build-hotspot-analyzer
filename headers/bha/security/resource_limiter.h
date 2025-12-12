//
// Created by gregorian on 28/10/2025.
//

#ifndef RESOURCE_LIMITER_H
#define RESOURCE_LIMITER_H

#include "bha/core/result.h"
#include <chrono>
#include <atomic>

namespace bha::security {

    /**
     * Enforces limits on memory, execution time, graph size, and unit counts during analysis.
     *
     * Use this class to guard against resource overuse when processing large inputs
     * or performing heavy analyses. You should call `start_timer()` before beginning
     * the work, and periodically invoke the check methods to abort early if limits are exceeded.
     */
    class ResourceLimiter {
    public:
        /**
         * Limits configuration for the resource checks.
         */
        struct Limits {
            size_t max_memory_bytes = 8ULL * 1024 * 1024 * 1024;  ///< Maximum memory use in bytes
            std::chrono::seconds max_execution_time{300};     ///< Maximum allowed execution time
            size_t max_graph_nodes = 100000;                      ///< Max number of nodes in dependency graph
            size_t max_graph_edges = 1000000;                     ///< Max number of edges in graph
            size_t max_compilation_units = 50000;                 ///< Max number of compilation units processed
        };

        /**
         * Construct a new ResourceLimiter with custom limits.
         * @param limits The desired resource limits.
         */
        explicit ResourceLimiter(const Limits& limits);

        /**
         * Start the internal execution timer.
         *
         * Must be called before checks that depend on elapsed time.
         */
        void start_timer();

        /**
         * Check if current memory usage exceeds the configured limit.
         *
         * @return Result<void> Ok if within limits, or error if exceeded.
         */
        [[nodiscard]] core::Result<void> check_memory_limit() const;

        /**
         * Check if the elapsed execution time exceeds the configured limit.
         *
         * @return Result<void> Ok if within time, or error if exceeded.
         */
        [[nodiscard]] core::Result<void> check_time_limit() const;

        /**
         * Check whether the graph size (nodes + edges) exceeds allowed bounds.
         *
         * @param nodes Number of nodes in the graph.
         * @param edges Number of edges in the graph.
         * @return Result<void> OK or error on violation.
         */
        [[nodiscard]] core::Result<void> check_graph_size_limit(size_t nodes, size_t edges) const;

        /**
         * Check whether the number of compilation units exceeds the allowed limit.
         *
         * @param count Number of compilation units to check.
         * @return Result<void> OK or error on violation.
         */
        [[nodiscard]] core::Result<void> check_compilation_units_limit(size_t count) const;

        /**
         * Reset internal state (timer, flags) for reuse.
         */
        void reset();

        /**
         * Query current memory usage.
         *
         * @return Current memory usage in bytes.
         */
        static size_t get_current_memory_usage();

        /**
         * Get the elapsed time since `start_timer()` was called.
         *
         * @return Duration elapsed.
         */
        [[nodiscard]] std::chrono::duration<double> get_elapsed_time() const;

    private:
        Limits limits_;                                     ///< Resource limits configuration
        std::chrono::steady_clock::time_point start_time_;  ///< Timestamp when timer started
        std::atomic<bool> timer_started_{false};          ///< Flag indicating whether timer is active
    };

} // namespace bha::security

#endif //RESOURCE_LIMITER_H
