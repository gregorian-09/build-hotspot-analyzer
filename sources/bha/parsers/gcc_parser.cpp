//
// Created by gregorian-rayne on 12/28/25.
//

/**
 * GCC Time Report Parser (-ftime-report flag)
 *
 * OFFICIAL GCC PHASES (from gcc/timevar.def):
 *   - "phase setup"                  - Compiler initialization
 *   - "phase parsing"                - Source code parsing
 *   - "phase lang. deferred"         - Language-specific deferred work (includes template instantiation)
 *   - "phase late parsing cleanups"  - Cleanup after parsing
 *   - "phase opt and generate"       - Optimization and code generation
 *   - "phase last asm"               - Final assembly output
 *   - "phase stream in"              - Link-Time Optimization (LTO) input
 *   - "phase stream out"             - Link-Time Optimization (LTO) output
 *   - "phase finalize"               - Final cleanup and output
 *
 * REFERENCES:
 *   - https://github.com/gcc-mirror/gcc/blob/master/gcc/timevar.def
 *   - https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html
 */

#include "bha/parsers/gcc_parser.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <regex>
#include <charconv>

namespace bha::parsers {

    namespace
    {
        constexpr std::string_view GCC_TIME_HEADER = "Time variable";
        constexpr std::string_view GCC_PHASE_PREFIX = "phase ";

        struct TimingLine {
            std::string phase_name;
            Duration user_time = Duration::zero();
            Duration sys_time = Duration::zero();
            Duration wall_time = Duration::zero();
        };

        std::optional<TimingLine> parse_timing_line(std::string_view line) {
            auto trimmed = string_utils::trim(line);

            if (!string_utils::starts_with(trimmed, GCC_PHASE_PREFIX) &&
                !string_utils::contains(trimmed, ":")) {
                return std::nullopt;
                }

            const auto colon_pos = trimmed.find(':');
            if (colon_pos == std::string_view::npos) {
                return std::nullopt;
            }

            TimingLine result;
            result.phase_name = std::string(string_utils::trim(trimmed.substr(0, colon_pos)));

            const auto times_part = trimmed.substr(colon_pos + 1);

            const std::regex time_regex(R"((\d+\.\d+)\s*\([^)]*\))");
            const std::string times_str(times_part);
            std::smatch match;
            std::vector<double> times;

            auto it = times_str.cbegin();
            while (std::regex_search(it, times_str.cend(), match, time_regex)) {
                double val = 0.0;
                std::string num = match[1].str();
                std::from_chars(num.data(), num.data() + num.size(), val);
                times.push_back(val);
                it = match.suffix().first;
            }

            if (!times.empty()) {
                result.user_time = std::chrono::duration_cast<Duration>(
                    std::chrono::duration<double>(times[0]));
            }
            if (times.size() >= 2) {
                result.sys_time = std::chrono::duration_cast<Duration>(
                    std::chrono::duration<double>(times[1]));
            }
            if (times.size() >= 3) {
                result.wall_time = std::chrono::duration_cast<Duration>(
                    std::chrono::duration<double>(times[2]));
            }

            return result;
        }

        void map_phase_to_breakdown(const TimingLine& timing, TimeBreakdown& breakdown) {
            const auto& name = timing.phase_name;

            // Official GCC phase names (must match exactly from timevar.def)
            if (name == "phase parsing") {
                breakdown.parsing += timing.wall_time;
            }
            else if (name == "phase lang. deferred") {
                // Language-specific deferred work includes template instantiation
                breakdown.semantic_analysis += timing.wall_time;
            }
            else if (name == "phase late parsing cleanups") {
                breakdown.parsing += timing.wall_time;
            }
            else if (name == "phase opt and generate") {
                // This combines optimization and code generation
                breakdown.optimization += timing.wall_time / 2;
                breakdown.code_generation += timing.wall_time / 2;
            }
            else if (name == "phase last asm") {
                breakdown.code_generation += timing.wall_time;
            }
            else if (name == "phase stream in" || name == "phase stream out") {
                // LTO phases are optimization-related
                breakdown.optimization += timing.wall_time;
            }
            else if (name == "phase finalize") {
                breakdown.code_generation += timing.wall_time;
            }
            else {
                // For non-phase timing variables, heuristic matching is employed
                const auto lower = string_utils::to_lower(name);

                if (string_utils::contains(lower, "preprocess")) {
                    breakdown.preprocessing += timing.wall_time;
                }
                else if (string_utils::contains(lower, "pars")) {
                    breakdown.parsing += timing.wall_time;
                }
                else if (string_utils::contains(lower, "template") ||
                         string_utils::contains(lower, "instantiat")) {
                    breakdown.template_instantiation += timing.wall_time;
                         }
                else if (string_utils::contains(lower, "semantic") ||
                         string_utils::contains(lower, "name lookup") ||
                         string_utils::contains(lower, "overload")) {
                    breakdown.semantic_analysis += timing.wall_time;
                         }
                else if (string_utils::contains(lower, "optim") ||
                         string_utils::contains(lower, "inline")) {
                    breakdown.optimization += timing.wall_time;
                         }
                else if (string_utils::contains(lower, "expand") ||
                         string_utils::contains(lower, "rtl") ||
                         string_utils::contains(lower, "codegen") ||
                         string_utils::contains(lower, "final") ||
                         string_utils::contains(lower, "assemb")) {
                    breakdown.code_generation += timing.wall_time;
                         }
            }
        }
    }  // namespace

    bool GCCTraceParser::can_parse(const fs::path& path) const {
        if (const auto ext = path.extension().string(); ext != ".txt" && ext != ".log" && ext != ".report") {
            return false;
        }

        auto result = file_utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool GCCTraceParser::can_parse_content(std::string_view content) const {
        return string_utils::contains(content, GCC_TIME_HEADER) &&
               string_utils::contains(content, "usr") &&
               string_utils::contains(content, "sys") &&
               string_utils::contains(content, "wall");
    }

    Result<CompilationUnit, Error> GCCTraceParser::parse_file(
        const fs::path& path
    ) const {
        auto content_result = file_utils::read_file(path);
        if (content_result.is_err()) {
            return Result<CompilationUnit, Error>::failure(content_result.error());
        }

        auto source_file = path;
        source_file.replace_extension(".cpp");

        return parse_content(content_result.value(), source_file);
    }

    Result<CompilationUnit, Error> GCCTraceParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        if (!can_parse_content(content)) {
            return Result<CompilationUnit, Error>::failure(
                Error::parse_error("Not a valid GCC time report")
            );
        }

        CompilationUnit unit;
        unit.source_file = source_hint;
        unit.metrics.path = source_hint;

        const auto lines = string_utils::split(content, '\n');
        Duration total_wall = Duration::zero();

        for (const auto& line : lines) {
            if (auto timing = parse_timing_line(line)) {
                total_wall += timing->wall_time;
                map_phase_to_breakdown(*timing, unit.metrics.breakdown);
            }
        }

        unit.metrics.total_time = total_wall;
        unit.metrics.frontend_time = unit.metrics.breakdown.preprocessing +
                                      unit.metrics.breakdown.parsing +
                                      unit.metrics.breakdown.semantic_analysis +
                                      unit.metrics.breakdown.template_instantiation;
        unit.metrics.backend_time = unit.metrics.breakdown.code_generation +
                                     unit.metrics.breakdown.optimization;

        return Result<CompilationUnit, Error>::success(std::move(unit));
    }

    void register_gcc_parser() {
        ParserRegistry::instance().register_parser(
            std::make_unique<GCCTraceParser>()
        );
    }

}  // namespace bha::parsers