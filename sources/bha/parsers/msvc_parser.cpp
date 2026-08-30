//
// Created by gregorian-rayne on 12/28/25.
//

/**
 * MSVC Build Time Parser (/Bt+ flag)
 *
 * ACTUAL OUTPUT FORMAT:
 *   time(C:\\path\\to\\source.cpp)=X.XXXs
 *   time(C:\\path\\to\\c1xx.dll)=X.XXXs < timestamp1 - timestamp2 > BB [source.cpp]
 *   time(C:\\path\\to\\c2.dll)=X.XXXs < timestamp3 - timestamp4 > BB [source.cpp]
 *
 * WHERE:
 *   c1xx.dll = Frontend compiler (parsing, semantic analysis, templates)
 *   c2.dll   = Backend compiler (optimization, code generation)
 *   Timestamps in angle brackets are compilation start/end times (not durations)
 *   The /Bt+ flag shows time spent in the front end and back end of the MSVC compiler. :contentReference[oaicite:0]{index=0}
 *
 * REFERENCES:
 *   - http://coding-scars.com/investigating-cpp-compile-times-3/ :contentReference[oaicite:1]{index=1}
 *   - https://aras-p.info/blog/2019/01/21/Another-cool-MSVC-flag-d1reportTime/ (discusses related MSVC timing flags) :contentReference[oaicite:2]{index=2}
 */

#include "bha/parsers/msvc_parser.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/numeric_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <cmath>

namespace bha::parsers {

    namespace {

        constexpr std::string_view MSVC_TIME_PREFIX = "time(";
        constexpr std::string_view MSVC_C1XX = "c1xx.dll";
        constexpr std::string_view MSVC_C2 = "c2.dll";

        std::optional<Duration> parse_msvc_time(const std::string_view time_str) {
            auto trimmed = utils::trim(time_str);

            if (!utils::ends_with(trimmed, "s")) {
                return std::nullopt;
            }
            trimmed = utils::trim(trimmed.substr(0, trimmed.size() - 1));
            if (trimmed.empty()) {
                return std::nullopt;
            }

            const auto seconds = utils::parse_double(trimmed);
            if (!seconds.has_value() || *seconds < 0.0) {
                return std::nullopt;
            }
            return utils::checked_duration_cast<Duration>(
                std::chrono::duration<double>(*seconds)
            );
        }

        struct MSVCTimeLine {
            std::string target;
            Duration total_time = Duration::zero();
        };

        struct MSVCTimeLineParse {
            bool is_timing_line = false;
            bool valid = true;
            MSVCTimeLine timing;
        };

        MSVCTimeLineParse parse_msvc_line(const std::string_view line) {
            const auto trimmed = utils::trim(line);

            if (!utils::starts_with(trimmed, MSVC_TIME_PREFIX)) {
                return {};
            }

            const auto close_paren = trimmed.find(')');
            if (close_paren == std::string_view::npos) {
                return {true, false, {}};
            }

            MSVCTimeLine result;
            result.target = std::string(utils::trim(trimmed.substr(5, close_paren - 5)));
            if (result.target.empty()) {
                return {true, false, {}};
            }

            const auto equals_pos = trimmed.find('=', close_paren);
            if (equals_pos == std::string_view::npos ||
                !utils::trim(trimmed.substr(close_paren + 1, equals_pos - close_paren - 1)).empty()) {
                return {true, false, {}};
            }

            const auto time_start = equals_pos + 1;
            auto time_end = trimmed.find_first_of(" <", time_start);
            if (time_end == std::string_view::npos) {
                time_end = trimmed.size();
            }

            const auto duration = parse_msvc_time(trimmed.substr(time_start, time_end - time_start));
            if (!duration.has_value()) {
                return {true, false, {}};
            }
            result.total_time = *duration;

            return {true, true, std::move(result)};
        }

        std::string_view target_basename(const std::string_view target) {
            const auto separator = target.find_last_of("/\\");
            return separator == std::string_view::npos
                ? target
                : target.substr(separator + 1);
        }

        bool is_msvc_component(const std::string_view target, const std::string_view component) {
            return utils::to_lower(target_basename(target)) == component;
        }

        bool is_source_target(const std::string_view target) {
            const auto lower = utils::to_lower(target_basename(target));
            return utils::ends_with(lower, ".cpp") ||
                   utils::ends_with(lower, ".cxx") ||
                   utils::ends_with(lower, ".cc") ||
                   utils::ends_with(lower, ".c");
        }

    }  // namespace

