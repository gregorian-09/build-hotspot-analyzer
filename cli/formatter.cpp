//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/formatter.hpp"
#include "bha/cli/progress.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

namespace bha::cli
{
    // ============================================================================
    // Colors
    // ============================================================================

    namespace colors {

        static bool g_colors_enabled = true;

        const char* RESET = "\033[0m";
        const char* BOLD = "\033[1m";
        const char* DIM = "\033[2m";
        const char* UNDERLINE = "\033[4m";

        const char* RED = "\033[31m";
        const char* GREEN = "\033[32m";
        const char* YELLOW = "\033[33m";
        const char* BLUE = "\033[34m";
        const char* MAGENTA = "\033[35m";
        const char* CYAN = "\033[36m";
        const char* WHITE = "\033[37m";

        const char* BG_RED = "\033[41m";
        const char* BG_GREEN = "\033[42m";
        const char* BG_YELLOW = "\033[43m";

        bool enabled() {
            return g_colors_enabled && is_tty();
        }

        void set_enabled(const bool enable) {
            g_colors_enabled = enable;
        }

    }  // namespace colors

    // ============================================================================
    // Formatting Functions
    // ============================================================================

    std::string format_duration(const Duration d) {
        auto ns = d.count();
        if (ns < 0) ns = 0;

        const auto us = ns / 1000;
        const auto ms = us / 1000;
        const auto seconds = ms / 1000;
        const auto minutes = seconds / 60;
        const auto hours = minutes / 60;

        std::ostringstream ss;

        if (hours > 0) {
            ss << hours << "h " << (minutes % 60) << "m " << (seconds % 60) << "s";
        } else if (minutes > 0) {
            ss << minutes << "m " << (seconds % 60) << "." << ((ms % 1000) / 100) << "s";
        } else if (seconds > 0) {
            ss << seconds << "." << std::setfill('0') << std::setw(2) << ((ms % 1000) / 10) << "s";
        } else if (ms > 0) {
            ss << ms << "." << ((us % 1000) / 100) << "ms";
        } else if (us > 0) {
            ss << us << "μs";
        } else {
            ss << ns << "ns";
        }

        return ss.str();
    }

    std::string format_ms(const double ms) {
        std::ostringstream ss;
        if (ms >= 1000.0) {
            ss << std::fixed << std::setprecision(2) << (ms / 1000.0) << "s";
        } else if (ms >= 1.0) {
            ss << std::fixed << std::setprecision(1) << ms << "ms";
        } else {
            ss << std::fixed << std::setprecision(2) << (ms * 1000.0) << "μs";
        }
        return ss.str();
    }

    std::string format_percent(const double pct) {
        std::ostringstream ss;
        // pct is already a percentage value (e.g., 11.1 for 11.1%), not a ratio
        ss << std::fixed << std::setprecision(1) << pct << "%";
        return ss.str();
    }

    std::string format_size(const std::size_t bytes) {
        int unit_idx = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit_idx < 4) {
            size /= 1024.0;
            unit_idx++;
        }

        std::ostringstream ss;
        if (unit_idx == 0) {
            ss << bytes << " B";
        } else
        {
            const char* units[] = {"B", "KB", "MB", "GB", "TB"};
            ss << std::fixed << std::setprecision(1) << size << " " << units[unit_idx];
        }
        return ss.str();
    }

    std::string format_count(const std::size_t count) {
        std::string result = std::to_string(count);

        // Add comma separators
        int insert_pos = static_cast<int>(result.length()) - 3;
        while (insert_pos > 0) {
            result.insert(static_cast<std::size_t>(insert_pos), ",");
            insert_pos -= 3;
        }

        return result;
    }

    std::string format_path(const fs::path& path, const std::size_t max_width) {
        std::string str = path.string();
        if (str.length() <= max_width) {
            return str;
        }

        // Truncate from the beginning
        const std::string ellipsis = "...";
        return ellipsis + str.substr(str.length() - max_width + ellipsis.length());
    }

    std::string format_timestamp(const Timestamp ts) {
        auto time_t = std::chrono::system_clock::to_time_t(ts);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &time_t);
#else
        localtime_r(&time_t, &tm);
