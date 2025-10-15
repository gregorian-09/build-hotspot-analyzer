//
// Created by gregorian on 15/10/2025.
//

#include "bha/parsers/gcc_parser.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/path_utils.h"
#include "bha/utils/string_utils.h"
#include "bha/utils/hash_utils.h"

namespace bha::parsers {

    core::Result<std::vector<core::CompilationUnit>> GCCTimeReportParser::parse(
        const std::string_view file_path
    ) {
        const auto content = utils::read_file(file_path);
        if (!content) {
            return core::Result<std::vector<core::CompilationUnit>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Failed to read trace file: " + std::string(file_path)
            );
        }

        return parse_string(*content);
    }

    core::Result<std::vector<core::CompilationUnit>> GCCTimeReportParser::parse_string(
        std::string_view content
    ) {
        auto entries_result = parse_time_entries(content);
        if (!entries_result.is_success()) {
            return core::Result<std::vector<core::CompilationUnit>>::failure(
                entries_result.error()
            );
        }

        const auto& entries = entries_result.value();

        if (entries.empty()) {
            return core::Result<std::vector<core::CompilationUnit>>::failure(
                core::ErrorCode::PARSE_ERROR,
                "No time entries found in GCC time report"
            );
        }

        std::string file_path = extract_file_path_from_content(content);

        auto unit_result = build_compilation_unit(entries, file_path);
        if (!unit_result.is_success()) {
            return core::Result<std::vector<core::CompilationUnit>>::failure(
                unit_result.error()
            );
        }

        std::vector<core::CompilationUnit> units;
        units.push_back(std::move(unit_result.value()));

        return core::Result<std::vector<core::CompilationUnit>>::success(std::move(units));
    }

    std::string GCCTimeReportParser::get_format_name() const {
        return "gcc-time-report";
    }

    CompilerType GCCTimeReportParser::get_compiler_type() const {
        return CompilerType::GCC;
    }

    bool GCCTimeReportParser::can_parse(std::string_view file_path) const {
        const auto content = utils::read_file(file_path);
        if (!content) {
            return false;
        }

        return utils::contains(*content, "Time variable") &&
               utils::contains(*content, "TOTAL");
    }

    ParserCapabilities GCCTimeReportParser::get_capabilities() const {
        ParserCapabilities caps;
        caps.supports_timing = true;
        caps.supports_templates = false;
        caps.supports_preprocessing = true;
        caps.supports_optimization = true;
        caps.supports_dependencies = false;
        return caps;
    }

    std::vector<std::string> GCCTimeReportParser::get_supported_extensions() const {
        return {".txt", ".log"};
    }

    core::Result<std::vector<GCCTimeReportParser::TimeEntry>>
    GCCTimeReportParser::parse_time_entries(const std::string_view content) {
        std::vector<TimeEntry> entries;
        const auto lines = utils::split_lines(content);

        bool in_time_section = false;

        for (const auto& line : lines) {
            if (utils::contains(line, "Time variable")) {
                in_time_section = true;
                continue;
            }

            if (in_time_section) {
                if (line.empty() || utils::is_empty_or_whitespace(line)) {
                    continue;
                }

                if (utils::starts_with(utils::trim(line), "TOTAL")) {
                    break;
                }

                if (auto entry_opt = parse_time_entry_line(line)) {
                    entries.push_back(*entry_opt);
                }
            }
        }

        if (entries.empty()) {
            return core::Result<std::vector<TimeEntry>>::failure(
                core::ErrorCode::PARSE_ERROR,
                "Failed to parse any time entries from GCC time report"
            );
        }

        return core::Result<std::vector<TimeEntry>>::success(std::move(entries));
    }

    core::Result<core::CompilationUnit> GCCTimeReportParser::build_compilation_unit(
        const std::vector<TimeEntry>& entries,
        const std::string_view file_path
    ) {
        core::CompilationUnit unit;

        unit.file_path = file_path;
        unit.compiler_type = "gcc";
        unit.id = utils::compute_hash_hex(file_path);
        unit.build_timestamp = std::chrono::system_clock::now();

        extract_timing_from_entries(entries, unit);

        return core::Result<core::CompilationUnit>::success(std::move(unit));
    }

    void GCCTimeReportParser::extract_timing_from_entries(
        const std::vector<TimeEntry>& entries,
        core::CompilationUnit& unit
    ) {
        double total_time = 0.0;
        double parsing_time = 0.0;
        double codegen_time = 0.0;
        double optimization_time = 0.0;

        for (const auto& entry : entries) {
            std::string lower_name = utils::to_lower(entry.phase_name);
            const double time_ms = entry.wall_time * 1000.0;

            total_time += time_ms;

            if (utils::contains(lower_name, "parsing") ||
                utils::contains(lower_name, "phase parsing") ||
                utils::contains(lower_name, "name lookup") ||
                utils::contains(lower_name, "template")) {
                parsing_time += time_ms;
            } else if (utils::contains(lower_name, "preprocessing") ||
                       utils::contains(lower_name, "phase setup")) {
                unit.preprocessing_time_ms += time_ms;
            } else if (utils::contains(lower_name, "expand") ||
                       utils::contains(lower_name, "RTL generation")) {
                codegen_time += time_ms;
            } else if (utils::contains(lower_name, "opt") ||
                       utils::contains(lower_name, "phase opt")) {
                optimization_time += time_ms;
            }
        }

        unit.total_time_ms = total_time;
        unit.parsing_time_ms = parsing_time;
        unit.codegen_time_ms = codegen_time;
        unit.optimization_time_ms = optimization_time;
    }

    std::string GCCTimeReportParser::extract_file_path_from_content(
        const std::string_view content
    ) {
        for (const auto lines = utils::split_lines(content); const auto& line : lines) {
            if (utils::ends_with(line, ".cpp") ||
                utils::ends_with(line, ".cc") ||
                utils::ends_with(line, ".cxx") ||
                utils::ends_with(line, ".c")) {

                for (auto parts = utils::split(line, ' '); const auto& part : parts) {
                    if (utils::ends_with(part, ".cpp") ||
                        utils::ends_with(part, ".cc") ||
                        utils::ends_with(part, ".cxx") ||
                        utils::ends_with(part, ".c")) {
                        return part;
                    }
                }
            }
        }

        return "unknown";
    }

    bool GCCTimeReportParser::is_time_report_line(const std::string_view line) {
        return utils::contains(line, ":") &&
               (utils::contains(line, "phase") ||
                utils::contains(line, "parsing") ||
                utils::contains(line, "name lookup") ||
                utils::contains(line, "template"));
    }

    std::optional<GCCTimeReportParser::TimeEntry>
    GCCTimeReportParser::parse_time_entry_line(const std::string_view line) {
        std::string trimmed = utils::trim(line);

        if (trimmed.empty()) {
            return std::nullopt;
        }

        const size_t colon_pos = trimmed.find(':');
        if (colon_pos == std::string::npos) {
            return std::nullopt;
        }

        TimeEntry entry{};
        entry.phase_name = utils::trim(trimmed.substr(0, colon_pos));

        const std::string time_part = trimmed.substr(colon_pos + 1);
        const auto parts = utils::split(time_part, ' ');

        std::vector<std::string> values;
        for (const auto& part : parts) {
            if (std::string cleaned = utils::trim(part); !cleaned.empty() && cleaned != "(" && cleaned != ")") {
                if (cleaned.back() == '%' || cleaned.back() == ')') {
                    cleaned.pop_back();
                }
                if (!cleaned.empty() && (std::isdigit(cleaned[0]) || cleaned[0] == '.')) {
                    values.push_back(cleaned);
                }
            }
        }

        if (values.size() >= 3) {
            try {
                entry.usr_time = std::stod(values[0]);
                entry.sys_time = std::stod(values[1]);
                entry.wall_time = std::stod(values[2]);

                if (values.size() >= 4) {
                    entry.percentage = std::stod(values[3]);
                }

                return entry;
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

} // namespace bha::parsers