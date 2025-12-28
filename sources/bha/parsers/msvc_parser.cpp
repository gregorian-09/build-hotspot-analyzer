//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/msvc_parser.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <regex>
#include <charconv>

namespace bha::parsers {

    namespace {

        constexpr std::string_view MSVC_TIME_PREFIX = "time(";
        constexpr std::string_view MSVC_C1XX = "c1xx.dll";
        constexpr std::string_view MSVC_C2 = "c2.dll";

        Duration parse_msvc_time(const std::string_view time_str) {
            double seconds = 0.0;
            auto trimmed = string_utils::trim(time_str);

            if (string_utils::ends_with(trimmed, "s")) {
                trimmed = trimmed.substr(0, trimmed.size() - 1);
            }

            std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), seconds);
            return std::chrono::duration_cast<Duration>(
                std::chrono::duration<double>(seconds)
            );
        }

        struct MSVCTimeLine {
            std::string target;
            Duration total_time = Duration::zero();
            Duration frontend_time = Duration::zero();
            Duration template_time = Duration::zero();
        };

        std::optional<MSVCTimeLine> parse_msvc_line(const std::string_view line) {
            const auto trimmed = string_utils::trim(line);

            if (!string_utils::starts_with(trimmed, MSVC_TIME_PREFIX)) {
                return std::nullopt;
            }

            const auto close_paren = trimmed.find(')');
            if (close_paren == std::string_view::npos) {
                return std::nullopt;
            }

            MSVCTimeLine result;
            result.target = std::string(trimmed.substr(5, close_paren - 5));

            const auto equals_pos = trimmed.find('=', close_paren);
            if (equals_pos == std::string_view::npos) {
                return std::nullopt;
            }

            const auto time_start = equals_pos + 1;
            auto time_end = trimmed.find_first_of(" <", time_start);
            if (time_end == std::string_view::npos) {
                time_end = trimmed.size();
            }

            result.total_time = parse_msvc_time(trimmed.substr(time_start, time_end - time_start));

            if (const auto detail_start = trimmed.find('<'); detail_start != std::string_view::npos) {
                if (const auto detail_end = trimmed.find('>'); detail_end != std::string_view::npos) {
                    const auto details = trimmed.substr(detail_start + 1, detail_end - detail_start - 1);

                    const std::regex time_regex(R"((\d+\.?\d*)\s*s\s*\(([^)]+)\))");
                    const std::string details_str(details);
                    std::smatch match;

                    auto it = details_str.cbegin();
                    while (std::regex_search(it, details_str.cend(), match, time_regex)) {
                        double val = 0.0;
                        std::string num = match[1].str();
                        std::from_chars(num.data(), num.data() + num.size(), val);
                        const auto dur = std::chrono::duration_cast<Duration>(
                            std::chrono::duration<double>(val));

                        std::string label = match[2].str();
                        auto lower_label = string_utils::to_lower(label);

                        if (string_utils::contains(lower_label, "frontend")) {
                            result.frontend_time = dur;
                        }
                        else if (string_utils::contains(lower_label, "template")) {
                            result.template_time = dur;
                        }

                        it = match.suffix().first;
                    }
                }
            }

            return result;
        }

    }  // namespace

    bool MSVCTraceParser::can_parse(const fs::path& path) const {
        if (const auto ext = path.extension().string(); ext != ".txt" && ext != ".log" && ext != ".btlog") {
            return false;
        }

        auto result = file_utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool MSVCTraceParser::can_parse_content(const std::string_view content) const {
        return string_utils::contains(content, MSVC_TIME_PREFIX) &&
               (string_utils::contains(content, MSVC_C1XX) ||
                string_utils::contains(content, MSVC_C2));
    }

    Result<CompilationUnit, Error> MSVCTraceParser::parse_file(
        const fs::path& path
    ) const {
        auto content_result = file_utils::read_file(path);
        if (content_result.is_err()) {
            return Result<CompilationUnit, Error>::failure(content_result.error());
        }

        return parse_content(content_result.value(), path);
    }

    Result<CompilationUnit, Error> MSVCTraceParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        if (!can_parse_content(content)) {
            return Result<CompilationUnit, Error>::failure(
                Error::parse_error("Not a valid MSVC timing output")
            );
        }

        CompilationUnit unit;
        unit.source_file = source_hint;
        unit.metrics.path = source_hint;

        for (const auto lines = string_utils::split(content, '\n'); const auto& line : lines) {
            const auto timing = parse_msvc_line(line);
            if (!timing) continue;

            auto lower_target = string_utils::to_lower(timing->target);

            if (string_utils::contains(lower_target, "c1xx")) {
                unit.metrics.frontend_time = timing->total_time;
                unit.metrics.breakdown.template_instantiation = timing->template_time;

                auto other_frontend = timing->total_time - timing->template_time;
                unit.metrics.breakdown.parsing = other_frontend / 2;
                unit.metrics.breakdown.semantic_analysis = other_frontend / 2;
            }
            else if (string_utils::contains(lower_target, "c2")) {
                unit.metrics.backend_time = timing->total_time;
                unit.metrics.breakdown.code_generation = timing->total_time / 2;
                unit.metrics.breakdown.optimization = timing->total_time / 2;
            }
            else if (string_utils::ends_with(lower_target, ".cpp") ||
                     string_utils::ends_with(lower_target, ".cxx") ||
                     string_utils::ends_with(lower_target, ".cc")) {
                unit.source_file = timing->target;
                unit.metrics.path = timing->target;
                unit.metrics.total_time = timing->total_time;
            }
        }

        if (unit.metrics.total_time == Duration::zero()) {
            unit.metrics.total_time = unit.metrics.frontend_time + unit.metrics.backend_time;
        }

        return Result<CompilationUnit, Error>::success(std::move(unit));
    }

    void register_msvc_parser() {
        ParserRegistry::instance().register_parser(
            std::make_unique<MSVCTraceParser>()
        );
    }

}  // namespace bha::parsers