    bool MSVCTraceParser::can_parse(const fs::path& path) const {
        if (const auto ext = path.extension().string(); ext != ".txt" && ext != ".log" && ext != ".btlog") {
            return false;
        }

        auto result = utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool MSVCTraceParser::can_parse_content(const std::string_view content) const {
        bool has_compiler_timing = false;
        for (const auto& line : utils::split(content, '\n')) {
            const auto parsed = parse_msvc_line(line);
            if (!parsed.is_timing_line) {
                continue;
            }
            if (!parsed.valid) {
                return false;
            }
            has_compiler_timing = has_compiler_timing ||
                is_msvc_component(parsed.timing.target, MSVC_C1XX) ||
                is_msvc_component(parsed.timing.target, MSVC_C2);
        }
        return has_compiler_timing;
    }

    Result<CompilationUnit, Error> MSVCTraceParser::parse_file(
        const fs::path& path
    ) const {
        auto content_result = utils::read_file(path);
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

        for (const auto lines = utils::split(content, '\n'); const auto& line : lines) {
            const auto parsed = parse_msvc_line(line);
            if (!parsed.is_timing_line) {
                continue;
            }
            if (!parsed.valid) {
                return Result<CompilationUnit, Error>::failure(
                    Error::parse_error("Malformed MSVC timing row")
                );
            }
            const auto& timing = parsed.timing;
            const auto add_duration = [&](Duration& total) {
                const auto sum = utils::checked_add_duration(total, timing.total_time);
                if (!sum.has_value()) {
                    return false;
                }
                total = *sum;
                return true;
            };

            if (is_msvc_component(timing.target, MSVC_C1XX)) {
                if (!add_duration(unit.metrics.frontend_time) ||
                    !add_duration(unit.metrics.breakdown.unclassified)) {
                    return Result<CompilationUnit, Error>::failure(
                        Error::parse_error(
                            "MSVC frontend timing exceeded the supported aggregate duration range",
                            source_hint.string()
                        )
                    );
                }
            }
            else if (is_msvc_component(timing.target, MSVC_C2)) {
                if (!add_duration(unit.metrics.backend_time) ||
                    !add_duration(unit.metrics.breakdown.unclassified)) {
                    return Result<CompilationUnit, Error>::failure(
                        Error::parse_error(
                            "MSVC backend timing exceeded the supported aggregate duration range",
                            source_hint.string()
                        )
                    );
                }
            }
            else if (is_source_target(timing.target)) {
                unit.source_file = timing.target;
                unit.metrics.path = timing.target;
                unit.metrics.total_time = timing.total_time;
            }
        }

        if (unit.metrics.frontend_time == Duration::zero() &&
            unit.metrics.backend_time == Duration::zero()) {
            return Result<CompilationUnit, Error>::failure(
                Error::parse_error("MSVC timing output contains no compiler component rows")
            );
        }

        const auto add_phase_capability = [&unit](
            const std::string_view phase,
            const bool observed,
            const std::string_view limitation = {}
        ) {
            MetricCapability capability;
            capability.metric = "compiler.phase." + std::string(phase);
            capability.provenance.evidence = observed
                ? EvidenceKind::Observed
                : EvidenceKind::Unavailable;
            capability.provenance.producer = "msvc";
            capability.provenance.capture_mode = "/Bt+";
            capability.provenance.scope = "translation-unit";
            capability.provenance.timing_domain = TimingDomain::WallClock;
            capability.provenance.timing_aggregation = TimingAggregation::Inclusive;
            capability.provenance.limitation = std::string(limitation);
            unit.metric_capabilities.push_back(std::move(capability));
        };
        add_phase_capability(
            "preprocessing",
            false,
            "MSVC /Bt+ reports frontend/backend totals, not a standalone preprocessing phase"
        );
        add_phase_capability(
            "parsing",
            false,
            "MSVC /Bt+ reports frontend/backend totals, not a standalone parsing phase"
        );
        add_phase_capability(
            "semantic_analysis",
            false,
            "MSVC /Bt+ reports frontend/backend totals, not a standalone semantic-analysis phase"
        );
        add_phase_capability(
            "template_instantiation",
            false,
            "MSVC /Bt+ reports frontend/backend totals, not a standalone template phase"
        );
        add_phase_capability(
            "code_generation",
            false,
            "MSVC /Bt+ reports frontend/backend totals, not a standalone code-generation phase"
        );
        add_phase_capability(
            "optimization",
            false,
            "MSVC /Bt+ reports frontend/backend totals, not a standalone optimization phase"
        );
        add_phase_capability(
            "unclassified",
            true,
            "MSVC frontend/backend totals are retained as an aggregate, not assigned to a finer phase"
        );

        return Result<CompilationUnit, Error>::success(std::move(unit));
    }

    void register_msvc_parser() {
        ParserRegistry::instance().register_parser(
            std::make_unique<MSVCTraceParser>()
        );
    }

}  // namespace bha::parsers
