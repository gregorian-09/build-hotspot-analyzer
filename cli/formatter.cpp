//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/formatter.hpp"
#include "bha/cli/progress.hpp"
#include "bha/utils/format_utils.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <nlohmann/json.hpp>

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
    // Formatting Functions (delegate to bha::utils)
    // ============================================================================

    std::string format_duration(const Duration d) {
        return bha::utils::format_duration(d);
    }

    std::string format_ms(const double ms) {
        return bha::utils::format_ms(ms);
    }

    std::string format_percent(const double pct) {
        return bha::utils::format_percent(pct);
    }

    std::string format_size(const std::size_t bytes) {
        return bha::utils::format_size(bytes);
    }

    std::string format_count(const std::size_t count) {
        return bha::utils::format_count(count);
    }

    std::string format_path(const fs::path& path, const std::size_t max_width) {
        return bha::utils::format_path(path, max_width);
    }

    std::string format_timestamp(const Timestamp ts) {
        return bha::utils::format_timestamp(ts);
    }

    std::string colorize_priority(const Priority priority) {
        if (!colors::enabled()) {
            return std::string{to_string(priority)};
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
            return std::string{to_string(type)};
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
        const auto filled = static_cast<std::size_t>(pct * static_cast<double>(width));

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
            row.emplace_back("");
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

        const auto& cache = result.cache_distribution;
        if (cache.total_compilations > 0) {
            out_ << "\n";
            out_ << "Cache Opportunity:    " << format_percent(cache.cache_hit_opportunity_percent) << "\n";
            out_ << "Cache Risky Units:    " << format_count(cache.cache_risk_compilations) << "\n";
            out_ << "Distributed Fit:      " << format_percent(cache.distributed_suitability_score) << "\n";
            out_ << "Tooling Detected:     "
                 << (cache.sccache_detected ? "sccache " : "")
                 << (cache.fastbuild_detected ? "fastbuild " : "")
                 << (cache.cache_wrapper_detected ? "cache-wrapper" : "");
            if (!cache.sccache_detected && !cache.fastbuild_detected && !cache.cache_wrapper_detected) {
                out_ << "none";
            }
            out_ << "\n";
        }

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

            if (!s.hotspot_origins.empty()) {
                out_ << "    Hotspot Origin:\n";
                for (const auto& origin : s.hotspot_origins) {
                    out_ << "      - " << origin.kind;
                    if (origin.estimated_cost > Duration::zero()) {
                        out_ << " (" << format_duration(origin.estimated_cost) << ")";
                    }
                    out_ << "\n";
                    if (!origin.chain.empty()) {
                        out_ << "        " << origin.chain.front() << "\n";
                        for (std::size_t origin_idx = 1; origin_idx < origin.chain.size(); ++origin_idx) {
                            out_ << "        -> " << origin.chain[origin_idx] << "\n";
                        }
                    }
                    if (!origin.note.empty()) {
                        out_ << "        " << origin.note << "\n";
                    }
                }
            }

            out_ << "\n";
        }
    }

    // ============================================================================
    // JSON Output
    // ============================================================================

    namespace json {

        namespace json_detail {
            using nlohmann::json;

            inline json perf_to_json(const analyzers::PerformanceAnalysisResult& perf) {
                json j;
                j["total_build_time_ns"] = perf.total_build_time.count();
                j["total_files"] = perf.total_files;
                j["avg_file_time_ns"] = perf.avg_file_time.count();
                j["median_file_time_ns"] = perf.median_file_time.count();
                j["p90_file_time_ns"] = perf.p90_file_time.count();
                j["p99_file_time_ns"] = perf.p99_file_time.count();
                return j;
            }

            inline json deps_to_json(const analyzers::DependencyAnalysisResult& deps) {
                json j;
                j["total_includes"] = deps.total_includes;
                j["unique_headers"] = deps.unique_headers;
                j["max_include_depth"] = deps.max_include_depth;
                j["total_include_time_ns"] = deps.total_include_time.count();
                return j;
            }

            inline json tmpl_to_json(const analyzers::TemplateAnalysisResult& tmpl) {
                json j;
                j["total_time_ns"] = tmpl.total_template_time.count();
                j["total_instantiations"] = tmpl.total_instantiations;
                j["time_percent"] = tmpl.template_time_percent;
                return j;
            }

            inline json cache_to_json(const analyzers::CacheDistributionAnalysisResult& cache) {
                json j;
                j["total_compilations"] = cache.total_compilations;
                j["cache_friendly_compilations"] = cache.cache_friendly_compilations;
                j["cache_risk_compilations"] = cache.cache_risk_compilations;
                j["cache_hit_opportunity_percent"] = cache.cache_hit_opportunity_percent;
                j["sccache_detected"] = cache.sccache_detected;
                j["fastbuild_detected"] = cache.fastbuild_detected;
                j["cache_wrapper_detected"] = cache.cache_wrapper_detected;
                j["dynamic_macro_risk_count"] = cache.dynamic_macro_risk_count;
                j["profile_or_coverage_risk_count"] = cache.profile_or_coverage_risk_count;
                j["pch_generation_risk_count"] = cache.pch_generation_risk_count;
                j["volatile_path_risk_count"] = cache.volatile_path_risk_count;
                j["heavy_translation_units"] = cache.heavy_translation_units;
                j["homogeneous_command_units"] = cache.homogeneous_command_units;
                j["distributed_suitability_score"] = cache.distributed_suitability_score;
                return j;
            }
        }

        std::string to_json(const analyzers::AnalysisResult& result, const bool pretty) {
            using json_detail::json;
            json j;
            j["bha_version"] = "0.1.0";
            j["performance"] = json_detail::perf_to_json(result.performance);
            j["dependencies"] = json_detail::deps_to_json(result.dependencies);
            j["templates"] = json_detail::tmpl_to_json(result.templates);
            j["cache_distribution"] = json_detail::cache_to_json(result.cache_distribution);
            return j.dump(pretty ? 2 : -1);
        }

        std::string to_json(const std::vector<Suggestion>& suggestions, const bool pretty) {
            using json_detail::json;
            json arr = json::array();
            for (const auto& s : suggestions) {
                const auto mode = resolve_application_mode(s);
                json j;
                j["id"] = s.id;
                j["type"] = to_string(s.type);
                j["priority"] = to_string(s.priority);
                j["confidence"] = s.confidence;
                j["title"] = s.title;
                j["description"] = s.description;
                if (!s.rationale.empty()) {
                    j["rationale"] = s.rationale;
                }
                j["estimated_savings_ns"] = s.estimated_savings.count();
                j["estimated_savings_percent"] = s.estimated_savings_percent;
                j["is_safe"] = s.is_safe;
                j["application_mode"] = to_string(mode);
                if (s.refactor_class_name) {
                    j["refactor_class_name"] = *s.refactor_class_name;
                }
                if (s.refactor_compile_commands_path) {
                    j["refactor_compile_commands_path"] = s.refactor_compile_commands_path->string();
                }
                if (s.application_summary) {
                    j["application_summary"] = *s.application_summary;
                }
                if (s.application_guidance) {
                    j["application_guidance"] = *s.application_guidance;
                }
                if (s.auto_apply_blocked_reason) {
                    j["auto_apply_blocked_reason"] = *s.auto_apply_blocked_reason;
                }

                json target_file;
                target_file["path"] = s.target_file.path.string();
                target_file["line_start"] = s.target_file.line_start;
                target_file["line_end"] = s.target_file.line_end;
                target_file["action"] = to_string(s.target_file.action);
                j["target_file"] = std::move(target_file);

                if (!s.secondary_files.empty()) {
                    json sec_arr = json::array();
                    for (const auto& sf : s.secondary_files) {
                        json sfj;
                        sfj["path"] = sf.path.string();
                        sfj["line_start"] = sf.line_start;
                        sfj["action"] = to_string(sf.action);
                        sec_arr.push_back(std::move(sfj));
                    }
                    j["secondary_files"] = std::move(sec_arr);
                }

                if (!s.before_code.code.empty()) {
                    json bc;
                    bc["file"] = s.before_code.file.string();
                    bc["line"] = s.before_code.line;
                    bc["code"] = s.before_code.code;
                    j["before_code"] = std::move(bc);
                }

                if (!s.after_code.code.empty()) {
                    json ac;
                    ac["file"] = s.after_code.file.string();
                    ac["line"] = s.after_code.line;
                    ac["code"] = s.after_code.code;
                    j["after_code"] = std::move(ac);
                }

                if (!s.edits.empty()) {
                    json edits_arr = json::array();
                    for (const auto& edit : s.edits) {
                        json ej;
                        ej["file"] = edit.file.string();
                        ej["start_line"] = edit.start_line;
                        ej["start_col"] = edit.start_col;
                        ej["end_line"] = edit.end_line;
                        ej["end_col"] = edit.end_col;
                        ej["new_text"] = edit.new_text;
                        edits_arr.push_back(std::move(ej));
                    }
                    j["edits"] = std::move(edits_arr);
                }

                if (!s.implementation_steps.empty()) {
                    json steps = json::array();
                    for (const auto& step : s.implementation_steps) {
                        steps.push_back(step);
                    }
                    j["implementation_steps"] = std::move(steps);
                }

                json impact;
                impact["total_files_affected"] = s.impact.total_files_affected;
                impact["cumulative_savings_ns"] = s.impact.cumulative_savings.count();
                impact["rebuild_files_count"] = s.impact.rebuild_files_count;
                j["impact"] = std::move(impact);

                if (!s.caveats.empty()) {
                    json caveats_arr = json::array();
                    for (const auto& cv : s.caveats) {
                        caveats_arr.push_back(cv);
                    }
                    j["caveats"] = std::move(caveats_arr);
                }

                if (!s.verification.empty()) {
                    j["verification"] = s.verification;
                }
                if (s.documentation_link) {
                    j["documentation_link"] = *s.documentation_link;
                }

                if (!s.hotspot_origins.empty()) {
                    json origins = json::array();
                    for (const auto& origin : s.hotspot_origins) {
                        json oj;
                        oj["kind"] = origin.kind;
                        oj["source"] = origin.source.string();
                        oj["target"] = origin.target.string();
                        oj["estimated_cost_ns"] = origin.estimated_cost.count();
                        oj["note"] = origin.note;
                        json chain = json::array();
                        for (const auto& ch : origin.chain) {
                            chain.push_back(ch);
                        }
                        oj["chain"] = std::move(chain);
                        origins.push_back(std::move(oj));
                    }
                    j["hotspot_origins"] = std::move(origins);
                }

                j["file"] = s.target_file.path.string();
                arr.push_back(std::move(j));
            }
            return arr.dump(pretty ? 2 : -1);
        }

        std::string to_json(const std::vector<analyzers::FileAnalysisResult>& files, const bool pretty) {
            using json_detail::json;
            json arr = json::array();
            for (const auto& f : files) {
                json j;
                j["file"] = f.file.string();
                j["compile_time_ns"] = f.compile_time.count();
                j["frontend_time_ns"] = f.frontend_time.count();
                j["backend_time_ns"] = f.backend_time.count();
                j["time_percent"] = f.time_percent;
                j["include_count"] = f.include_count;
                j["template_count"] = f.template_count;
                arr.push_back(std::move(j));
            }
            return arr.dump(pretty ? 2 : -1);
        }

    }  // namespace json
}  // namespace bha::cli
