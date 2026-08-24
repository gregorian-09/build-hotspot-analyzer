//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/template_analyzer.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <algorithm>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace bha::analyzers
{
    namespace {

        struct TemplateStats {
            std::string name;
            std::string full_signature;
            Duration total_time = Duration::zero();
            std::size_t instantiation_count = 0;
            std::vector<SourceLocation> locations;
            std::unordered_set<std::string> files_using;
        };

    }  // namespace

    Result<AnalysisResult, Error> TemplateAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        const auto start_time = std::chrono::steady_clock::now();

        if (!options.analyze_templates) {
            result.analysis_time = std::chrono::system_clock::now();
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        std::unordered_map<std::string, TemplateStats> template_map;
        Duration total_template_time = Duration::zero();
        const Duration total_build_time = trace.total_time;

        for (const auto& unit : trace.units) {
            for (const auto& tmpl : unit.templates) {
                if (tmpl.full_signature.empty()) {
                    // A friendly name is not a stable specialization identity.
                    // Do not collapse unidentified producer rows into one entry.
                    continue;
                }

                auto& [name, full_signature, total_time, instantiation_count, locations, files_using] =
                    template_map[tmpl.full_signature];

                if (full_signature.empty()) {
                    name = tmpl.name;
                    full_signature = tmpl.full_signature;
                }

                if (const auto sum = utils::checked_add_duration(total_time, tmpl.time); sum.has_value()) {
                    total_time = *sum;
                } else {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error(
                            "Template timing exceeded the supported aggregate duration range"
                        )
                    );
                }
                if (const auto sum = utils::checked_add(instantiation_count, tmpl.count); sum.has_value()) {
                    instantiation_count = *sum;
                } else {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Template instantiation count overflowed")
                    );
                }
                if (const auto sum = utils::checked_add_duration(total_template_time, tmpl.time); sum.has_value()) {
                    total_template_time = *sum;
                } else {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error(
                            "Total template timing exceeded the supported aggregate duration range"
                        )
                    );
                }

                if (tmpl.location.has_location()) {
                    locations.push_back(tmpl.location);
                }

                if (!unit.source_file.empty()) {
                    files_using.insert(unit.source_file.string());
                }
            }
        }

        result.templates.templates.reserve(template_map.size());

        for (auto& [name, full_signature, total_time, instantiation_count, locations, files_using] :
             template_map | std::views::values) {
            TemplateAnalysisResult::TemplateInfo info;
            info.name = name;
            info.full_signature = full_signature;
            info.total_time = total_time;
            info.instantiation_count = instantiation_count;
            info.locations = std::move(locations);
            info.files_using.reserve(files_using.size());
            for (const auto& source_file : files_using) {
                info.files_using.push_back(source_file);
            }

            if (total_template_time.count() > 0) {
                info.time_percent = 100.0 * static_cast<double>(total_time.count()) /
                                   static_cast<double>(total_template_time.count());
            }

            result.templates.templates.push_back(std::move(info));
        }

        std::ranges::sort(result.templates.templates,
                          [](const auto& a, const auto& b) {
                              return a.total_time > b.total_time;
                          });

        result.templates.total_template_time = total_template_time;

        if (total_build_time.count() > 0) {
            result.templates.template_time_percent =
                100.0 * static_cast<double>(total_template_time.count()) /
                static_cast<double>(total_build_time.count());
        }

        result.templates.total_instantiations = 0;
        for (const auto& tmpl : result.templates.templates) {
            const auto sum = utils::checked_add(
                result.templates.total_instantiations,
                tmpl.instantiation_count
            );
            if (!sum.has_value()) {
                return Result<AnalysisResult, Error>::failure(
                    Error::analysis_error("Total template instantiation count overflowed")
                );
            }
            result.templates.total_instantiations = *sum;
        }

        const auto end_time = std::chrono::steady_clock::now();
        result.analysis_time = std::chrono::system_clock::now();
        result.analysis_duration = std::chrono::duration_cast<Duration>(end_time - start_time);

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_template_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<TemplateAnalyzer>()
        );
    }
}  // namespace bha::analyzers
