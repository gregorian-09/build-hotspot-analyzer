//
// Created by gregorian on 15/10/2025.
//

#ifndef RESULT_H
#define RESULT_H

#include "bha/core/error.h"
#include <variant>
#include <stdexcept>
#include <type_traits>

namespace bha::core {

    /**
     * @brief Represents the result of an operation that can either succeed with a value or fail with an error.
     *
     * @tparam T The type of the successful result value.
     */
    template<typename T>
    class Result {
    public:
        /// Constructs a successful result holding a copy of @p value.
        explicit Result(const T& value) : data_(value) {}

        /// Constructs a successful result by moving @p value.
        explicit Result(T&& value) : data_(std::move(value)) {}

        /// Constructs a failed result holding a copy of @p error.
        explicit Result(const Error& error) : data_(error) {}

        /// Constructs a failed result by moving @p error.
        explicit Result(Error&& error) : data_(std::move(error)) {}

        /// Creates a successful result containing @p value.
        static Result success(const T& value) { return Result(value); }

        /// Creates a successful result by moving @p value.
        static Result success(T&& value) { return Result(std::move(value)); }

        /// Creates a failed result from @p error.
        static Result failure(const Error& error) { return Result(error); }

        /// Creates a failed result by moving @p error.
        static Result failure(Error&& error) { return Result(std::move(error)); }

        /// Creates a failed result from an error code and message.
        static Result failure(const ErrorCode code, std::string message) {
            return Result(make_error(code, std::move(message)));
        }

        /// @return True if this result represents success.
        [[nodiscard]] bool is_success() const { return std::holds_alternative<T>(data_); }

        /// @return True if this result represents failure.
        [[nodiscard]] bool is_failure() const { return std::holds_alternative<Error>(data_); }

        /// Implicit conversion to bool (true if success).
        explicit operator bool() const { return is_success(); }

        /// @return The contained value, or throws if this is a failure.
        const T& value() const & {
            if (!is_success()) throw std::runtime_error("Accessed value of failed Result");
            return std::get<T>(data_);
        }

        /// @return Mutable reference to the contained value, or throws if failure.
        T& value() & {
            if (!is_success()) throw std::runtime_error("Accessed value of failed Result");
            return std::get<T>(data_);
        }

        /// @return Moved contained value, or throws if failure.
        T&& value() && {
            if (!is_success()) throw std::runtime_error("Accessed value of failed Result");
            return std::move(std::get<T>(data_));
        }

        /// @return The contained error, or throws if this is a success.
        [[nodiscard]] const Error& error() const & {
            if (!is_failure()) throw std::runtime_error("Accessed error of successful Result");
            return std::get<Error>(data_);
        }

        /// @return Mutable reference to the contained error, or throws if success.
        Error& error() & {
            if (!is_failure()) throw std::runtime_error("Accessed error of successful Result");
            return std::get<Error>(data_);
        }

        /// @return Moved error object, or throws if success.
        Error&& error() && {
            if (!is_failure()) throw std::runtime_error("Accessed error of successful Result");
            return std::move(std::get<Error>(data_));
        }

        /// Returns the contained value if successful, or @p default_value otherwise.
        T value_or(const T& default_value) const & {
            return is_success() ? std::get<T>(data_) : default_value;
        }

        /// Returns the contained value if successful, or moves @p default_value otherwise.
        T value_or(T&& default_value) && {
            return is_success() ? std::move(std::get<T>(data_)) : std::move(default_value);
        }

        /**
         * @brief Applies a function to the value if successful.
         *
         * @tparam F The callable type.
         * @return A new Result containing the transformed value or the same error.
         */
        template<typename F>
        auto map(F&& func) const & -> Result<decltype(func(std::declval<T>()))> {
            using U = decltype(func(std::declval<T>()));
            if (is_success()) return Result<U>::success(func(std::get<T>(data_)));
            return Result<U>::failure(std::get<Error>(data_));
        }

        template<typename F>
        auto map(F&& func) && -> Result<decltype(func(std::declval<T>()))> {
            using U = decltype(func(std::declval<T>()));
            if (is_success()) return Result<U>::success(func(std::move(std::get<T>(data_))));
            return Result<U>::failure(std::move(std::get<Error>(data_)));
        }

