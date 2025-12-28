//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/nvcc_parser.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <regex>
#include <charconv>

namespace bha::parsers {

    namespace {

        constexpr std::string_view NVCC_MARKER = "nvcc";
        constexpr std::string_view PTXAS_MARKER = "ptxas";
        constexpr std::string_view FATBIN_MARKER = "fatbinary";
        constexpr std::string_view CICC_MARKER = "cicc";

        Duration parse_nvcc_time(const std::string_view time_str) {
            double seconds = 0.0;
            auto trimmed = string_utils::trim(time_str);

            if (string_utils::ends_with(trimmed, "s")) {
                trimmed = trimmed.substr(0, trimmed.size() - 1);
            }
            if (string_utils::ends_with(trimmed, "m")) {
                trimmed = trimmed.substr(0, trimmed.size() - 1);
                std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), seconds);
                seconds *= 60.0;
                return std::chrono::duration_cast<Duration>(
                    std::chrono::duration<double>(seconds));
            }

            std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), seconds);
            return std::chrono::duration_cast<Duration>(
                std::chrono::duration<double>(seconds)
            );
        }

        struct NVCCPhase {
            std::string name;
            Duration time = Duration::zero();
        };

        std::vector<NVCCPhase> parse_nvcc_phases(std::string_view content) {
            std::vector<NVCCPhase> phases;

            const std::regex time_regex(R"((\w+)\s*[:=]\s*(\d+\.?\d*)\s*s)");
            const std::string content_str(content);
            std::smatch match;

            auto it = content_str.cbegin();
            while (std::regex_search(it, content_str.cend(), match, time_regex)) {
                NVCCPhase phase;
                phase.name = match[1].str();
                phase.time = parse_nvcc_time(match[2].str());
                phases.push_back(std::move(phase));
                it = match.suffix().first;
            }

            const std::regex alt_regex(R"((\d+\.?\d*)\s*s\s+(\w+))");
            it = content_str.cbegin();
            while (std::regex_search(it, content_str.cend(), match, alt_regex)) {
                NVCCPhase phase;
                phase.name = match[2].str();
                phase.time = parse_nvcc_time(match[1].str());

                bool exists = false;
                for (const auto& [name, time] : phases) {
                    if (name == phase.name) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    phases.push_back(std::move(phase));
                }
                it = match.suffix().first;
            }

            return phases;
        }

    }  // namespace

    bool NVCCTraceParser::can_parse(const fs::path& path) const {
        if (const auto ext = path.extension().string(); ext != ".txt" && ext != ".log" && ext != ".nvlog") {
            return false;
        }

        auto result = file_utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool NVCCTraceParser::can_parse_content(std::string_view content) const {
        const auto lower = string_utils::to_lower(std::string(content.substr(0, 1000)));

        const bool has_nvcc = string_utils::contains(lower, NVCC_MARKER);
        const bool has_cuda_tools = string_utils::contains(lower, PTXAS_MARKER) ||
                              string_utils::contains(lower, FATBIN_MARKER) ||
                              string_utils::contains(lower, CICC_MARKER);

        return has_nvcc || has_cuda_tools;
    }

    Result<CompilationUnit, Error> NVCCTraceParser::parse_file(
        const fs::path& path
    ) const {
        auto content_result = file_utils::read_file(path);
        if (content_result.is_err()) {
            return Result<CompilationUnit, Error>::failure(content_result.error());
        }

        auto source_file = path;
        source_file.replace_extension(".cu");

        return parse_content(content_result.value(), source_file);
    }

    Result<CompilationUnit, Error> NVCCTraceParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        CompilationUnit unit;
        unit.source_file = source_hint;
        unit.metrics.path = source_hint;

        const auto phases = parse_nvcc_phases(content);

        Duration host_time = Duration::zero();
        Duration device_time = Duration::zero();
        Duration link_time = Duration::zero();
        Duration total_time = Duration::zero();

        for (const auto& [name, time] : phases) {
            auto lower_name = string_utils::to_lower(name);
            total_time += time;

            if (string_utils::contains(lower_name, "compile") ||
                string_utils::contains(lower_name, "host") ||
                string_utils::contains(lower_name, "c++")) {
                host_time += time;
            }
            else if (string_utils::contains(lower_name, "ptx") ||
                     string_utils::contains(lower_name, "cicc") ||
                     string_utils::contains(lower_name, "device")) {
                device_time += time;
            }
            else if (string_utils::contains(lower_name, "fat") ||
                     string_utils::contains(lower_name, "link") ||
                     string_utils::contains(lower_name, "nvlink")) {
                link_time += time;
            }
        }

        unit.metrics.total_time = total_time;
        unit.metrics.frontend_time = host_time;
        unit.metrics.backend_time = device_time + link_time;

        unit.metrics.breakdown.parsing = host_time / 3;
        unit.metrics.breakdown.semantic_analysis = host_time / 3;
        unit.metrics.breakdown.template_instantiation = host_time / 3;
        unit.metrics.breakdown.code_generation = device_time;
        unit.metrics.breakdown.optimization = link_time;

        return Result<CompilationUnit, Error>::success(std::move(unit));
    }

    void register_nvcc_parser() {
        ParserRegistry::instance().register_parser(
            std::make_unique<NVCCTraceParser>()
        );
    }

}  // namespace bha::parsers