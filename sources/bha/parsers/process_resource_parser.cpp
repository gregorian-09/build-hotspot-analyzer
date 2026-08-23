// Created by gregorian-rayne on 8/23/26.

#include "bha/parsers/process_resource_parser.hpp"

#include "bha/utils/file_utils.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace bha::parsers {
    namespace {

        Result<std::vector<std::string>, Error> parse_csv_record(
            const std::string_view line,
            const fs::path& source_hint
        ) {
            std::vector<std::string> fields;
            std::string field;
            bool in_quotes = false;
            bool closed_quote = false;

            for (std::size_t i = 0; i < line.size(); ++i) {
                const char character = line[i];
                if (in_quotes) {
                    if (character != '"') {
                        field.push_back(character);
                        continue;
                    }
                    if (i + 1 < line.size() && line[i + 1] == '"') {
                        field.push_back('"');
                        ++i;
                    } else {
                        in_quotes = false;
                        closed_quote = true;
                    }
                    continue;
                }

                if (character == '"') {
                    if (!field.empty() || closed_quote) {
                        return Result<std::vector<std::string>, Error>::failure(
                            Error::parse_error("Invalid quoted process resource CSV field", source_hint.string())
                        );
                    }
                    in_quotes = true;
                } else if (character == ',') {
                    fields.push_back(std::move(field));
                    field.clear();
                    closed_quote = false;
                } else if (closed_quote) {
                    return Result<std::vector<std::string>, Error>::failure(
                        Error::parse_error("Unexpected content after quoted process resource CSV field", source_hint.string())
                    );
                } else {
                    field.push_back(character);
                }
            }

            if (in_quotes) {
                return Result<std::vector<std::string>, Error>::failure(
                    Error::parse_error("Unterminated process resource CSV field", source_hint.string())
                );
            }
            fields.push_back(std::move(field));
            return Result<std::vector<std::string>, Error>::success(std::move(fields));
        }

        Result<std::uint64_t, Error> parse_unsigned(
            const std::string_view value,
            const char* field_name,
            const fs::path& source_hint
        ) {
            if (value.empty()) {
                return Result<std::uint64_t, Error>::failure(
                    Error::parse_error(std::string("Empty process resource field: ") + field_name, source_hint.string())
                );
            }

            std::uint64_t parsed = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} || end != value.data() + value.size()) {
                return Result<std::uint64_t, Error>::failure(
                    Error::parse_error(std::string("Invalid process resource field: ") + field_name, source_hint.string())
                );
            }
            return Result<std::uint64_t, Error>::success(parsed);
        }

        Result<Duration, Error> parse_microseconds(
            const std::string_view value,
            const char* field_name,
            const fs::path& source_hint
        ) {
            const auto parsed = parse_unsigned(value, field_name, source_hint);
            if (parsed.is_err()) {
                return Result<Duration, Error>::failure(parsed.error());
            }

            constexpr auto max_microseconds =
                static_cast<std::uint64_t>(std::numeric_limits<Duration::rep>::max()) / 1000U;
            if (parsed.value() > max_microseconds) {
                return Result<Duration, Error>::failure(
                    Error::parse_error(std::string("Process resource time is too large: ") + field_name, source_hint.string())
                );
            }
            return Result<Duration, Error>::success(
                std::chrono::duration_cast<Duration>(std::chrono::microseconds(parsed.value()))
            );
        }

        MetricCapability observed_capability() {
            MetricCapability capability;
            capability.metric = "process.resource_counters";
            capability.provenance.evidence = EvidenceKind::Observed;
            capability.provenance.producer = "clang";
            capability.provenance.capture_mode = "-fproc-stat-report=FILE";
            capability.provenance.scope = "build";
            capability.provenance.timing_domain = TimingDomain::WallClock;
            capability.provenance.timing_aggregation = TimingAggregation::Exclusive;
            capability.provenance.limitation =
                "Rows identify tool invocations and output paths; the producer does not provide source translation-unit ownership";
            return capability;
        }

    }  // namespace

    Result<ProcessResourceReport, Error> ProcessResourceParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        ProcessResourceReport result;
        std::size_t line_start = 0;
        std::size_t line_number = 0;

        while (line_start <= content.size()) {
            const auto line_end = content.find('\n', line_start);
            const auto line_length = line_end == std::string_view::npos
                ? content.size() - line_start
                : line_end - line_start;
            std::string_view line = content.substr(line_start, line_length);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            ++line_number;

            if (!line.empty()) {
                const auto fields = parse_csv_record(line, source_hint);
                if (fields.is_err()) {
                    return Result<ProcessResourceReport, Error>::failure(
                        Error::parse_error(
                            "Invalid process resource row at line " + std::to_string(line_number) +
                                ": " + fields.error().message(),
                            source_hint.string()
                        )
                    );
                }
                if (fields.value().size() != 5 || fields.value()[0].empty() || fields.value()[1].empty()) {
                    return Result<ProcessResourceReport, Error>::failure(
                        Error::parse_error(
                            "Process resource row must contain tool, output, and three numeric fields",
                            source_hint.string()
                        )
                    );
                }

                const auto total_time = parse_microseconds(fields.value()[2], "total-time-us", source_hint);
                const auto user_time = parse_microseconds(fields.value()[3], "user-time-us", source_hint);
                const auto peak_memory = parse_unsigned(fields.value()[4], "peak-memory-kib", source_hint);
                if (total_time.is_err()) {
                    return Result<ProcessResourceReport, Error>::failure(total_time.error());
                }
                if (user_time.is_err()) {
                    return Result<ProcessResourceReport, Error>::failure(user_time.error());
                }
                if (peak_memory.is_err()) {
                    return Result<ProcessResourceReport, Error>::failure(peak_memory.error());
                }
                if (user_time.value() > total_time.value()) {
                    return Result<ProcessResourceReport, Error>::failure(
                        Error::parse_error("Process resource user time exceeds total time", source_hint.string())
                    );
                }

                result.observations.push_back({
                    fs::path(fields.value()[0]),
                    fs::path(fields.value()[1]),
                    total_time.value(),
                    user_time.value(),
                    peak_memory.value()
                });
            }

            if (line_end == std::string_view::npos) {
                break;
            }
            line_start = line_end + 1;
        }

        if (result.observations.empty()) {
            return Result<ProcessResourceReport, Error>::failure(
                Error::parse_error("Process resource report contains no observations", source_hint.string())
            );
        }
        result.metric_capabilities.push_back(observed_capability());
        return Result<ProcessResourceReport, Error>::success(std::move(result));
    }

    Result<ProcessResourceReport, Error> ProcessResourceParser::parse_file(const fs::path& path) const {
        const auto content = utils::read_file(path);
        if (content.is_err()) {
            return Result<ProcessResourceReport, Error>::failure(content.error());
        }
        return parse_content(content.value(), path);
    }

    Result<void, Error> ProcessResourceParser::attach_to_trace(
        BuildTrace& trace,
        const fs::path& path
    ) const {
        const auto parsed = parse_file(path);
        if (parsed.is_err()) {
            return Result<void, Error>::failure(parsed.error());
        }
        trace.process_resource_report = parsed.value();
        for (const auto& capability : parsed.value().metric_capabilities) {
            const auto existing = std::ranges::find(
                trace.metric_capabilities,
                capability.metric,
                &MetricCapability::metric
            );
            if (existing == trace.metric_capabilities.end()) {
                trace.metric_capabilities.push_back(capability);
            }
        }
        return Result<void, Error>::success();
    }

}  // namespace bha::parsers