        /**
         * @brief Chains another operation if successful.
         *
         * The function must return another Result type.
         */
        template<typename F>
        auto and_then(F&& func) const & -> decltype(func(std::declval<T>())) {
            if (is_success()) return func(std::get<T>(data_));
            using ReturnType = decltype(func(std::declval<T>()));
            return ReturnType::failure(std::get<Error>(data_));
        }

        template<typename F>
        auto and_then(F&& func) && -> decltype(func(std::declval<T>())) {
            if (is_success()) return func(std::move(std::get<T>(data_)));
            using ReturnType = decltype(func(std::declval<T>()));
            return ReturnType::failure(std::move(std::get<Error>(data_)));
        }

        /**
         * @brief Handles failure by providing a fallback via @p func.
         * @return The result of @p func if failure, otherwise this result.
         */
        template<typename F>
        Result or_else(F&& func) const & {
            if (is_failure()) return func(std::get<Error>(data_));
            return *this;
        }

        template<typename F>
        Result or_else(F&& func) && {
            if (is_failure()) return func(std::move(std::get<Error>(data_)));
            return std::move(*this);
        }

    private:
        std::variant<T, Error> data_;
    };

    /**
     * @brief Specialization for Result<void>, representing success or failure without a value.
     */
    template<>
    class Result<void> {
    public:
        Result() : data_(std::monostate{}) {}
        explicit Result(const Error& error) : data_(error) {}
        explicit Result(Error&& error) : data_(std::move(error)) {}

        static Result success() { return {}; }
        static Result failure(const Error& error) { return Result(error); }
        static Result failure(Error&& error) { return Result(std::move(error)); }
        static Result failure(const ErrorCode code, std::string message) {
            return Result(make_error(code, std::move(message)));
        }

        [[nodiscard]] bool is_success() const { return std::holds_alternative<std::monostate>(data_); }
        [[nodiscard]] bool is_failure() const { return std::holds_alternative<Error>(data_); }
        explicit operator bool() const { return is_success(); }

        [[nodiscard]] const Error& error() const & {
            if (!is_failure()) throw std::runtime_error("Accessed error of successful Result");
            return std::get<Error>(data_);
        }

        Error& error() & {
            if (!is_failure()) throw std::runtime_error("Accessed error of successful Result");
            return std::get<Error>(data_);
        }

        Error&& error() && {
            if (!is_failure()) throw std::runtime_error("Accessed error of successful Result");
            return std::move(std::get<Error>(data_));
        }

        template<typename F>
        Result and_then(F&& func) const & {
            if (is_success()) return func();
            return failure(std::get<Error>(data_));
        }

        template<typename F>
        Result and_then(F&& func) && {
            if (is_success()) return func();
            return failure(std::move(std::get<Error>(data_)));
        }

        template<typename F>
        Result or_else(F&& func) const & {
            if (is_failure()) return func(std::get<Error>(data_));
            return *this;
        }

        template<typename F>
        Result or_else(F&& func) && {
            if (is_failure()) return func(std::move(std::get<Error>(data_)));
            return std::move(*this);
        }

    private:
        std::variant<std::monostate, Error> data_;
    };

    /// Helper for constructing successful results.
    template<typename T>
    Result<T> Ok(T&& value) { return Result<T>::success(std::forward<T>(value)); }

    /// Helper for constructing successful void results.
    inline Result<void> Ok() { return Result<void>::success(); }

    /// Helper for constructing failed results.
    template<typename T>
    Result<T> Err(Error error) { return Result<T>::failure(std::move(error)); }

    /// Helper for constructing failed results with code + message.
    template<typename T>
    Result<T> Err(ErrorCode code, std::string message) {
        return Result<T>::failure(code, std::move(message));
    }

    /// Helper for constructing failed void results.
    inline Result<void> Err(Error error) { return Result<void>::failure(std::move(error)); }

    inline Result<void> Err(const ErrorCode code, std::string message) {
        return Result<void>::failure(code, std::move(message));
    }

}

#endif //RESULT_H
