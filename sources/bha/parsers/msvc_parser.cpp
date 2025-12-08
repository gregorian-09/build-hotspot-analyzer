//
// Created by gregorian on 15/10/2025.
//

#include "bha/parsers/msvc_parser.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/path_utils.h"
#include "bha/utils/string_utils.h"
#include "bha/utils/hash_utils.h"
#include <algorithm>

namespace bha::parsers {

    core::Result<std::vector<core::CompilationUnit>> MSVCTraceParser::parse(
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

    core::Result<std::vector<core::CompilationUnit>> MSVCTraceParser::parse_string(
        std::string_view content
    ) {
        auto file_times_result = parse_file_times(content);
        if (!file_times_result.is_success()) {
            return core::Result<std::vector<core::CompilationUnit>>::failure(
                file_times_result.error()
            );
        }
        
        auto include_times_result = parse_include_times(content);
        auto template_times_result = parse_template_times(content);
        
        const auto& file_times = file_times_result.value();
        const auto& include_times = include_times_result.is_success() ? 
                                    include_times_result.value() : 
                                    std::vector<TimeEntry>{};
        const auto& template_times = template_times_result.is_success() ? 
                                     template_times_result.value() : 
                                     std::vector<TemplateEntry>{};
        
        auto unit_result = build_compilation_unit(file_times, include_times, template_times);
        if (!unit_result.is_success()) {
            return core::Result<std::vector<core::CompilationUnit>>::failure(
                unit_result.error()
            );
        }
        
        std::vector<core::CompilationUnit> units;
        units.push_back(std::move(unit_result.value()));
        
        return core::Result<std::vector<core::CompilationUnit>>::success(std::move(units));
    }

    std::string MSVCTraceParser::get_format_name() const {
        return "msvc-trace";
    }

    CompilerType MSVCTraceParser::get_compiler_type() const {
        return CompilerType::MSVC;
    }

    bool MSVCTraceParser::can_parse(const std::string_view file_path) const {
        const auto content = utils::read_file(file_path);
        if (!content) {
            return false;
        }
        
        return utils::contains(*content, "c1xx.dll") ||
               utils::contains(*content, "time(");
    }

    ParserCapabilities MSVCTraceParser::get_capabilities() const {
        ParserCapabilities caps;
        caps.supports_timing = true;
        caps.supports_templates = true;
        caps.supports_preprocessing = false;
        caps.supports_optimization = false;
        caps.supports_dependencies = true;
        return caps;
    }

    std::vector<std::string> MSVCTraceParser::get_supported_extensions() const {
        return {".txt", ".log"};
    }

    core::Result<std::vector<MSVCTraceParser::TimeEntry>> 
    MSVCTraceParser::parse_file_times(const std::string_view content) {
        std::vector<TimeEntry> entries;
        const auto lines = utils::split_lines(content);
        
        bool in_file_section = false;
        
        for (const auto& line : lines) {
            if (utils::contains(line, "c1xx.dll")) {
                in_file_section = true;
                continue;
            }
            
            if (in_file_section) {
                if (line.empty() || utils::is_empty_or_whitespace(line)) {
                    if (!entries.empty()) {
                        break;
                    }
                    continue;
                }

                if (auto entry_opt = parse_time_line(line)) {
                    entries.push_back(*entry_opt);
                }
            }
        }
        
        if (entries.empty()) {
            return core::Result<std::vector<TimeEntry>>::failure(
                core::ErrorCode::PARSE_ERROR,
                "No file time entries found in MSVC trace"
            );
        }
        
        return core::Result<std::vector<TimeEntry>>::success(std::move(entries));
    }

    core::Result<std::vector<MSVCTraceParser::TimeEntry>> 
    MSVCTraceParser::parse_include_times(const std::string_view content) {
        std::vector<TimeEntry> entries;
        const auto lines = utils::split_lines(content);
        
        bool in_include_section = false;
        
        for (const auto& line : lines) {
            if (utils::contains(line, "Include Time Summary") ||
                utils::contains(line, "Header Units Time Summary")) {
                in_include_section = true;
                continue;
            }
            
            if (in_include_section) {
                if (line.empty() || utils::is_empty_or_whitespace(line)) {
                    continue;
                }
                
                if (utils::contains(line, "Template")) {
                    break;
                }

                if (auto entry_opt = parse_time_line(line)) {
                    entries.push_back(*entry_opt);
                }
            }
        }
        
        return core::Result<std::vector<TimeEntry>>::success(std::move(entries));
    }

    core::Result<std::vector<MSVCTraceParser::TemplateEntry>> 
    MSVCTraceParser::parse_template_times(const std::string_view content) {
        std::vector<TemplateEntry> entries;
        const auto lines = utils::split_lines(content);
        
        bool in_template_section = false;
        
        for (const auto& line : lines) {
            if (utils::contains(line, "Template Instantiation Time") ||
                utils::contains(line, "Class Template Member Functions")) {
                in_template_section = true;
                continue;
            }
            
            if (in_template_section) {
                if (line.empty() || utils::is_empty_or_whitespace(line)) {
                    continue;
                }

                if (auto entry_opt = parse_template_line(line)) {
                    entries.push_back(*entry_opt);
                }
            }
        }
        
        return core::Result<std::vector<TemplateEntry>>::success(std::move(entries));
    }

    core::Result<core::CompilationUnit> MSVCTraceParser::build_compilation_unit(
        const std::vector<TimeEntry>& file_times,
        const std::vector<TimeEntry>& include_times,
        const std::vector<TemplateEntry>& template_times
    ) {
        core::CompilationUnit unit;

        const std::string main_file = extract_main_file(file_times);
        unit.file_path = main_file;
        unit.compiler_type = "msvc";
        unit.id = utils::compute_hash_hex(main_file);
        unit.build_timestamp = std::chrono::system_clock::now();
        
        double total_time = 0.0;
        for (const auto& entry : file_times) {
            total_time += entry.time_seconds;
        }
        unit.total_time_ms = total_time * 1000.0;
        
        double include_time = 0.0;
        for (const auto& entry : include_times) {
            include_time += entry.time_seconds;
            unit.direct_includes.push_back(entry.file_or_header);
        }
        unit.preprocessing_time_ms = include_time * 1000.0;
        
        for (const auto& [template_name, time_seconds] : template_times) {
            core::TemplateInstantiation inst;
            inst.template_name = template_name;
            inst.time_ms = time_seconds * 1000.0;
            inst.instantiation_depth = 0;
            unit.template_instantiations.push_back(std::move(inst));
        }
        
        std::ranges::sort(unit.template_instantiations.begin(),
                  unit.template_instantiations.end(),
                  [](const auto& a, const auto& b) {
                      return a.time_ms > b.time_ms;
                  });
        
        return core::Result<core::CompilationUnit>::success(std::move(unit));
    }

    std::string MSVCTraceParser::extract_main_file(const std::vector<TimeEntry>& file_times) {
        for (const auto& entry : file_times) {
            if (utils::ends_with(entry.file_or_header, ".cpp") ||
                utils::ends_with(entry.file_or_header, ".cc") ||
                utils::ends_with(entry.file_or_header, ".cxx") ||
                utils::ends_with(entry.file_or_header, ".c")) {
                return entry.file_or_header;
            }
        }
        
        if (!file_times.empty()) {
            return file_times[0].file_or_header;
        }
        
        return "unknown";
    }

    std::optional<MSVCTraceParser::TimeEntry> 
    MSVCTraceParser::parse_time_line(const std::string_view line) {
        std::string trimmed = utils::trim(line);
        
        if (trimmed.empty()) {
            return std::nullopt;
        }

        const size_t time_start = trimmed.find("time(");
        if (time_start == std::string::npos) {
            return std::nullopt;
        }

        const size_t time_end = trimmed.find(')', time_start);
        if (time_end == std::string::npos) {
            return std::nullopt;
        }

        const size_t equals_pos = trimmed.find('=', time_start);
        if (equals_pos == std::string::npos || equals_pos > time_end) {
            return std::nullopt;
        }

        const std::string file_part = utils::trim(trimmed.substr(time_start + 5, equals_pos - time_start - 5));
        const std::string time_str = utils::trim(trimmed.substr(equals_pos + 1, time_end - equals_pos - 1));
        
        TimeEntry entry;
        entry.file_or_header = file_part;
        entry.time_seconds = parse_time_value(time_str);
        entry.count = 1;

        if (const size_t times_pos = trimmed.find(" times", time_end); times_pos != std::string::npos) {
            if (const size_t paren_pos = trimmed.rfind('(', times_pos); paren_pos != std::string::npos) {
                const std::string count_str = utils::trim(trimmed.substr(paren_pos + 1, times_pos - paren_pos - 1));
                try {
                    entry.count = std::stoi(count_str);
                } catch (...) {
                }
            }
        }
        
        return entry;
    }

    std::optional<MSVCTraceParser::TemplateEntry>
    MSVCTraceParser::parse_template_line(const std::string_view line) {
        const std::string trimmed = utils::trim(line);

        if (trimmed.empty()) {
            return std::nullopt;
        }

        // Find the delimiter colon (skip :: in template names)
        size_t colon_pos = std::string::npos;
        for (size_t i = 0; i < trimmed.length(); ++i) {
            if (trimmed[i] == ':') {
                // Check if this is part of ::
                if (i + 1 < trimmed.length() && trimmed[i + 1] == ':') {
                    ++i; // Skip the ::
                    continue;
                }
                colon_pos = i;
                break;
            }
        }

        if (colon_pos == std::string::npos) {
            return std::nullopt;
        }

        const std::string time_part = utils::trim(trimmed.substr(0, colon_pos));
        const std::string template_part = utils::trim(trimmed.substr(colon_pos + 1));

        if (template_part.empty()) {
            return std::nullopt;
        }

        const double time_seconds = parse_time_value(time_part);

        if (time_seconds <= 0.0) {
            return std::nullopt;
        }

        TemplateEntry entry;
        entry.template_name = template_part;
        entry.time_seconds = time_seconds;

        return entry;
    }

    double MSVCTraceParser::parse_time_value(const std::string_view time_str) {
        std::string cleaned = utils::trim(time_str);
        
        cleaned = utils::replace_all(cleaned, "s", "");
        cleaned = utils::replace_all(cleaned, "ms", "");
        cleaned = utils::trim(cleaned);
        
        if (cleaned.empty()) {
            return 0.0;
        }
        
        try {
            return std::stod(cleaned);
        } catch (...) {
            return 0.0;
        }
    }

} // namespace bha::parsers