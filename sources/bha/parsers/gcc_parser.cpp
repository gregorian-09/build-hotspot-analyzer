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
#include "bha/parsers/memory_parser.hpp"
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
            const auto trimmed = utils::trim(line);

            if (!utils::starts_with(trimmed, GCC_PHASE_PREFIX) &&
                !utils::contains(trimmed, ":")) {
                return std::nullopt;
            }

            const auto colon_pos = trimmed.find(':');
            if (colon_pos == std::string_view::npos) {
                return std::nullopt;
            }

            TimingLine result;
            result.phase_name = std::string(utils::trim(trimmed.substr(0, colon_pos)));

            const auto times_part = trimmed.substr(colon_pos + 1);

            const std::regex time_regex(R"((\d+\.\d+)\s*\([^)]*\))");
            const std::string times_str(times_part);
            std::smatch match;
            std::vector<double> times;

            auto it = times_str.cbegin();
            while (std::regex_search(it, times_str.cend(), match, time_regex)) {
                double val = 0.0;
                const std::string num = match[1].str();
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
            if (name == "phase parsing" || name == "phase late parsing cleanups") {
                breakdown.parsing += timing.wall_time;
            }
            else if (name == "phase lang. deferred") {
                // GCC reports this as one aggregate phase; it is not a
                // per-template or semantic-only measurement.
                breakdown.unclassified += timing.wall_time;
            }
            else if (name == "phase opt and generate") {
                // This phase combines optimization and code generation.
                breakdown.unclassified += timing.wall_time;
            }
            else if (name == "phase last asm") {
                breakdown.unclassified += timing.wall_time;
            }
            else if (name == "phase stream in" || name == "phase stream out") {
                // LTO stream phases are not normalized optimization timings.
                breakdown.unclassified += timing.wall_time;
            }
            else if (name == "phase finalize") {
                breakdown.unclassified += timing.wall_time;
            }
            else {
                // Preserve unrecognized GCC timing variables without
                // guessing a normalized compiler phase.
                breakdown.unclassified += timing.wall_time;
            }
        }
    }  // namespace

    bool GCCTraceParser::can_parse(const fs::path& path) const {
        if (const auto ext = path.extension().string(); ext != ".txt" && ext != ".log" && ext != ".report") {
            return false;
        }

        auto result = utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool GCCTraceParser::can_parse_content(std::string_view content) const {
        return utils::contains(content, GCC_TIME_HEADER) &&
               utils::contains(content, "usr") &&
               utils::contains(content, "sys") &&
               utils::contains(content, "wall");
    }

    Result<CompilationUnit, Error> GCCTraceParser::parse_file(
        const fs::path& path
    ) const {
        auto content_result = utils::read_file(path);
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

        const auto lines = utils::split(content, '\n');
        Duration total_wall = Duration::zero();
        Duration frontend_time = Duration::zero();
        Duration backend_time = Duration::zero();
        bool saw_template_phase = false;

        for (const auto& line : lines) {
            if (auto timing = parse_timing_line(line)) {
                total_wall += timing->wall_time;
                saw_template_phase = saw_template_phase || timing->phase_name == "phase lang. deferred";
                map_phase_to_breakdown(*timing, unit.metrics.breakdown);

                if (timing->phase_name == "phase parsing" ||
                    timing->phase_name == "phase lang. deferred" ||
                    timing->phase_name == "phase late parsing cleanups") {
                    frontend_time += timing->wall_time;
                }
                else if (timing->phase_name == "phase opt and generate" ||
                         timing->phase_name == "phase last asm" ||
                         timing->phase_name == "phase stream in" ||
                         timing->phase_name == "phase stream out") {
                    backend_time += timing->wall_time;
                }
            }
        }

        unit.metrics.total_time = total_wall;
        unit.metrics.frontend_time = frontend_time;
        unit.metrics.backend_time = backend_time;

        if (saw_template_phase || unit.metrics.breakdown.template_instantiation != Duration::zero()) {
            unit.template_evidence = TemplateEvidence::AggregateTiming;
        }

        return Result<CompilationUnit, Error>::success(std::move(unit));
    }

    void register_gcc_parser() {
        ParserRegistry::instance().register_parser(
            std::make_unique<GCCTraceParser>()
        );
    }

}  // namespace bha::parsers
