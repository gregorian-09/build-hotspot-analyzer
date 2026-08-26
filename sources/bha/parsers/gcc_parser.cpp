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
#include "bha/utils/numeric_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <array>
#include <cmath>
#include <cctype>

namespace bha::parsers {

    namespace
    {
        constexpr std::string_view GCC_PHASE_PREFIX = "phase ";
        constexpr std::string_view GCC_TOTAL_ROW = "TOTAL";

        struct ReportSchema {
            std::size_t timing_columns = 0;
        };

        struct TimingLine {
            std::string phase_name;
            Duration user_time = Duration::zero();
            Duration sys_time = Duration::zero();
            Duration wall_time = Duration::zero();
        };

        struct TimingLineParse {
            bool is_report_row = false;
            bool valid = true;
            TimingLine timing;
        };

        std::vector<std::string_view> split_whitespace(const std::string_view text) {
            std::vector<std::string_view> tokens;
            std::size_t position = 0;
            while (position < text.size()) {
                while (position < text.size() &&
                       std::isspace(static_cast<unsigned char>(text[position])) != 0) {
                    ++position;
                }
                if (position == text.size()) {
                    break;
                }
                const auto start = position;
                while (position < text.size() &&
                       std::isspace(static_cast<unsigned char>(text[position])) == 0) {
                    ++position;
                }
                tokens.push_back(text.substr(start, position - start));
            }
            return tokens;
        }

        std::optional<ReportSchema> parse_report_header(const std::string_view line) {
            const auto tokens = split_whitespace(utils::trim(line));
            if (tokens.size() == 5 &&
                tokens[0] == "Time" && tokens[1] == "variable" &&
                tokens[2] == "usr" && tokens[3] == "sys" && tokens[4] == "wall") {
                return ReportSchema{3};
            }
            if (tokens.size() == 6 &&
                tokens[0] == "Time" && tokens[1] == "variable" &&
                tokens[2] == "usr" && tokens[3] == "sys" && tokens[4] == "wall" &&
                tokens[5] == "GGC") {
                return ReportSchema{3};
            }
            if (tokens.size() == 4 &&
                tokens[0] == "Time" && tokens[1] == "variable" &&
                tokens[2] == "wall" && tokens[3] == "GGC") {
                return ReportSchema{1};
            }
            if (tokens.size() == 3 &&
                tokens[0] == "Time" && tokens[1] == "variable" && tokens[2] == "wall") {
                return ReportSchema{1};
            }
            return std::nullopt;
        }

        std::optional<ReportSchema> find_report_schema(const std::string_view content) {
            for (const auto& line : utils::split(content, '\n')) {
                if (const auto schema = parse_report_header(line); schema.has_value()) {
                    return schema;
                }
            }
            return std::nullopt;
        }

        struct ParsedTimeField {
            Duration duration = Duration::zero();
            std::size_t next_position = 0;
        };

        std::optional<ParsedTimeField> parse_time_field(
            const std::string_view text,
            std::size_t position
        ) {
            while (position < text.size() &&
                   std::isspace(static_cast<unsigned char>(text[position])) != 0) {
                ++position;
            }
            const auto number_start = position;
            while (position < text.size() &&
                   std::isspace(static_cast<unsigned char>(text[position])) == 0 &&
                   text[position] != '(') {
                ++position;
            }
            if (number_start == position) {
                return std::nullopt;
            }

            const auto number = text.substr(number_start, position - number_start);
            const auto seconds = utils::parse_double(number);
            if (!seconds.has_value() || *seconds < 0.0) {
                return std::nullopt;
            }
            const auto duration = utils::checked_duration_cast<Duration>(
                std::chrono::duration<double>(*seconds)
            );
            if (!duration.has_value()) {
                return std::nullopt;
            }

            while (position < text.size() &&
                   std::isspace(static_cast<unsigned char>(text[position])) != 0) {
                ++position;
            }
            if (position >= text.size() || text[position] != '(') {
                return std::nullopt;
            }
            const auto close = text.find(')', position + 1);
            if (close == std::string_view::npos) {
                return std::nullopt;
            }
            const auto percentage = utils::trim(text.substr(position + 1, close - position - 1));
            if (percentage.empty() || percentage.back() != '%') {
                return std::nullopt;
            }
            const auto percentage_number = percentage.substr(0, percentage.size() - 1);
            const auto parsed_percentage = utils::parse_double(percentage_number);
            if (!parsed_percentage.has_value() || *parsed_percentage < 0.0) {
                return std::nullopt;
            }

            return ParsedTimeField{*duration, close + 1};
        }

