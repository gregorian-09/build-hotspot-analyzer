//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/nvcc_parser.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <algorithm>
#include <regex>
#include <charconv>
#include <optional>

namespace bha::parsers {

    namespace {

        constexpr std::string_view NVCC_MARKER = "nvcc";
        constexpr std::string_view PTXAS_MARKER = "ptxas";
        constexpr std::string_view FATBIN_MARKER = "fatbinary";
        constexpr std::string_view CICC_MARKER = "cicc";

        std::optional<double> parse_number(const std::string_view token) {
            auto trimmed = utils::trim(token);
            double value = 0.0;
            if (trimmed.empty()) {
                return std::nullopt;
            }
            const auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
            if (ec != std::errc() || ptr != trimmed.data() + trimmed.size()) {
                return std::nullopt;
            }
            return value;
        }

        std::optional<Duration> parse_duration_from_text(const std::string_view text) {
            const std::string raw(text);
            static const std::regex min_sec_regex(
                R"((\d+\.?\d*)\s*m(?:in(?:ute)?s?)?\s*(\d+\.?\d*)\s*s(?:ec(?:ond)?s?)?)",
                std::regex_constants::icase
            );
            static const std::regex sec_regex(
                R"((\d+\.?\d*)\s*s(?:ec(?:ond)?s?)?)",
                std::regex_constants::icase
            );
            static const std::regex min_regex(
                R"((\d+\.?\d*)\s*m(?:in(?:ute)?s?)?)",
                std::regex_constants::icase
            );

            if (std::smatch match; std::regex_search(raw, match, min_sec_regex) && match.size() >= 3) {
                const auto minutes = parse_number(match[1].str());
                const auto seconds = parse_number(match[2].str());
                if (minutes.has_value() && seconds.has_value()) {
                    const double total_seconds = (*minutes * 60.0) + *seconds;
                    return std::chrono::duration_cast<Duration>(std::chrono::duration<double>(total_seconds));
                }
            }
            if (std::smatch match; std::regex_search(raw, match, sec_regex) && match.size() >= 2) {
                if (const auto seconds = parse_number(match[1].str()); seconds.has_value()) {
                    return std::chrono::duration_cast<Duration>(std::chrono::duration<double>(*seconds));
                }
            }
            if (std::smatch match; std::regex_search(raw, match, min_regex) && match.size() >= 2) {
                if (const auto minutes = parse_number(match[1].str()); minutes.has_value()) {
                    return std::chrono::duration_cast<Duration>(std::chrono::duration<double>(*minutes * 60.0));
                }
            }
            return std::nullopt;
        }

        struct NVCCPhase {
            std::string name;
            Duration time = Duration::zero();
        };

        std::vector<NVCCPhase> parse_nvcc_phases(std::string_view content) {
            std::vector<NVCCPhase> phases;
            static const std::regex prefix_min_sec_regex(
                R"(^\s*(\d+\.?\d*)\s*m(?:in(?:ute)?s?)?\s*(\d+\.?\d*)\s*s(?:ec(?:ond)?s?)?\s+(.+)$)",
                std::regex_constants::icase
            );
            static const std::regex prefix_sec_regex(
                R"(^\s*(\d+\.?\d*)\s*s(?:ec(?:ond)?s?)?\s+(.+)$)",
                std::regex_constants::icase
            );
            static const std::regex prefix_min_regex(
                R"(^\s*(\d+\.?\d*)\s*m(?:in(?:ute)?s?)?\s+(.+)$)",
                std::regex_constants::icase
            );

            auto add_phase = [&phases](const std::string& name, const Duration time) {
                if (name.empty() || time <= Duration::zero()) {
                    return;
                }
                const auto normalized_name_view = utils::trim(name);
                const std::string normalized_name(normalized_name_view);
                if (normalized_name.empty()) {
                    return;
                }
                const auto already_exists = std::ranges::any_of(phases, [&](const NVCCPhase& phase) {
                    return phase.name == normalized_name && phase.time == time;
                });
                if (!already_exists) {
                    phases.push_back(NVCCPhase{normalized_name, time});
                }
            };

            for (const auto lines = utils::split(content, '\n'); const auto& line_view : lines) {
                const std::string line(utils::trim(line_view));
                if (line.empty()) {
                    continue;
                }

                if (std::smatch match; std::regex_match(line, match, prefix_min_sec_regex) && match.size() >= 4) {
                    const auto minutes = parse_number(match[1].str());
                    const auto seconds = parse_number(match[2].str());
                    if (minutes.has_value() && seconds.has_value()) {
                        const auto total_seconds = (*minutes * 60.0) + *seconds;
                        add_phase(
                            match[3].str(),
                            std::chrono::duration_cast<Duration>(std::chrono::duration<double>(total_seconds))
                        );
                        continue;
                    }
                }
                if (std::smatch match; std::regex_match(line, match, prefix_sec_regex) && match.size() >= 3) {
                    if (const auto seconds = parse_number(match[1].str()); seconds.has_value()) {
                        add_phase(
                            match[2].str(),
                            std::chrono::duration_cast<Duration>(std::chrono::duration<double>(*seconds))
                        );
                        continue;
                    }
                }
                if (std::smatch match; std::regex_match(line, match, prefix_min_regex) && match.size() >= 3) {
                    if (const auto minutes = parse_number(match[1].str()); minutes.has_value()) {
                        add_phase(
                            match[2].str(),
                            std::chrono::duration_cast<Duration>(std::chrono::duration<double>(*minutes * 60.0))
                        );
                        continue;
                    }
                }

                const auto sep_pos = line.find_first_of(":=");
                if (sep_pos == std::string::npos || sep_pos == 0) {
                    continue;
                }
                const std::string name(utils::trim(line.substr(0, sep_pos)));
                const auto maybe_duration = parse_duration_from_text(line.substr(sep_pos + 1));
                if (maybe_duration.has_value()) {
                    add_phase(name, *maybe_duration);
                }
            }

            return phases;
        }

    }  // namespace

    bool NVCCTraceParser::can_parse(const fs::path& path) const {
        if (const auto ext = path.extension().string(); ext != ".txt" && ext != ".log" && ext != ".nvlog") {
            return false;
        }

        auto result = utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool NVCCTraceParser::can_parse_content(std::string_view content) const {
        const auto lower = utils::to_lower(std::string(content.substr(0, 1000)));

        const bool has_nvcc = utils::contains(lower, NVCC_MARKER);
        const bool has_cuda_tools = utils::contains(lower, PTXAS_MARKER) ||
                              utils::contains(lower, FATBIN_MARKER) ||
                              utils::contains(lower, CICC_MARKER);

        return has_nvcc || has_cuda_tools;
    }

    Result<CompilationUnit, Error> NVCCTraceParser::parse_file(
        const fs::path& path
    ) const {
        auto content_result = utils::read_file(path);
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
            auto lower_name = utils::to_lower(name);
            total_time += time;

            if (utils::contains(lower_name, "compile") ||
                utils::contains(lower_name, "host") ||
                utils::contains(lower_name, "c++")) {
                host_time += time;
            }
            else if (utils::contains(lower_name, "ptx") ||
                     utils::contains(lower_name, "cicc") ||
                     utils::contains(lower_name, "device")) {
                device_time += time;
            }
            else if (utils::contains(lower_name, "fat") ||
                     utils::contains(lower_name, "link") ||
                     utils::contains(lower_name, "nvlink")) {
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
