//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_PARALLEL_HPP
#define BUILDTIMEHOTSPOTANALYZER_PARALLEL_HPP

/**
 * @file parallel.hpp
 * @brief Parallel execution utilities.
 *
 * Provides utilities for parallel processing of collections,
 * including parallel map, filter, and reduce operations.
 * Uses a thread pool for efficient work distribution.
 */

#include <vector>
#include <future>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <algorithm>

namespace bha::parallel {

    /**
     * Returns the number of hardware threads available.
     *
     * @return The number of threads, or 1 if detection fails.
     */
    inline unsigned int hardware_concurrency() noexcept {
        unsigned int n = std::thread::hardware_concurrency();
        return n > 0 ? n : 1;
    }

    /**
     * A simple thread pool for parallel task execution.
     */
    class ThreadPool {
    public:
        /**
         * Creates a thread pool with the specified number of threads.
         *
         * @param num_threads Number of worker threads (0 = auto-detect).
         */
        explicit ThreadPool(unsigned int num_threads = 0)
            : stop_(false) {
            if (num_threads == 0) {
                num_threads = hardware_concurrency();
            }

            workers_.reserve(num_threads);
            for (unsigned int i = 0; i < num_threads; ++i) {
                workers_.emplace_back([this] {
                    worker_loop();
                });
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

        /**
         * Submits a task to the thread pool.
         *
         * @tparam F Function type.
         * @tparam Args Argument types.
         * @param f The function to execute.
         * @param args Arguments to pass to the function.
         * @return A future for the result.
         */
        template<typename F, typename... Args>
        auto submit(F&& f, Args&&... args)
            -> std::future<std::invoke_result_t<F, Args...>> {
            using return_type = std::invoke_result_t<F, Args...>;

            auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

            std::future<return_type> result = task->get_future();

            {
                std::unique_lock lock(queue_mutex_);
                if (stop_) {
                    throw std::runtime_error("Cannot submit to stopped thread pool");
                }
                tasks_.emplace([task]() { (*task)(); });
            }

            condition_.notify_one();
            return result;
        }

        /**
         * Returns the number of worker threads.
         */
        [[nodiscard]] std::size_t size() const noexcept {
            return workers_.size();
        }

    private:
        void worker_loop() {
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock lock(queue_mutex_);
                    condition_.wait(lock, [this] {
                        return stop_ || !tasks_.empty();
                    });

                    if (stop_ && tasks_.empty()) {
                        return;
                    }

                    task = std::move(tasks_.front());
                    tasks_.pop();
                }

                task();
            }
        }

        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        std::mutex queue_mutex_;
        std::condition_variable condition_;
        bool stop_;
    };

    /**
     * Global thread pool for parallel operations.
     *
     * @return Reference to the global thread pool.
     */
    inline ThreadPool& global_pool() {
        static ThreadPool pool;
        return pool;
    }

    /**
     * Applies a function to each element in parallel.
     *
     * @tparam Container Input container type.
     * @tparam F Function type.
     * @param items The items to process.
     * @param f The function to apply to each item.
     * @param pool The thread pool to use (default: global pool).
     */
    template<typename Container, typename F>
    void for_each(Container& items, F&& f, ThreadPool& pool = global_pool()) {
        std::vector<std::future<void>> futures;
        futures.reserve(items.size());

        for (auto& item : items) {
            futures.push_back(pool.submit([&f, &item]() {
                f(item);
            }));
        }

        for (auto& future : futures) {
            future.get();
        }
    }

    /**
     * Maps a function over a collection in parallel.
     *
     * @tparam T Input type.
     * @tparam F Function type.
     * @param items The items to transform.
     * @param f The transformation function.
     * @param pool The thread pool to use (default: global pool).
     * @return Vector of transformed items.
     */
    template<typename T, typename F>
    auto map(const std::vector<T>& items, F&& f, ThreadPool& pool = global_pool())
        -> std::vector<std::invoke_result_t<F, const T&>> {
        using ResultType = std::invoke_result_t<F, const T&>;

        std::vector<std::future<ResultType>> futures;
        futures.reserve(items.size());

        for (const auto& item : items) {
            futures.push_back(pool.submit([&f, &item]() {
                return f(item);
            }));
        }

        std::vector<ResultType> results;
        results.reserve(items.size());

        for (auto& future : futures) {
            results.push_back(future.get());
        }

        return results;
    }

    /**
     * Filters a collection in parallel.
     *
     * @tparam T Element type.
     * @tparam F Predicate type.
     * @param items The items to filter.
     * @param predicate The filter predicate.
     * @param pool The thread pool to use (default: global pool).
     * @return Vector of items matching the predicate.
     */
    template<typename T, typename F>
    std::vector<T> filter(const std::vector<T>& items, F&& predicate, ThreadPool& pool = global_pool()) {
        std::vector<std::future<bool>> futures;
        futures.reserve(items.size());

        for (const auto& item : items) {
            futures.push_back(pool.submit([&predicate, &item]() {
                return predicate(item);
            }));
        }

        std::vector<T> results;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (futures[i].get()) {
                results.push_back(items[i]);
            }
        }

        return results;
    }

    /**
     * Reduces a collection in parallel using chunked processing.
     *
     * @tparam T Element type.
     * @tparam F Reducer function type.
     * @param items The items to reduce.
     * @param initial The initial value.
     * @param reducer The reduction function (must be associative).
     * @param pool The thread pool to use (default: global pool).
     * @return The reduced value.
     */
    template<typename T, typename F>
    T reduce(
        const std::vector<T>& items,
        T initial,
        F&& reducer,
        ThreadPool& pool = global_pool()
    ) {
        if (items.empty()) {
            return initial;
        }

        auto num_threads = pool.size();
        auto chunk_size = (items.size() + num_threads - 1) / num_threads;

        std::vector<std::future<T>> futures;

        for (std::size_t i = 0; i < items.size(); i += chunk_size) {
            auto end = std::min(i + chunk_size, items.size());
            futures.push_back(pool.submit([&items, &reducer, i, end, &initial]() {
                T result = initial;
                for (std::size_t j = i; j < end; ++j) {
                    result = reducer(result, items[j]);
                }
                return result;
            }));
        }

        T result = initial;
        for (auto& future : futures) {
            result = reducer(result, future.get());
        }

        return result;
    }

    /**
     * Executes multiple tasks in parallel and waits for all to complete.
     *
     * @tparam F Function types.
     * @param tasks The tasks to execute.
     * @param pool The thread pool to use (default: global pool).
     */
    template<typename... F>
    void execute_all(ThreadPool& pool, F&&... tasks) {
        std::vector<std::future<void>> futures;
        futures.reserve(sizeof...(tasks));

        (futures.push_back(pool.submit(std::forward<F>(tasks))), ...);

        for (auto& future : futures) {
            future.get();
        }
    }

}  // namespace bha::parallel

#endif //BUILDTIMEHOTSPOTANALYZER_PARALLEL_HPP