        std::optional<ParsedTimeField> parse_plain_time_field(
            const std::string_view text,
            std::size_t position
        ) {
            while (position < text.size() &&
                   std::isspace(static_cast<unsigned char>(text[position])) != 0) {
                ++position;
            }
            const auto number_start = position;
            while (position < text.size() &&
                   std::isspace(static_cast<unsigned char>(text[position])) == 0) {
                ++position;
            }
            if (number_start == position) {
                return std::nullopt;
            }

            const auto number = text.substr(number_start, position - number_start);
            const auto seconds = utils::parse_double(number);
            if (!seconds.has_value() || *seconds < 0.0) {
                return std::nullopt;
            }
            const auto duration = utils::checked_duration_cast<Duration>(
                std::chrono::duration<double>(*seconds)
            );
            if (!duration.has_value()) {
                return std::nullopt;
            }
            return ParsedTimeField{*duration, position};
        }

        TimingLineParse parse_timing_line(
            const std::string_view line,
            const ReportSchema schema
        ) {
            const auto trimmed = utils::trim(line);
            const auto colon_pos = trimmed.find(':');
            if (colon_pos == std::string_view::npos) {
                return {};
            }

            const auto phase_name = utils::trim(trimmed.substr(0, colon_pos));
            const bool is_phase = utils::starts_with(phase_name, GCC_PHASE_PREFIX);
            const bool is_total = phase_name == GCC_TOTAL_ROW;
            if (!is_phase && !is_total) {
                return {};
            }

            TimingLineParse result;
            result.is_report_row = true;
            result.timing.phase_name = std::string(phase_name);
            std::array<Duration, 3> durations{};
            auto position = colon_pos + 1;
            for (std::size_t index = 0; index < schema.timing_columns; ++index) {
                const auto field = is_total
                    ? parse_plain_time_field(trimmed, position)
                    : parse_time_field(trimmed, position);
                if (!field.has_value()) {
                    result.valid = false;
                    return result;
                }
                durations[index] = field->duration;
                position = field->next_position;
            }

            if (schema.timing_columns == 3) {
                result.timing.user_time = durations[0];
                result.timing.sys_time = durations[1];
                result.timing.wall_time = durations[2];
            } else {
                result.timing.wall_time = durations[0];
            }
            return result;
        }

