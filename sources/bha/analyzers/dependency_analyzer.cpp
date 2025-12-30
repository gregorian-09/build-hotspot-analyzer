//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/dependency_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace bha::analyzers
{
    namespace {

        struct HeaderStats {
            fs::path path;
            Duration total_parse_time = Duration::zero();
            std::size_t inclusion_count = 0;
            std::unordered_set<std::string> including_files;
        };

        std::string path_key(const fs::path& p) {
            return p.lexically_normal().string();
        }

    }  // namespace

    Result<AnalysisResult, Error> DependencyAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        const auto start_time = std::chrono::steady_clock::now();

        std::unordered_map<std::string, HeaderStats> header_map;
        std::size_t max_depth = 0;
        Duration total_include_time = Duration::zero();

        for (const auto& unit : trace.units) {
            std::string source_key = path_key(unit.source_file);

            for (const auto& include : unit.includes) {
                std::string header_key = path_key(include.header);

                auto& [path, total_parse_time, inclusion_count, including_files] = header_map[header_key];
                if (path.empty()) {
                    path = include.header;
                }

                total_parse_time += include.parse_time;
                inclusion_count += 1;
                including_files.insert(source_key);

                total_include_time += include.parse_time;
                max_depth = std::max(max_depth, include.depth);
            }
        }

        result.dependencies.headers.reserve(header_map.size());

        for (auto& [path, total_parse_time, inclusion_count, including_files] : header_map | std::views::values) {
            DependencyAnalysisResult::HeaderInfo info;
            info.path = path;
            info.total_parse_time = total_parse_time;
            info.inclusion_count = inclusion_count;
            info.including_files = including_files.size();

            for (const auto& file : including_files) {
                info.included_by.emplace_back(file);
            }

            const auto time_factor = static_cast<double>(total_parse_time.count());
            const auto count_factor = static_cast<double>(inclusion_count);
            info.impact_score = time_factor * std::sqrt(count_factor);

            result.dependencies.headers.push_back(std::move(info));
        }

        std::ranges::sort(result.dependencies.headers,
                          [](const auto& a, const auto& b) {
                              return a.impact_score > b.impact_score;
                          });

        result.dependencies.total_includes = 0;
        for (const auto& unit : trace.units) {
            result.dependencies.total_includes += unit.includes.size();
        }
        result.dependencies.unique_headers = header_map.size();
        result.dependencies.max_include_depth = max_depth;
        result.dependencies.total_include_time = total_include_time;

        const auto end_time = std::chrono::steady_clock::now();
        result.analysis_time = std::chrono::system_clock::now();
        result.analysis_duration = std::chrono::duration_cast<Duration>(end_time - start_time);

        (void)options;

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_dependency_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<DependencyAnalyzer>()
        );
    }
}  // namespace bha::analyzers