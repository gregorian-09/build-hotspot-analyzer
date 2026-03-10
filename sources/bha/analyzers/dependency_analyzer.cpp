//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/dependency_analyzer.hpp"
#include "bha/git/git_integration.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
#include <ranges>
#include <regex>
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

        struct IncludeDirective {
            std::string header_name;
            bool is_system = false;
        };

        struct HeaderIncludeGraph {
            std::vector<fs::path> nodes;
            std::vector<std::unordered_set<std::size_t>> adjacency;
        };

        std::vector<IncludeDirective> parse_include_directives(const fs::path& file_path) {
            std::vector<IncludeDirective> directives;

            std::ifstream in(file_path);
            if (!in) {
                return directives;
            }

            static const std::regex include_regex(R"(^\s*#\s*include\s*([<"])([^">]+)[">])");
            std::string line;
            while (std::getline(in, line)) {
                std::smatch match;
                if (!std::regex_search(line, match, include_regex)) {
                    continue;
                }

                IncludeDirective directive;
                directive.is_system = match[1].str() == "<";
                directive.header_name = fs::path(match[2].str()).lexically_normal().generic_string();
                directives.push_back(std::move(directive));
            }

            return directives;
        }

        bool path_ends_with_include(const std::string& full_path, const std::string& include_path) {
            if (full_path == include_path) {
                return true;
            }
            if (include_path.empty() || full_path.size() <= include_path.size()) {
                return false;
            }
            if (!full_path.ends_with(include_path)) {
                return false;
            }
            const auto sep_pos = full_path.size() - include_path.size() - 1;
            return full_path[sep_pos] == '/';
        }

        std::optional<std::size_t> resolve_include_target(
            const IncludeDirective& directive,
            const fs::path& including_header,
            const std::unordered_map<std::string, std::size_t>& key_to_index,
            const std::unordered_map<std::string, std::vector<std::size_t>>& filename_to_indices,
            const std::vector<fs::path>& nodes
        ) {
            if (directive.header_name.empty() || directive.is_system) {
                return std::nullopt;
            }

            const fs::path include_path(directive.header_name);
            if (include_path.is_absolute()) {
                if (const auto it = key_to_index.find(path_key(include_path)); it != key_to_index.end()) {
                    return it->second;
                }
            }

            const fs::path local_candidate = (including_header.parent_path() / include_path).lexically_normal();
            if (const auto it = key_to_index.find(path_key(local_candidate)); it != key_to_index.end()) {
                return it->second;
            }

            const std::string include_filename = include_path.filename().string();
            const auto by_name = filename_to_indices.find(include_filename);
            if (by_name == filename_to_indices.end() || by_name->second.empty()) {
                return std::nullopt;
            }

            if (directive.header_name.find('/') != std::string::npos ||
                directive.header_name.find('\\') != std::string::npos) {
                std::vector<std::size_t> matches;
                matches.reserve(by_name->second.size());
                const std::string normalized_include = fs::path(directive.header_name).lexically_normal().generic_string();
                for (const auto idx : by_name->second) {
                    if (path_ends_with_include(nodes[idx].generic_string(), normalized_include)) {
                        matches.push_back(idx);
                    }
                }
                if (matches.size() == 1) {
                    return matches.front();
                }
                return std::nullopt;
            }

            if (by_name->second.size() == 1) {
                return by_name->second.front();
            }

            return std::nullopt;
        }

        HeaderIncludeGraph build_header_include_graph(
            const std::unordered_map<std::string, HeaderStats>& header_map
        ) {
            HeaderIncludeGraph graph;
            graph.nodes.reserve(header_map.size());

            for (const auto& stats : header_map | std::views::values) {
                if (stats.path.empty() || !fs::exists(stats.path) || !fs::is_regular_file(stats.path)) {
                    continue;
                }
                if (is_external_header(stats.path)) {
                    continue;
                }
                graph.nodes.push_back(stats.path.lexically_normal());
            }

            graph.adjacency.resize(graph.nodes.size());

            std::unordered_map<std::string, std::size_t> key_to_index;
            key_to_index.reserve(graph.nodes.size());
            std::unordered_map<std::string, std::vector<std::size_t>> filename_to_indices;
            filename_to_indices.reserve(graph.nodes.size());

            for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
                key_to_index[path_key(graph.nodes[i])] = i;
                filename_to_indices[graph.nodes[i].filename().string()].push_back(i);
            }

            for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
                const auto directives = parse_include_directives(graph.nodes[i]);
                for (const auto& directive : directives) {
                    const auto target = resolve_include_target(
                        directive,
                        graph.nodes[i],
                        key_to_index,
                        filename_to_indices,
                        graph.nodes
                    );
                    if (!target.has_value()) {
                        continue;
                    }
                    graph.adjacency[i].insert(*target);
                }
            }

            return graph;
        }

        std::vector<std::pair<fs::path, fs::path>> detect_circular_header_dependencies(
            const std::unordered_map<std::string, HeaderStats>& header_map
        ) {
            const HeaderIncludeGraph graph = build_header_include_graph(header_map);
            const std::size_t n = graph.nodes.size();
            if (n == 0) {
                return {};
            }

            std::vector<int> index(n, -1);
            std::vector<int> lowlink(n, 0);
            std::vector<bool> on_stack(n, false);
            std::vector<std::size_t> stack;
            stack.reserve(n);
            int next_index = 0;
            std::vector<std::vector<std::size_t>> sccs;

            std::function<void(std::size_t)> strongconnect;
            strongconnect = [&](const std::size_t v) {
                index[v] = next_index;
                lowlink[v] = next_index;
                ++next_index;
                stack.push_back(v);
                on_stack[v] = true;

                for (const auto w : graph.adjacency[v]) {
                    if (index[w] == -1) {
                        strongconnect(w);
                        lowlink[v] = std::min(lowlink[v], lowlink[w]);
                    } else if (on_stack[w]) {
                        lowlink[v] = std::min(lowlink[v], index[w]);
                    }
                }

                if (lowlink[v] == index[v]) {
                    std::vector<std::size_t> component;
                    while (!stack.empty()) {
                        const std::size_t w = stack.back();
                        stack.pop_back();
                        on_stack[w] = false;
                        component.push_back(w);
                        if (w == v) {
                            break;
                        }
                    }
                    sccs.push_back(std::move(component));
                }
            };

            for (std::size_t v = 0; v < n; ++v) {
                if (index[v] == -1) {
                    strongconnect(v);
                }
            }

            std::vector<std::pair<fs::path, fs::path>> cycles;
            std::unordered_set<std::string> emitted;

            for (auto& scc : sccs) {
                if (scc.empty()) {
                    continue;
                }

                if (scc.size() == 1) {
                    const auto only = scc.front();
                    if (!graph.adjacency[only].contains(only)) {
                        continue;
                    }
                    const std::string key = path_key(graph.nodes[only]) + "->" + path_key(graph.nodes[only]);
                    if (emitted.insert(key).second) {
                        cycles.emplace_back(graph.nodes[only], graph.nodes[only]);
                    }
                    continue;
                }

                std::ranges::sort(scc);
                std::unordered_set<std::size_t> members(scc.begin(), scc.end());
                std::optional<std::pair<std::size_t, std::size_t>> chosen_edge;

                for (const auto u : scc) {
                    std::vector<std::size_t> neighbors;
                    neighbors.reserve(graph.adjacency[u].size());
                    for (const auto v : graph.adjacency[u]) {
                        if (members.contains(v)) {
                            neighbors.push_back(v);
                        }
                    }
                    std::ranges::sort(neighbors);
                    if (!neighbors.empty()) {
                        chosen_edge = std::pair{u, neighbors.front()};
                        break;
                    }
                }

                if (!chosen_edge.has_value()) {
                    chosen_edge = std::pair{scc[0], scc[1]};
                }

                const auto& from = graph.nodes[chosen_edge->first];
                const auto& to = graph.nodes[chosen_edge->second];
                const std::string key = path_key(from) + "->" + path_key(to);
                if (emitted.insert(key).second) {
                    cycles.emplace_back(from, to);
                }
            }

            std::ranges::sort(cycles, [](const auto& lhs, const auto& rhs) {
                const std::string lhs_key = path_key(lhs.first) + "->" + path_key(lhs.second);
                const std::string rhs_key = path_key(rhs.first) + "->" + path_key(rhs.second);
                return lhs_key < rhs_key;
            });

            return cycles;
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

        result.dependencies.circular_dependencies = detect_circular_header_dependencies(header_map);

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