        bool map_phase_to_breakdown(const TimingLine& timing, TimeBreakdown& breakdown) {
            const auto& name = timing.phase_name;
            const auto add_unclassified = [&]() {
                return utils::checked_add_duration(
                    breakdown.unclassified,
                    timing.wall_time
                );
            };
            const auto add_parsing = [&]() {
                return utils::checked_add_duration(
                    breakdown.parsing,
                    timing.wall_time
                );
            };

            // Official GCC phase names (must match exactly from timevar.def)
            if (name == "phase parsing" || name == "phase late parsing cleanups") {
                const auto sum = add_parsing();
                if (!sum.has_value()) {
                    return false;
                }
                breakdown.parsing = *sum;
            }
            else if (name == "phase lang. deferred") {
                // GCC reports this as one aggregate phase; it is not a
                // per-template or semantic-only measurement.
                const auto sum = add_unclassified();
                if (!sum.has_value()) {
                    return false;
                }
                breakdown.unclassified = *sum;
            }
            else if (name == "phase opt and generate") {
                // This phase combines optimization and code generation.
                const auto sum = add_unclassified();
                if (!sum.has_value()) {
                    return false;
                }
                breakdown.unclassified = *sum;
            }
            else if (name == "phase last asm") {
                const auto sum = add_unclassified();
                if (!sum.has_value()) {
                    return false;
                }
                breakdown.unclassified = *sum;
            }
            else if (name == "phase stream in" || name == "phase stream out") {
                // LTO stream phases are not normalized optimization timings.
                const auto sum = add_unclassified();
                if (!sum.has_value()) {
                    return false;
                }
                breakdown.unclassified = *sum;
            }
            else if (name == "phase finalize") {
                const auto sum = add_unclassified();
                if (!sum.has_value()) {
                    return false;
                }
                breakdown.unclassified = *sum;
            }
            else {
                // Preserve unrecognized GCC timing variables without
                // guessing a normalized compiler phase.
                const auto sum = add_unclassified();
                if (!sum.has_value()) {
                    return false;
                }
                breakdown.unclassified = *sum;
            }
            return true;
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
        return find_report_schema(content).has_value();
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
        const auto schema = find_report_schema(content);
        if (!schema.has_value()) {
            return Result<CompilationUnit, Error>::failure(
                Error::parse_error("Not a valid GCC time report")
            );
        }

        CompilationUnit unit;
        unit.source_file = source_hint;
        unit.metrics.path = source_hint;

        const auto lines = utils::split(content, '\n');
        Duration frontend_time = Duration::zero();
        Duration backend_time = Duration::zero();
        bool saw_template_phase = false;
        bool saw_timing_row = false;
        std::optional<Duration> reported_total;

        for (const auto& line : lines) {
            const auto parsed = parse_timing_line(line, *schema);
            if (!parsed.is_report_row) {
                continue;
            }
            if (!parsed.valid) {
                return Result<CompilationUnit, Error>::failure(
                    Error::parse_error("Malformed GCC timing row")
                );
            }

            saw_timing_row = true;
            const auto& timing = parsed.timing;
            if (timing.phase_name == GCC_TOTAL_ROW) {
                reported_total = timing.wall_time;
                continue;
            }

            saw_template_phase = saw_template_phase || timing.phase_name == "phase lang. deferred";
            if (!map_phase_to_breakdown(timing, unit.metrics.breakdown)) {
                return Result<CompilationUnit, Error>::failure(
                    Error::parse_error(
                        "GCC phase timing exceeded the supported aggregate duration range",
                        source_hint.string()
                    )
                );
            }

            if (timing.phase_name == "phase parsing" ||
                timing.phase_name == "phase lang. deferred" ||
                timing.phase_name == "phase late parsing cleanups") {
                const auto sum = utils::checked_add_duration(frontend_time, timing.wall_time);
                if (!sum.has_value()) {
                    return Result<CompilationUnit, Error>::failure(
                        Error::parse_error(
                            "GCC frontend timing exceeded the supported aggregate duration range",
                            source_hint.string()
                        )
                    );
                }
                frontend_time = *sum;
            }
            else if (timing.phase_name == "phase opt and generate" ||
                     timing.phase_name == "phase last asm" ||
                     timing.phase_name == "phase stream in" ||
                     timing.phase_name == "phase stream out") {
                const auto sum = utils::checked_add_duration(backend_time, timing.wall_time);
                if (!sum.has_value()) {
                    return Result<CompilationUnit, Error>::failure(
                        Error::parse_error(
                            "GCC backend timing exceeded the supported aggregate duration range",
                            source_hint.string()
                        )
                    );
                }
                backend_time = *sum;
            }
        }

        if (!saw_timing_row) {
            return Result<CompilationUnit, Error>::failure(
                Error::parse_error("GCC time report contains no timing rows")
            );
        }

        unit.metrics.total_time = reported_total.value_or(Duration::zero());
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
