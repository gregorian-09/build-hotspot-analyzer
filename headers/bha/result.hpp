//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_RESULT_HPP
#define BUILDTIMEHOTSPOTANALYZER_RESULT_HPP

/**
 * @file result.hpp
 * @brief Result type for error handling without exceptions.
 *
 * Result<T, E> is a discriminated union that holds either a success value of
 * type T or an error value of type E. This enables explicit error handling
 * and makes error paths visible in the type system.
 *
 * Usage:
 * @code
 *     Result<int, Error> divide(int a, int b) {
 *         if (b == 0) {
 *             return Result<int, Error>::failure(Error::invalid_argument("division by zero"));
 *         }
 *         return Result<int, Error>::success(a / b);
 *     }
 *
 *     auto result = divide(10, 2);
 *     if (result.is_ok()) {
 *         std::cout << "Result: " << result.value() << std::endl;
 *     } else {
 *         std::cerr << "Error: " << result.error().message() << std::endl;
 *     }
 * @endcode
 *
 * Monadic operations:
 * @code
 *     auto doubled = result.map([](int x) { return x * 2; });
 *     auto chained = result.and_then([](int x) { return divide(x, 2); });
 * @endcode
 */

#include <variant>
#include <optional>
#include <utility>
#include <type_traits>
#include <stdexcept>

namespace bha {

    /**
     * Tag type for constructing a success Result.
     */
    struct SuccessTag {};

    /**
     * Tag type for constructing a failure Result.
     */
    struct FailureTag {};

    /**
     * Inline constexpr instances for tagged construction.
     */
    inline constexpr SuccessTag success_tag{};
    inline constexpr FailureTag failure_tag{};

    /**
     * A type that represents either a successful value or an error.
     *
     * @tparam T The type of the success value.
     * @tparam E The type of the error value. Defaults to class Error.
     *
     * Result is never empty - it always contains either a T or an E.
     * Move and copy semantics follow the contained type.
     */
    template<typename T, typename E>
    class Result {
    public:
        using value_type = T;
        using error_type = E;

        /**
         * Constructs a successful Result containing the given value.
         *
         * @param value The success value to store.
         * @return A Result in the success state.
         */
        static Result success(T value) {
            return Result(success_tag, std::move(value));
        }

        /**
         * Constructs a failed Result containing the given error.
         *
         * @param error The error value to store.
         * @return A Result in the failure state.
         */
        static Result failure(E error) {
            return Result(failure_tag, std::move(error));
        }

        Result(SuccessTag, T value) : data_(std::in_place_index<0>, std::move(value)) {}
        Result(FailureTag, E error) : data_(std::in_place_index<1>, std::move(error)) {}

        Result(const Result&) = default;
        Result(Result&&) noexcept = default;
        Result& operator=(const Result&) = default;
        Result& operator=(Result&&) noexcept = default;
        ~Result() = default;

        /**
         * Checks if the Result contains a success value.
         */
        [[nodiscard]] bool is_ok() const noexcept {
            return data_.index() == 0;
        }

        /**
         * Checks if the Result contains an error.
         */
        [[nodiscard]] bool is_err() const noexcept {
            return data_.index() == 1;
        }

        /**
         * Explicit bool conversion. True if success, false if error.
         */
        explicit operator bool() const noexcept {
            return is_ok();
        }

        /**
         * Returns a reference to the success value.
         * @throws std::logic_error if the Result contains an error.
         */
        T& value() & {
            if (is_err()) {
                throw std::logic_error("Result::value() called on error result");
            }
            return std::get<0>(data_);
        }

        /**
         * Returns a const reference to the success value.
         * @throws std::logic_error if the Result contains an error.
         */
        const T& value() const& {
            if (is_err()) {
                throw std::logic_error("Result::value() called on error result");
            }
            return std::get<0>(data_);
        }

        /**
         * Returns an rvalue reference to the success value.
         * @throws std::logic_error if the Result contains an error.
         */
        T&& value() && {
            if (is_err()) {
                throw std::logic_error("Result::value() called on error result");
            }
            return std::get<0>(std::move(data_));
        }

        /**
         * Returns a reference to the error value.
         * @throws std::logic_error if the Result contains a success value.
         */
        E& error() & {
            if (is_ok()) {
                throw std::logic_error("Result::error() called on success result");
            }
            return std::get<1>(data_);
        }

        /**
         * Returns a const reference to the error value.
         * @throws std::logic_error if the Result contains a success value.
         */
        const E& error() const& {
            if (is_ok()) {
                throw std::logic_error("Result::error() called on success result");
            }
            return std::get<1>(data_);
        }

        /**
         * Returns the success value, or the provided default if this is an error.
         *
         * @param default_value The value to return if this Result is an error.
         * @return The success value or the default.
         */
        T value_or(T default_value) const& {
            if (is_ok()) {
                return std::get<0>(data_);
            }
            return default_value;
        }

