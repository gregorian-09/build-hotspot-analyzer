//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/progress.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace bha::cli
{
    // ============================================================================
    // Terminal Utilities
    // ============================================================================

    bool is_tty() {
#ifdef _WIN32
        return _isatty(_fileno(stdout)) != 0;
#else
        return isatty(fileno(stdout)) != 0;
#endif
    }

    std::size_t terminal_width() {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
            return static_cast<std::size_t>(csbi.srWindow.Right - csbi.srWindow.Left + 1);
        }
        return 80;
#else
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
            return w.ws_col;
        }
        return 80;
#endif
    }

    std::string format_duration(std::chrono::milliseconds ms) {
        const auto total_seconds = ms.count() / 1000;
        const auto hours = total_seconds / 3600;
        const auto minutes = (total_seconds % 3600) / 60;
        const auto seconds = total_seconds % 60;

        std::ostringstream ss;
        if (hours > 0) {
            ss << hours << "h " << minutes << "m " << seconds << "s";
        } else if (minutes > 0) {
            ss << minutes << "m " << seconds << "s";
        } else if (total_seconds > 0) {
            ss << seconds << "." << (ms.count() % 1000) / 100 << "s";
        } else {
            ss << ms.count() << "ms";
        }
        return ss.str();
    }

    // ============================================================================
    // ProgressBar Implementation
    // ============================================================================

    ProgressBar::ProgressBar(const std::size_t total, const std::string_view label)
        : ProgressBar(total, label, ProgressStyle{}) {}

    ProgressBar::ProgressBar(const std::size_t total, const std::string_view label, const ProgressStyle& style)
        : total_(total)
        , label_(label)
        , style_(style)
        , start_time_(std::chrono::steady_clock::now())
        , is_tty_(is_tty())
    {
        if (is_tty_) {
            render();
        }
    }

    ProgressBar::~ProgressBar() {
        if (!finished_ && !failed_ && is_tty_) {
            finish();
        }
    }

    void ProgressBar::update(const std::size_t current) {
        current_ = std::min(current, total_);
        if (is_tty_) {
            render();
        }
    }

    void ProgressBar::tick() {
        if (const auto cur = current_.load(); cur < total_) {
            current_ = cur + 1;
            if (is_tty_) {
                render();
            }
        }
    }

    void ProgressBar::set_message(const std::string_view msg) {
        message_ = msg;
        if (is_tty_) {
            render();
        }
    }

    void ProgressBar::finish() {
        if (finished_) return;
        finished_ = true;
        current_ = total_;
        if (is_tty_) {
            render();
            std::cout << "\n" << std::flush;
        } else {
            std::cout << label_ << ": " << total_ << "/" << total_ << " (100%)\n";
        }
    }

    void ProgressBar::fail(const std::string_view reason) {
        if (finished_ || failed_) return;
        failed_ = true;
        if (is_tty_) {
            clear_line();
            std::cout << "\r" << label_ << ": Failed";
            if (!reason.empty()) {
                std::cout << " - " << reason;
            }
            std::cout << "\n" << std::flush;
        } else {
            std::cout << label_ << ": Failed";
            if (!reason.empty()) {
                std::cout << " - " << reason;
            }
            std::cout << "\n";
        }
    }

    double ProgressBar::progress() const {
        if (total_ == 0) return 1.0;
        return static_cast<double>(current_.load()) / static_cast<double>(total_);
    }

    std::chrono::milliseconds ProgressBar::elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time_
        );
    }

    std::chrono::milliseconds ProgressBar::eta() const {
        const auto cur = current_.load();
        if (cur == 0) return std::chrono::milliseconds{0};

        const auto elapsed_ms = elapsed().count();
        const auto remaining = total_ - cur;
        const auto rate = static_cast<double>(cur) / static_cast<double>(elapsed_ms);

        if (rate <= 0) return std::chrono::milliseconds{0};
        return std::chrono::milliseconds{static_cast<long long>(static_cast<double>(remaining) / rate)};
    }

    void ProgressBar::render() const
    {
        clear_line();

        std::ostringstream ss;
        ss << "\r";

        if (!label_.empty()) {
            ss << label_ << " ";
        }

        ss << style_.left_bracket;

        const double pct = progress();
        const std::size_t filled = static_cast<std::size_t>(pct * static_cast<double>(style_.bar_width));

        for (std::size_t i = 0; i < style_.bar_width; ++i) {
            if (i < filled) {
                ss << style_.fill_char;
            } else {
                ss << style_.empty_char;
            }
        }

        ss << style_.right_bracket;

        if (style_.show_percentage) {
            ss << " " << std::fixed << std::setprecision(0) << (pct * 100) << "%";
        }

        if (style_.show_count) {
            ss << " (" << current_.load() << "/" << total_ << ")";
        }

        if (style_.show_eta && !finished_) {
            if (const auto remaining = eta(); remaining.count() > 0) {
                ss << " ETA: " << format_duration(remaining);
            }
        }

        if (!message_.empty()) {
            ss << " " << message_;
        }

        std::cout << ss.str() << std::flush;
    }

    void ProgressBar::clear_line() {
        std::cout << "\r" << std::string(terminal_width(), ' ') << std::flush;
    }

    // ============================================================================
    // Spinner Implementation
    // ============================================================================

    Spinner::Spinner(const std::string_view message)
        : message_(message)
        , is_tty_(is_tty())
    {
        if (is_tty_) {
            render();
        } else {
            std::cout << message_ << "...\n";
        }
    }

    Spinner::~Spinner() {
        if (!stopped_) {
            stop();
        }
    }

    void Spinner::tick() {
        if (stopped_) return;
        frame_ = (frame_ + 1) % FRAME_COUNT;
        if (is_tty_) {
            render();
        }
    }

    void Spinner::set_message(const std::string_view msg) {
        message_ = msg;
        if (is_tty_) {
            render();
        }
    }

    void Spinner::success(const std::string_view msg) {
        stopped_ = true;
        success_ = true;
        if (is_tty_) {
            clear_line();
            std::cout << "\r✓ " << message_;
            if (!msg.empty() && msg != "Done") {
                std::cout << ": " << msg;
            }
            std::cout << "\n" << std::flush;
        } else {
            std::cout << message_ << ": " << msg << "\n";
        }
    }

    void Spinner::fail(const std::string_view msg) {
        stopped_ = true;
        success_ = false;
        if (is_tty_) {
            clear_line();
            std::cout << "\r✗ " << message_;
            if (!msg.empty()) {
                std::cout << ": " << msg;
            }
            std::cout << "\n" << std::flush;
        } else {
            std::cout << message_ << ": " << msg << "\n";
        }
    }

    void Spinner::stop() {
        if (stopped_) return;
        stopped_ = true;
        if (is_tty_) {
            clear_line();
            std::cout << "\r" << std::flush;
        }
    }

    void Spinner::render() const
    {
        clear_line();
        std::cout << "\r" << FRAMES[frame_] << " " << message_ << std::flush;
    }

    void Spinner::clear_line() {
        std::cout << "\r" << std::string(terminal_width(), ' ') << std::flush;
    }

    // ============================================================================
    // MultiProgress Implementation
    // ============================================================================

    MultiProgress::MultiProgress(const std::size_t num_bars)
        : is_tty_(is_tty())
    {
        bars_.reserve(num_bars);
    }

    MultiProgress::~MultiProgress() {
        finish();
    }

    ProgressBar& MultiProgress::bar(const std::size_t index) const
    {
        return *bars_.at(index).bar;
    }

    std::size_t MultiProgress::add_bar(std::size_t total, std::string_view label) {
        BarInfo info;
        info.bar = std::make_unique<ProgressBar>(total, label);
        bars_.push_back(std::move(info));
        return bars_.size() - 1;
    }

    void MultiProgress::render() const
    {
        if (!is_tty_) return;

        // For now, simple sequential rendering
        // Future: Later version would use cursor positioning
        for (auto& info : bars_) {
            info.bar->tick();
        }
    }

    void MultiProgress::finish() const
    {
        for (const auto& [bar, last_render] : bars_) {
            bar->finish();
        }
    }

    // ============================================================================
    // ScopedProgress Implementation
    // ============================================================================

    ScopedProgress::ScopedProgress(std::size_t total, std::string_view label)
        : bar_(std::make_unique<ProgressBar>(total, label))
    {}

    ScopedProgress::~ScopedProgress() {
        if (!failed_) {
            bar_->finish();
        }
    }
}  // namespace bha::cli