#endif

        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string colorize_priority(const Priority priority) {
        if (!colors::enabled()) {
            return std::string(to_string(priority));
        }

        std::string result;
        switch (priority) {
        case Priority::Critical:
            result = std::string(colors::RED) + colors::BOLD + "CRITICAL" + colors::RESET;
            break;
        case Priority::High:
            result = std::string(colors::YELLOW) + "HIGH" + colors::RESET;
            break;
        case Priority::Medium:
            result = std::string(colors::CYAN) + "MEDIUM" + colors::RESET;
            break;
        case Priority::Low:
            result = std::string(colors::DIM) + "LOW" + colors::RESET;
            break;
        }
        return result;
    }

    std::string colorize_type(const SuggestionType type) {
        if (!colors::enabled()) {
            return std::string(to_string(type));
        }

        const std::string name = to_string(type);
        return std::string(colors::BLUE) + name + colors::RESET;
    }

    std::string colorize_duration(const Duration d, const Duration warning_threshold, const Duration critical_threshold) {
        std::string formatted = format_duration(d);

        if (!colors::enabled()) {
            return formatted;
        }

        if (d >= critical_threshold) {
            return std::string(colors::RED) + colors::BOLD + formatted + colors::RESET;
        }
        if (d >= warning_threshold) {
            return std::string(colors::YELLOW) + formatted + colors::RESET;
        }
        return std::string(colors::GREEN) + formatted + colors::RESET;
    }

    std::string bar_graph(const double value, double max_value, const std::size_t width) {
        if (max_value <= 0) max_value = 1.0;
        const double pct = std::min(value / max_value, 1.0);
        const std::size_t filled = static_cast<std::size_t>(pct * static_cast<double>(width));

        std::string result;

        if (colors::enabled()) {
            const char* color = colors::GREEN;
            if (pct > 0.75) color = colors::RED;
            else if (pct > 0.5) color = colors::YELLOW;

            result += color;
            for (std::size_t i = 0; i < filled; ++i) {
                result += "█";
            }
            result += colors::RESET;
            result += colors::DIM;
            for (std::size_t i = filled; i < width; ++i) {
                result += "░";
            }
            result += colors::RESET;
        } else {
            for (std::size_t i = 0; i < filled; ++i) {
                result += "#";
            }
            for (std::size_t i = filled; i < width; ++i) {
                result += "-";
            }
        }

        return result;
    }

    // ============================================================================
    // Table Implementation
    // ============================================================================

    Table::Table(std::vector<Column> columns)
        : columns_(std::move(columns))
    {}

    void Table::add_row(Row row) {
        // Pad row to match column count
        while (row.size() < columns_.size()) {
            row.push_back("");
        }
        rows_.push_back(std::move(row));
        separators_.push_back(false);
    }

    void Table::add_separator() {
        if (!separators_.empty()) {
            separators_.back() = true;
        }
    }

    void Table::clear() {
        rows_.clear();
        separators_.clear();
    }

    void Table::calculate_widths() {
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].width == 0) {
                // Auto-calculate
                std::size_t max_width = columns_[i].header.length();
                for (const auto& row : rows_) {
                    if (i < row.size() && row[i].length() > max_width) {
                        max_width = row[i].length();
                    }
                }
                columns_[i].width = max_width;
            }
        }
    }

    std::string Table::render() const {
        std::ostringstream ss;
        render(ss);
        return ss.str();
    }

    void Table::render(std::ostream& out) const {
        // Make a mutable copy to calculate widths
        Table temp = *this;
        temp.calculate_widths();

        auto render_row = [&](const Row& row, const bool is_header = false) {
            for (std::size_t i = 0; i < temp.columns_.size(); ++i) {
                const auto& col = temp.columns_[i];
                std::string cell = i < row.size() ? row[i] : "";

                // Truncate if too long
                if (cell.length() > col.width) {
                    cell = cell.substr(0, col.width - 3) + "...";
                }

                if (is_header && colors::enabled()) {
                    out << colors::BOLD;
                }

                if (col.right_align) {
                    out << std::right << std::setw(static_cast<int>(col.width)) << cell;
                } else {
                    out << std::left << std::setw(static_cast<int>(col.width)) << cell;
                }

                if (is_header && colors::enabled()) {
                    out << colors::RESET;
                }

                if (i < temp.columns_.size() - 1) {
                    out << "  ";  // Column separator
                }
            }
            out << "\n";
        };

        auto render_separator = [&]() {
            for (std::size_t i = 0; i < temp.columns_.size(); ++i) {
                out << std::string(temp.columns_[i].width, '-');
                if (i < temp.columns_.size() - 1) {
                    out << "--";
                }
            }
            out << "\n";
        };

        if (show_headers_) {
            Row header;
            for (const auto& col : temp.columns_) {
                header.push_back(col.header);
            }
            render_row(header, true);
            render_separator();
        }

        for (std::size_t i = 0; i < temp.rows_.size(); ++i) {
            render_row(temp.rows_[i]);
            if (i < temp.separators_.size() && temp.separators_[i]) {
                render_separator();
            }
        }
    }

    // ============================================================================
    // SummaryPrinter Implementation
    // ============================================================================

    SummaryPrinter::SummaryPrinter(std::ostream& out)
        : out_(out)
    {}

    void SummaryPrinter::print_build_summary(const analyzers::AnalysisResult& result) const
    {
        out_ << "\n";
        if (colors::enabled()) {
            out_ << colors::BOLD << "Build Summary" << colors::RESET << "\n";
        } else {
            out_ << "Build Summary\n";
        }
        out_ << std::string(60, '=') << "\n\n";

        const auto& perf = result.performance;

        out_ << "Total Build Time:     " << format_duration(perf.total_build_time) << "\n";
        out_ << "Files Analyzed:       " << format_count(perf.total_files) << "\n";
        out_ << "Average File Time:    " << format_duration(perf.avg_file_time) << "\n";
        out_ << "Median File Time:     " << format_duration(perf.median_file_time) << "\n";
        out_ << "P90 File Time:        " << format_duration(perf.p90_file_time) << "\n";
        out_ << "P99 File Time:        " << format_duration(perf.p99_file_time) << "\n";

        out_ << "\n";

        const auto& deps = result.dependencies;
        out_ << "Total Includes:       " << format_count(deps.total_includes) << "\n";
        out_ << "Unique Headers:       " << format_count(deps.unique_headers) << "\n";
        out_ << "Max Include Depth:    " << deps.max_include_depth << "\n";
        out_ << "Total Include Time:   " << format_duration(deps.total_include_time) << "\n";

        if (!deps.circular_dependencies.empty()) {
            out_ << "\n";
            if (colors::enabled()) {
                out_ << colors::YELLOW << "Warning: " << colors::RESET;
            } else {
                out_ << "Warning: ";
            }
            out_ << deps.circular_dependencies.size() << " circular dependencies detected\n";
        }

        out_ << "\n";

        const auto& tmpl = result.templates;
        out_ << "Template Time:        " << format_duration(tmpl.total_template_time);
        out_ << " (" << format_percent(tmpl.template_time_percent) << " of total)\n";
        out_ << "Total Instantiations: " << format_count(tmpl.total_instantiations) << "\n";

        out_ << "\n";
    }

    void SummaryPrinter::print_file_summary(const std::vector<analyzers::FileAnalysisResult>& files, const std::size_t limit) const
    {
        if (files.empty()) return;

        if (colors::enabled()) {
            out_ << colors::BOLD << "Slowest Files" << colors::RESET << "\n";
        } else {
            out_ << "Slowest Files\n";
        }
        out_ << std::string(60, '-') << "\n\n";

        Table table({
            {"#", 3, true, std::nullopt},
            {"File", 40, false, std::nullopt},
            {"Time", 10, true, std::nullopt},
            {"%", 6, true, std::nullopt},
            {"Graph", 20, false, std::nullopt}
        });

        double max_time = 0;
        if (!files.empty()) {
            max_time = std::chrono::duration<double, std::milli>(files[0].compile_time).count();
        }

        const std::size_t count = std::min(limit == 0 ? files.size() : limit, files.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& file = files[i];
            const double ms = std::chrono::duration<double, std::milli>(file.compile_time).count();

            table.add_row({
                std::to_string(i + 1),
                format_path(file.file, 40),
                format_ms(ms),
                format_percent(file.time_percent),
                bar_graph(ms, max_time, 20)
            });
        }

        table.render(out_);
        out_ << "\n";
    }

    void SummaryPrinter::print_include_summary(const analyzers::DependencyAnalysisResult& deps, const std::size_t limit) const
    {
        if (deps.headers.empty()) return;

        if (colors::enabled()) {
            out_ << colors::BOLD << "Most Expensive Headers" << colors::RESET << "\n";
        } else {
            out_ << "Most Expensive Headers\n";
        }
        out_ << std::string(60, '-') << "\n\n";

        Table table({
            {"#", 3, true, std::nullopt},
            {"Header", 40, false, std::nullopt},
            {"Parse Time", 12, true, std::nullopt},
            {"Inclusions", 10, true, std::nullopt},
            {"Impact", 8, true, std::nullopt}
        });

        auto sorted = deps.headers;
        std::ranges::sort(sorted,
                          [](const auto& a, const auto& b) { return a.impact_score > b.impact_score; });

        const std::size_t count = std::min(limit == 0 ? sorted.size() : limit, sorted.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& header = sorted[i];
            table.add_row({
                std::to_string(i + 1),
                format_path(header.path, 40),
                format_duration(header.total_parse_time),
                std::to_string(header.inclusion_count),
                format_percent(header.impact_score)
            });
        }

        table.render(out_);
        out_ << "\n";
    }

    void SummaryPrinter::print_template_summary(const analyzers::TemplateAnalysisResult& templates, const std::size_t limit, const bool no_truncate) const
    {
        if (templates.templates.empty()) return;

        if (colors::enabled()) {
            out_ << colors::BOLD << "Most Expensive Templates" << colors::RESET << "\n";
        } else {
            out_ << "Most Expensive Templates\n";
        }
        out_ << std::string(60, '-') << "\n\n";

        // Use wider column when not truncating
        const std::size_t name_width = no_truncate ? 0 : 50;  // 0 = auto-calculate

        Table table({
            {"#", 3, true, std::nullopt},
            {"Template", name_width, false, std::nullopt},
            {"Time", 10, true, std::nullopt},
            {"Count", 8, true, std::nullopt},
            {"%", 6, true, std::nullopt}
        });

        const std::size_t count = std::min(limit == 0 ? templates.templates.size() : limit, templates.templates.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& tmpl = templates.templates[i];
            std::string display_name = !tmpl.full_signature.empty() ? tmpl.full_signature : tmpl.name;
            if (!no_truncate && display_name.length() > 50) {
                display_name = display_name.substr(0, 47) + "...";
            }
            table.add_row({
                std::to_string(i + 1),
                display_name,
                format_duration(tmpl.total_time),
                std::to_string(tmpl.instantiation_count),
                format_percent(tmpl.time_percent)
            });
        }

        table.render(out_);
        out_ << "\n";
    }

    void SummaryPrinter::print_suggestions(const std::vector<Suggestion>& suggestions, const std::size_t limit) const
    {
        if (suggestions.empty()) {
            out_ << "No suggestions generated.\n\n";
            return;
        }

        if (colors::enabled()) {
            out_ << colors::BOLD << "Optimization Suggestions" << colors::RESET << "\n";
        } else {
            out_ << "Optimization Suggestions\n";
        }
        out_ << std::string(60, '-') << "\n\n";

        const std::size_t count = std::min(limit == 0 ? suggestions.size() : limit, suggestions.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& s = suggestions[i];

            out_ << "[" << (i + 1) << "] ";
            out_ << colorize_priority(s.priority) << " ";
            out_ << colorize_type(s.type) << "\n";

            if (colors::enabled()) {
                out_ << colors::BOLD << s.title << colors::RESET << "\n";
            } else {
                out_ << s.title << "\n";
            }

            out_ << "    " << s.description << "\n";

            if (!s.target_file.path.empty()) {
                out_ << "    File: " << s.target_file.path.string();
                if (s.target_file.has_line_range()) {
                    out_ << ":" << s.target_file.line_start;
                    if (s.target_file.line_end != s.target_file.line_start) {
                        out_ << "-" << s.target_file.line_end;
                    }
                }
                out_ << "\n";
            }

            out_ << "    Estimated savings: " << format_duration(s.estimated_savings);
            // confidence is stored as 0.0-1.0 ratio, convert to percentage
            out_ << " (confidence: " << format_percent(s.confidence * 100.0) << ")\n";

            if (!s.caveats.empty()) {
                if (colors::enabled()) {
                    out_ << "    " << colors::YELLOW << "Caveats: " << colors::RESET;
                } else {
                    out_ << "    Caveats: ";
                }
                out_ << s.caveats[0] << "\n";
            }

            out_ << "\n";
        }
    }

    // ============================================================================
    // JSON Output
    // ============================================================================

    namespace json {

        std::string escape_string(const std::string& s) {
            std::string result;
            result.reserve(s.size() * 2);
            for (const char c : s) {
                switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
                }
            }
            return result;
        }

        std::string to_json(const analyzers::AnalysisResult& result, bool pretty) {
            std::ostringstream ss;
            const std::string indent = pretty ? "  " : "";
            const std::string nl = pretty ? "\n" : "";

            ss << "{" << nl;

            ss << indent << "\"bha_version\": \"1.0.0\"," << nl;

            ss << indent << "\"performance\": {" << nl;
            ss << indent << indent << "\"total_build_time_ns\": " << result.performance.total_build_time.count() << "," << nl;
            ss << indent << indent << "\"total_files\": " << result.performance.total_files << "," << nl;
            ss << indent << indent << "\"avg_file_time_ns\": " << result.performance.avg_file_time.count() << "," << nl;
            ss << indent << indent << "\"median_file_time_ns\": " << result.performance.median_file_time.count() << "," << nl;
            ss << indent << indent << "\"p90_file_time_ns\": " << result.performance.p90_file_time.count() << "," << nl;
            ss << indent << indent << "\"p99_file_time_ns\": " << result.performance.p99_file_time.count() << nl;
            ss << indent << "}," << nl;

            ss << indent << "\"dependencies\": {" << nl;
            ss << indent << indent << "\"total_includes\": " << result.dependencies.total_includes << "," << nl;
            ss << indent << indent << "\"unique_headers\": " << result.dependencies.unique_headers << "," << nl;
            ss << indent << indent << "\"max_include_depth\": " << result.dependencies.max_include_depth << "," << nl;
            ss << indent << indent << "\"total_include_time_ns\": " << result.dependencies.total_include_time.count() << nl;
            ss << indent << "}," << nl;

            ss << indent << "\"templates\": {" << nl;
            ss << indent << indent << "\"total_time_ns\": " << result.templates.total_template_time.count() << "," << nl;
            ss << indent << indent << "\"total_instantiations\": " << result.templates.total_instantiations << "," << nl;
            ss << indent << indent << "\"time_percent\": " << result.templates.template_time_percent << nl;
            ss << indent << "}" << nl;

            ss << "}";
            return ss.str();
        }

        std::string to_json(const std::vector<Suggestion>& suggestions, bool pretty) {
            std::ostringstream ss;
            const std::string indent = pretty ? "  " : "";
            const std::string nl = pretty ? "\n" : "";

            ss << "[" << nl;

            for (std::size_t i = 0; i < suggestions.size(); ++i) {
                const auto& s = suggestions[i];
                ss << indent << "{" << nl;
                ss << indent << indent << "\"id\": \"" << escape_string(s.id) << "\"," << nl;
                ss << indent << indent << "\"type\": \"" << to_string(s.type) << "\"," << nl;
                ss << indent << indent << "\"priority\": \"" << to_string(s.priority) << "\"," << nl;
                ss << indent << indent << "\"confidence\": " << s.confidence << "," << nl;
                ss << indent << indent << "\"title\": \"" << escape_string(s.title) << "\"," << nl;
                ss << indent << indent << "\"description\": \"" << escape_string(s.description) << "\"," << nl;
                ss << indent << indent << "\"estimated_savings_ns\": " << s.estimated_savings.count() << "," << nl;
                ss << indent << indent << "\"file\": \"" << escape_string(s.target_file.path.string()) << "\"" << nl;
                ss << indent << "}";
                if (i < suggestions.size() - 1) ss << ",";
                ss << nl;
            }

            ss << "]";
            return ss.str();
        }

        std::string to_json(const std::vector<analyzers::FileAnalysisResult>& files, bool pretty) {
            std::ostringstream ss;
            const std::string indent = pretty ? "  " : "";
            const std::string nl = pretty ? "\n" : "";

            ss << "[" << nl;

            for (std::size_t i = 0; i < files.size(); ++i) {
                const auto& f = files[i];
                ss << indent << "{" << nl;
                ss << indent << indent << "\"file\": \"" << escape_string(f.file.string()) << "\"," << nl;
                ss << indent << indent << "\"compile_time_ns\": " << f.compile_time.count() << "," << nl;
                ss << indent << indent << "\"frontend_time_ns\": " << f.frontend_time.count() << "," << nl;
                ss << indent << indent << "\"backend_time_ns\": " << f.backend_time.count() << "," << nl;
                ss << indent << indent << "\"time_percent\": " << f.time_percent << "," << nl;
                ss << indent << indent << "\"include_count\": " << f.include_count << "," << nl;
                ss << indent << indent << "\"template_count\": " << f.template_count << nl;
                ss << indent << "}";
                if (i < files.size() - 1) ss << ",";
                ss << nl;
            }

            ss << "]";
            return ss.str();
        }

    }  // namespace json
}  // namespace bha::cli