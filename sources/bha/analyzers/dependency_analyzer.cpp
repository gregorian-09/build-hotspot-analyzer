//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/dependency_analyzer.hpp"
#include "bha/git/git_integration.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <ranges>
#include <sstream>
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

        bool is_external_header(const fs::path& header_path) {
            const std::string path_str = header_path.string();
            return path_str.find("/usr/") == 0 ||
                   path_str.find("/opt/") == 0 ||
                   path_str.find("C:\\Program Files") == 0 ||
                   path_str.find("third_party") != std::string::npos ||
                   path_str.find("external") != std::string::npos ||
                   path_str.find("dependencies") != std::string::npos;
        }

        struct FileModificationInfo {
            std::size_t modification_count = 0;
            Timestamp last_modified = Timestamp{};
            Duration time_since_modification = Duration::zero();
        };

        FileModificationInfo analyze_file_history(
            const fs::path& file_path,
            const fs::path& repo_dir
        ) {
            FileModificationInfo info;

            auto log_result = git::execute_git(
                {"log", "--follow", "--format=%aI", "--", file_path.string()},
                repo_dir,
                std::chrono::seconds(10)
            );

            if (log_result.is_err() || log_result.value().exit_code != 0) {
                return info;
            }

            std::string output = log_result.value().stdout_output;
            std::istringstream ss(output);
            std::string line;

            std::vector<Timestamp> modification_dates;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;

                std::tm tm{};
                std::istringstream date_ss(line);
                date_ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
                if (!date_ss.fail()) {
                    modification_dates.push_back(
                        std::chrono::system_clock::from_time_t(std::mktime(&tm))
                    );
                }
            }

            info.modification_count = modification_dates.size();

            if (!modification_dates.empty()) {
                info.last_modified = modification_dates.front();
                auto now = std::chrono::system_clock::now();
                info.time_since_modification =
                    std::chrono::duration_cast<Duration>(now - info.last_modified);
            }

            return info;
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

        std::optional<fs::path> repo_root;
        if (trace.git_info.has_value()) {
            if (auto root_result = git::get_repository_root(); root_result.is_ok()) {
                repo_root = root_result.value();
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

            info.is_external = is_external_header(path);

            if (repo_root.has_value() && !info.is_external) {
                auto [modification_count, last_modified, time_since_modification] = analyze_file_history(path, repo_root.value());
                info.modification_count = modification_count;
                info.last_modified = last_modified;
                info.time_since_modification = time_since_modification;

                constexpr auto six_months = std::chrono::hours(24 * 30 * 6);
                constexpr std::size_t stable_mod_threshold = 5;

                info.is_stable = (info.time_since_modification >= six_months) ||
                                (info.modification_count > 0 &&
                                 info.modification_count <= stable_mod_threshold &&
                                 info.time_since_modification >= std::chrono::hours(24 * 30));
            } else if (info.is_external) {
                info.is_stable = true;
            }

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

        std::unordered_map<std::string, std::unordered_set<std::string>> include_graph;
        for (const auto& unit : trace.units) {
            std::string source_key = path_key(unit.source_file);
            for (const auto& include : unit.includes) {
                std::string header_key = path_key(include.header);
                include_graph[source_key].insert(header_key);
            }
        }

        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> rec_stack;
        std::function<void(const std::string&)> detect_cycles;

        detect_cycles = [&](const std::string& node) {
            visited.insert(node);
            rec_stack.insert(node);

            if (include_graph.contains(node)) {
                for (const auto& neighbor : include_graph[node]) {
                    if (!visited.contains(neighbor)) {
                        detect_cycles(neighbor);
                    } else if (rec_stack.contains(neighbor)) {
                        result.dependencies.circular_dependencies.emplace_back(
                            fs::path(node), fs::path(neighbor)
                        );
                    }
                }
            }

            rec_stack.erase(node);
        };

        for (const auto& node : include_graph | std::views::keys) {
            if (!visited.contains(node)) {
                detect_cycles(node);
            }
        }

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