//
// Created by gregorian on 20/10/2025.
//

#include "bha/analysis/template_analyzer.h"
#include "bha/utils/string_utils.h"
#include <algorithm>
#include <ranges>

namespace bha::analysis {

    core::Result<TemplateAnalysisResult> TemplateAnalyzer::analyze_templates(
        const core::BuildTrace& trace,
        int top_n
    ) {
        TemplateAnalysisResult result;

        if (auto expensive_result = find_expensive_templates(trace, top_n); expensive_result.is_success()) {
            result.expensive_templates = expensive_result.value();
        }

        if (auto counts_result = count_instantiations(trace); counts_result.is_success()) {
            result.instantiation_counts = counts_result.value();
        }

        if (auto times_result = calculate_template_times(trace); times_result.is_success()) {
            result.total_times_by_template = times_result.value();
        }

        result.total_template_time_ms = 0.0;
        for (const auto& time : result.total_times_by_template | std::views::values) {
            result.total_template_time_ms += time;
        }

        if (trace.total_build_time_ms > 0) {
            result.template_time_percentage =
                (result.total_template_time_ms / trace.total_build_time_ms) * 100.0;
        }

        return core::Result<TemplateAnalysisResult>::success(std::move(result));
    }

    core::Result<std::vector<core::TemplateHotspot>> TemplateAnalyzer::find_expensive_templates(
        const core::BuildTrace& trace,
        const int top_n,
        const double threshold_ms
    ) {
        std::unordered_map<std::string, core::TemplateHotspot> template_map;

        for (const auto& unit : trace.compilation_units) {
            for (const auto& inst : unit.template_instantiations) {
                if (std::string normalized = normalize_template_name(inst.template_name); template_map.contains(normalized)) {
                    auto& hotspot = template_map[normalized];
                    hotspot.time_ms += inst.time_ms;
                    hotspot.instantiation_count++;
                } else {
                    core::TemplateHotspot hotspot;
                    hotspot.template_name = normalized;
                    hotspot.instantiation_context = inst.instantiation_context;
                    hotspot.time_ms = inst.time_ms;
                    hotspot.instantiation_count = 1;
                    hotspot.instantiation_stack = inst.call_stack;

                    template_map[normalized] = hotspot;
                }
            }
        }

        std::vector<core::TemplateHotspot> hotspots;
        for (const auto& hotspot : template_map | std::views::values) {
            if (hotspot.time_ms >= threshold_ms) {
                hotspots.push_back(hotspot);
            }
        }

        std::ranges::sort(hotspots,
                          [](const core::TemplateHotspot& a, const core::TemplateHotspot& b) {
                              return a.time_ms > b.time_ms;
                          });

        if (hotspots.size() > static_cast<size_t>(top_n)) {
            hotspots.resize(top_n);
        }

        return core::Result<std::vector<core::TemplateHotspot>>::success(std::move(hotspots));
    }

    core::Result<std::unordered_map<std::string, int>> TemplateAnalyzer::count_instantiations(
        const core::BuildTrace& trace
    ) {
        std::unordered_map<std::string, int> counts;

        for (const auto& unit : trace.compilation_units) {
            for (const auto& inst : unit.template_instantiations) {
                std::string normalized = normalize_template_name(inst.template_name);
                counts[normalized]++;
            }
        }

        return core::Result<std::unordered_map<std::string, int>>::success(std::move(counts));
    }

    core::Result<std::unordered_map<std::string, double>> TemplateAnalyzer::calculate_template_times(
        const core::BuildTrace& trace
    ) {
        std::unordered_map<std::string, double> times;

        for (const auto& unit : trace.compilation_units) {
            for (const auto& inst : unit.template_instantiations) {
                std::string normalized = normalize_template_name(inst.template_name);
                times[normalized] += inst.time_ms;
            }
        }

        return core::Result<std::unordered_map<std::string, double>>::success(std::move(times));
    }

    core::Result<std::vector<std::string>> TemplateAnalyzer::suggest_explicit_instantiations(
        const core::BuildTrace& trace,
        const int min_instantiation_count
    ) {
        std::vector<std::string> suggestions;

        auto counts_result = count_instantiations(trace);
        if (!counts_result.is_success()) {
            return core::Result<std::vector<std::string>>::failure(counts_result.error());
        }

        const auto& counts = counts_result.value();

        for (const auto& [template_name, count] : counts) {
            if (count >= min_instantiation_count && !is_std_template(template_name)) {
                suggestions.push_back(template_name);
            }
        }

        std::ranges::sort(suggestions,
                          [&counts](const std::string& a, const std::string& b) {
                              return counts.at(a) > counts.at(b);
                          });

        return core::Result<std::vector<std::string>>::success(std::move(suggestions));
    }

    core::Result<std::vector<std::string>> TemplateAnalyzer::find_template_heavy_files(
        const core::BuildTrace& trace,
        const double threshold_percent
    ) {
        std::vector<std::string> heavy_files;

        for (const auto& unit : trace.compilation_units) {
            if (const double overhead = calculate_template_overhead(unit); overhead >= threshold_percent) {
                heavy_files.push_back(unit.file_path);
            }
        }

        return core::Result<std::vector<std::string>>::success(std::move(heavy_files));
    }

    double TemplateAnalyzer::calculate_template_overhead(
        const core::CompilationUnit& unit
    ) {
        if (unit.total_time_ms <= 0) {
            return 0.0;
        }

        double template_time = 0.0;
        for (const auto& inst : unit.template_instantiations) {
            template_time += inst.time_ms;
        }

        return (template_time / unit.total_time_ms) * 100.0;
    }

    std::string TemplateAnalyzer::normalize_template_name(const std::string& name) {
        std::string normalized = name;

        normalized = utils::replace_all(normalized, " >", ">");
        normalized = utils::replace_all(normalized, "< ", "<");
        normalized = utils::replace_all(normalized, " ,", ",");
        normalized = utils::replace_all(normalized, ", ", ",");

        if (const size_t angle_pos = normalized.find('<'); angle_pos != std::string::npos) {
            if (const size_t end_pos = normalized.rfind('>'); end_pos != std::string::npos) {
                const std::string params = normalized.substr(angle_pos + 1, end_pos - angle_pos - 1);
                auto param_list = utils::split(params, ',');

                std::vector<std::string> simplified;
                for (auto& param : param_list) {
                    param = utils::trim(param);
                    if (utils::starts_with(param, "std::")) {
                        simplified.push_back(param);
                    } else {
                        simplified.emplace_back("T");
                    }
                }

                normalized = normalized.substr(0, angle_pos + 1) +
                            utils::join(simplified, ",") + ">";
            }
        }

        return normalized;
    }

    bool TemplateAnalyzer::is_std_template(const std::string& template_name) {
        return utils::starts_with(template_name, "std::") ||
               utils::contains(template_name, "std::vector") ||
               utils::contains(template_name, "std::map") ||
               utils::contains(template_name, "std::string") ||
               utils::contains(template_name, "std::shared_ptr") ||
               utils::contains(template_name, "std::unique_ptr");
    }

} // namespace bha::analysis