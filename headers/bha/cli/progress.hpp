//
// Created by gregorian-rayne on 1/2/26.
//

#ifndef BHA_PROGRESS_HPP
#define BHA_PROGRESS_HPP


/**
 * @file progress.hpp
 * @brief Progress bar and spinner utilities for CLI.
 *
 * Provides terminal-based progress indicators:
 * - Progress bars with percentage
 * - Spinners for indeterminate operations
 * - Multi-line progress tracking
 */

#include <string>
#include <string_view>
#include <chrono>
#include <atomic>
#include <memory>
#include <functional>

namespace bha::cli
{
    /**
     * Style options for progress bars.
     */
    struct ProgressStyle {
        std::string fill_char = "█";
        std::string empty_char = "░";
        std::string left_bracket = "[";
        std::string right_bracket = "]";
        std::size_t bar_width = 40;
        bool show_percentage = true;
        bool show_count = true;
        bool show_eta = true;
        bool use_color = true;
    };

    /**
     * Progress bar for operations with known total.
     */
    class ProgressBar {
    public:
        explicit ProgressBar(std::size_t total, std::string_view label = "");
        ProgressBar(std::size_t total, std::string_view label, const ProgressStyle& style);
        ~ProgressBar();

        /**
         * Updates progress to a specific value.
         */
        void update(std::size_t current);

        /**
         * Increments progress by one.
         */
        void tick();

        /**
         * Sets the current message/label.
         */
        void set_message(std::string_view msg);

        /**
         * Marks the progress as complete.
         */
        void finish();

        /**
         * Marks the progress as failed.
         */
        void fail(std::string_view reason = "");

        /**
         * Returns current progress (0.0 to 1.0).
         */
        [[nodiscard]] double progress() const;

        /**
         * Returns elapsed time.
         */
        [[nodiscard]] std::chrono::milliseconds elapsed() const;

        /**
         * Returns estimated time remaining.
         */
        [[nodiscard]] std::chrono::milliseconds eta() const;

    private:
        void render() const;
        static void clear_line();

        std::size_t total_;
        std::atomic<std::size_t> current_{0};
        std::string label_;
        std::string message_;
        ProgressStyle style_;
        std::chrono::steady_clock::time_point start_time_;
        bool finished_ = false;
        bool failed_ = false;
        bool is_tty_ = true;
    };

    /**
     * Spinner for indeterminate operations.
     */
    class Spinner {
    public:
        explicit Spinner(std::string_view message);
        ~Spinner();

        /**
         * Updates the spinner animation (call periodically).
         */
        void tick();

        /**
         * Sets a new message.
         */
        void set_message(std::string_view msg);

        /**
         * Marks operation as successful.
         */
        void success(std::string_view msg = "Done");

        /**
         * Marks operation as failed.
         */
        void fail(std::string_view msg = "Failed");

        /**
         * Stops the spinner.
         */
        void stop();

    private:
        void render() const;
        static void clear_line();

        std::string message_;
        std::size_t frame_ = 0;
        bool stopped_ = false;
        bool success_ = false;
        bool is_tty_ = true;

        static constexpr const char* FRAMES[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        static constexpr std::size_t FRAME_COUNT = 10;
    };

    /**
     * Multi-progress tracker for parallel operations.
     */
    class MultiProgress {
    public:
        explicit MultiProgress(std::size_t num_bars);
        ~MultiProgress();

        /**
         * Gets a progress bar by index.
         */
        ProgressBar& bar(std::size_t index) const;

        /**
         * Creates a new progress bar and returns its index.
         */
        std::size_t add_bar(std::size_t total, std::string_view label);

        /**
         * Renders all progress bars.
         */
        void render() const;

        /**
         * Marks all as complete.
         */
        void finish() const;

    private:
        struct BarInfo {
            std::unique_ptr<ProgressBar> bar;
            std::string last_render;
        };
        std::vector<BarInfo> bars_;
        bool is_tty_ = true;
    };

    /**
     * RAII wrapper for progress that auto-finishes.
     */
    class ScopedProgress {
    public:
        ScopedProgress(std::size_t total, std::string_view label);
        ~ScopedProgress();

        ProgressBar& bar() const { return *bar_; }

        void update(const std::size_t current) const { bar_->update(current); }
        void tick() const { bar_->tick(); }
        void set_message(const std::string_view msg) const { bar_->set_message(msg); }
        void fail(const std::string_view reason = "") { failed_ = true; bar_->fail(reason); }

    private:
        std::unique_ptr<ProgressBar> bar_;
        bool failed_ = false;
    };

    /**
     * Checks if stdout is a TTY.
     */
    [[nodiscard]] bool is_tty();

    /**
     * Gets terminal width.
     */
    [[nodiscard]] std::size_t terminal_width();

    /**
     * Format a duration for display.
     */
    [[nodiscard]] std::string format_duration(std::chrono::milliseconds ms);

    /**
     * Format a size (bytes) for display.
     */
    [[nodiscard]] std::string format_size(std::size_t bytes);

}  // namespace bha::cli

#endif //BHA_PROGRESS_HPP