        T value_or(T default_value) && {
            if (is_ok()) {
                return std::get<0>(std::move(data_));
            }
            return default_value;
        }

        /**
         * Applies a function to the success value, returning a new Result.
         *
         * If this Result is an error, returns a Result with the same error.
         * If this Result is a success, applies f to the value and wraps the
         * result in a new success Result.
         *
         * @tparam F A callable type that takes T and returns U.
         * @param f The function to apply.
         * @return Result<U, E> containing either f(value) or the original error.
         */
        template<typename F>
        auto map(F&& f) const& -> Result<std::invoke_result_t<F, const T&>, E> {
            using U = std::invoke_result_t<F, const T&>;
            if (is_ok()) {
                return Result<U, E>::success(std::forward<F>(f)(std::get<0>(data_)));
            }
            return Result<U, E>::failure(std::get<1>(data_));
        }

        template<typename F>
        auto map(F&& f) && -> Result<std::invoke_result_t<F, T&&>, E> {
            using U = std::invoke_result_t<F, T&&>;
            if (is_ok()) {
                return Result<U, E>::success(std::forward<F>(f)(std::get<0>(std::move(data_))));
            }
            return Result<U, E>::failure(std::get<1>(std::move(data_)));
        }

        /**
         * Applies a function that returns a Result to the success value.
         *
         * Similar to map(), but the function f returns a Result instead of a
         * plain value. This enables chaining operations that may fail.
         *
         * @tparam F A callable that takes T and returns Result<U, E>.
         * @param f The function to apply.
         * @return The result of f(value) or the original error.
         */
        template<typename F>
        auto and_then(F&& f) const& -> std::invoke_result_t<F, const T&> {
            if (is_ok()) {
                return std::forward<F>(f)(std::get<0>(data_));
            }
            using ResultType = std::invoke_result_t<F, const T&>;
            return ResultType::failure(std::get<1>(data_));
        }

        template<typename F>
        auto and_then(F&& f) && -> std::invoke_result_t<F, T&&> {
            if (is_ok()) {
                return std::forward<F>(f)(std::get<0>(std::move(data_)));
            }
            using ResultType = std::invoke_result_t<F, T&&>;
            return ResultType::failure(std::get<1>(std::move(data_)));
        }

        /**
         * Applies a function to the error value, returning a new Result.
         *
         * If this Result is a success, returns a Result with the same value.
         * If this Result is an error, applies f to the error.
         *
         * @tparam F A callable that takes E and returns Result<T, E2>.
         * @param f The function to apply to the error.
         * @return The original success or the result of f(error).
         */
        template<typename F>
        auto or_else(F&& f) const& -> std::invoke_result_t<F, const E&> {
            if (is_ok()) {
                using ResultType = std::invoke_result_t<F, const E&>;
                return ResultType::success(std::get<0>(data_));
            }
            return std::forward<F>(f)(std::get<1>(data_));
        }

        template<typename F>
        auto or_else(F&& f) && -> std::invoke_result_t<F, E&&> {
            if (is_ok()) {
                using ResultType = std::invoke_result_t<F, E&&>;
                return ResultType::success(std::get<0>(std::move(data_)));
            }
            return std::forward<F>(f)(std::get<1>(std::move(data_)));
        }

    private:
        std::variant<T, E> data_;
    };

    /**
     * Specialization for void success type.
     *
     * Used for operations that can fail but don't return a value on success.
     */
    template<typename E>
    class Result<void, E> {
    public:
        using value_type = void;
        using error_type = E;

        static Result success() {
            return Result(success_tag);
        }

        static Result failure(E error) {
            return Result(failure_tag, std::move(error));
        }

        explicit Result(SuccessTag) : error_(std::nullopt) {}
        Result(FailureTag, E error) : error_(std::move(error)) {}

        Result(const Result&) = default;
        Result(Result&&) noexcept = default;
        Result& operator=(const Result&) = default;
        Result& operator=(Result&&) noexcept = default;
        ~Result() = default;

        [[nodiscard]] bool is_ok() const noexcept {
            return !error_.has_value();
        }

        [[nodiscard]] bool is_err() const noexcept {
            return error_.has_value();
        }

        explicit operator bool() const noexcept {
            return is_ok();
        }

        E& error() & {
            if (is_ok()) {
                throw std::logic_error("Result::error() called on success result");
            }
            return *error_;
        }

        const E& error() const& {
            if (is_ok()) {
                throw std::logic_error("Result::error() called on success result");
            }
            return *error_;
        }

        template<typename F>
        auto and_then(F&& f) const& -> std::invoke_result_t<F> {
            if (is_ok()) {
                return std::forward<F>(f)();
            }
            using ResultType = std::invoke_result_t<F>;
            return ResultType::failure(*error_);
        }

    private:
        std::optional<E> error_;
    };

}  // namespace bha

#endif //BUILDTIMEHOTSPOTANALYZER_RESULT_HPP