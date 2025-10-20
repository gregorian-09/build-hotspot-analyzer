//
// Created by gregorian on 20/10/2025.
//

#include "bha/suggestions/header_splitter.h"
#include "bha/utils/file_utils.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <regex>
#include <fstream>
#include <random>
#include <ranges>

namespace bha::suggestions {

    HeaderSplitter::HeaderSplitter(const core::DependencyGraph& graph)
        : graph_(graph) {}

    core::Result<HeaderSplitSuggestion> HeaderSplitter::suggest_split(
        const std::string& header_file,
        const std::vector<std::string>& dependent_files,
        size_t min_cluster_size
    )
    {
        auto symbol_cache_result = extract_symbol_usage(header_file, dependent_files);
        if (!symbol_cache_result.is_success()) {
            return core::Result<HeaderSplitSuggestion>::failure(symbol_cache_result.error());
        }
        auto& symbol_cache = symbol_cache_result.value();

        if (symbol_cache.all_symbols.size() < min_cluster_size * 2) {
            return core::Result<HeaderSplitSuggestion>::failure(core::Error{
                .code = core::ErrorCode::ANALYSIS_ERROR,
                .message = "Not enough symbols to justify splitting"
            });
        }

        std::vector<std::string> symbols(
            symbol_cache.all_symbols.begin(),
            symbol_cache.all_symbols.end()
        );

        auto co_usage_result = build_co_usage_matrix(symbols, symbol_cache);
        if (!co_usage_result.is_success()) {
            return core::Result<HeaderSplitSuggestion>::failure(co_usage_result.error());
        }
        auto& co_usage = co_usage_result.value();

        auto clustering_result = perform_spectral_clustering(
            co_usage.co_usage_matrix,
            2
        );
        if (!clustering_result.is_success()) {
            return core::Result<HeaderSplitSuggestion>::failure(clustering_result.error());
        }
        auto& clustering = clustering_result.value();

        std::map<int, std::vector<std::string>> clusters;
        for (size_t i = 0; i < clustering.labels.size(); ++i) {
            clusters[clustering.labels[i]].push_back(symbols[i]);
        }

        for (auto& symbol_list : clusters | std::views::values) {
            std::ranges::sort(symbol_list);
        }

        if (clusters.size() < 2) {
            return core::Result<HeaderSplitSuggestion>::failure(core::Error{
                .code = core::ErrorCode::ANALYSIS_ERROR,
                .message = "Clustering did not produce meaningful splits"
            });
        }

        double benefit = calculate_split_benefit(
            clusters,
            symbol_cache
        );

        if (!is_split_worthwhile(benefit, clusters)) {
            return core::Result<HeaderSplitSuggestion>::failure(core::Error{
                .code = core::ErrorCode::ANALYSIS_ERROR,
                .message = "Estimated benefit too small to justify split"
            });
        }

        HeaderSplitSuggestion suggestion;
        suggestion.original_file = header_file;
        suggestion.estimated_benefit_ms = benefit;
        suggestion.confidence = clustering.quality_score;

        for (size_t i = 0; i < clusters.size(); ++i) {
            std::string part_name = header_file;
            if (size_t dot_pos = part_name.rfind('.'); dot_pos != std::string::npos) {
                part_name.insert(dot_pos, "_part" + std::to_string(i + 1));
            } else {
                part_name += "_part" + std::to_string(i + 1);
            }

            suggestion.suggested_splits.emplace_back(
                part_name,
                clusters.at(static_cast<int>(i))
            );
        }

        suggestion.rationale = "Header contains " + std::to_string(symbol_cache.all_symbols.size()) +
                              " symbols used in distinct patterns. Clustering analysis identified " +
                              std::to_string(clusters.size()) + " logically separate groups.";

        return core::Result<HeaderSplitSuggestion>::success(std::move(suggestion));
    }

    core::Result<SymbolCoUsage> HeaderSplitter::build_co_usage_matrix(
        const std::vector<std::string>& symbols,
        const SymbolUsageCache& usage_cache
    ) {
        const size_t n = symbols.size();
        std::vector matrix(n, std::vector(n, 0));

        for (const auto& used_symbols : usage_cache.dependent_to_symbols | std::views::values) {
            for (size_t i = 0; i < symbols.size(); ++i) {
                if (used_symbols.contains(symbols[i])) {
                    for (size_t j = 0; j < symbols.size(); ++j) {
                        if (used_symbols.contains(symbols[j])) {
                            matrix[i][j]++;
                        }
                    }
                }
            }
        }

        return core::Result<SymbolCoUsage>::success(SymbolCoUsage{
            .co_usage_matrix = std::move(matrix),
            .symbols = symbols,
            .num_files_analyzed = static_cast<int>(usage_cache.dependent_to_symbols.size())
        });
    }

    core::Result<ClusteringResult> HeaderSplitter::perform_spectral_clustering(
        const std::vector<std::vector<int>>& co_usage_matrix,
        const int target_clusters
    ) {
        if (co_usage_matrix.empty()) {
            return core::Result<ClusteringResult>::failure(core::Error{
                .code = core::ErrorCode::ANALYSIS_ERROR,
                .message = "Empty co-usage matrix"
            });
        }

        const auto affinity = compute_affinity_matrix(co_usage_matrix);

        auto labels = kmeans_clustering(affinity, target_clusters);

        const int actual_clusters = *std::ranges::max_element(labels) + 1;

        std::vector cluster_sizes(actual_clusters, 0);
        for (const int label : labels) {
            cluster_sizes[label]++;
        }

        double quality = 0.0;
        for (const int size : cluster_sizes) {
            if (size > 0) {
                quality += static_cast<double>(size) / static_cast<double>(labels.size());
            }
        }
        quality /= actual_clusters;

        return core::Result<ClusteringResult>::success(ClusteringResult{
            .labels = std::move(labels),
            .num_clusters = actual_clusters,
            .quality_score = quality
        });
    }

    double HeaderSplitter::calculate_split_benefit(
        const std::map<int, std::vector<std::string>>& clusters,
        const SymbolUsageCache& usage_cache) {
        std::vector<std::string> dependent_files;
        dependent_files.reserve(usage_cache.dependent_to_symbols.size());

        for (const auto& file : usage_cache.dependent_to_symbols | std::views::keys) {
            dependent_files.push_back(file);
        }

        const double include_reduction = estimate_include_reduction(
            clusters,
            dependent_files,
            usage_cache
        );

        return include_reduction * 10.0;
    }

    core::Result<HeaderSplitter::SymbolUsageCache>
    HeaderSplitter::extract_symbol_usage(
        const std::string& header_file,
        const std::vector<std::string>& dependent_files
    ) {
        SymbolUsageCache cache;

        auto symbols = parse_symbols_from_header(header_file);
        cache.all_symbols.insert(symbols.begin(), symbols.end());

        for (const auto& file : dependent_files) {
            auto used = get_used_symbols_for_file(file, header_file);
            cache.dependent_to_symbols[file] = std::move(used);
        }

        if (cache.all_symbols.empty()) {
            return core::Result<SymbolUsageCache>::failure(core::Error{
                .code = core::ErrorCode::PARSE_ERROR,
                .message = "Could not extract symbols from header"
            });
        }

        return core::Result<SymbolUsageCache>::success(std::move(cache));
    }

    std::vector<std::string> HeaderSplitter::parse_symbols_from_header(
        const std::string& header_file
    ) {
        std::vector<std::string> symbols;
        std::ifstream file(header_file);

        if (!file.is_open()) {
            return symbols;
        }

        std::regex class_pattern(R"(\bclass\s+(\w+))");
        std::regex struct_pattern(R"(\bstruct\s+(\w+))");
        std::regex function_pattern(R"(\b(\w+)\s*\([^)]*\)\s*[{;])");
        std::regex var_pattern(R"(\b(extern\s+)?(?:const\s+)?(?:static\s+)?(?:inline\s+)?\w+\s+(\w+)\s*[;=])");

        std::string line;
        while (std::getline(file, line)) {
            std::smatch match;

            if (std::regex_search(line, match, class_pattern)) {
                symbols.push_back(match[1].str());
            }
            if (std::regex_search(line, match, struct_pattern)) {
                symbols.push_back(match[1].str());
            }
        }

        std::ranges::sort(symbols);
        symbols.erase(std::ranges::unique(symbols).begin(), symbols.end());

        return symbols;
    }

    std::set<std::string> HeaderSplitter::get_used_symbols_for_file(
        const std::string& dependent_file,
        const std::string& header_file
    ) {
        std::set<std::string> used_symbols;
        std::ifstream file(dependent_file);

        if (!file.is_open()) {
            return used_symbols;
        }

        const std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

        for (const auto symbols = parse_symbols_from_header(header_file); const auto& symbol : symbols) {
            if (std::regex symbol_usage(R"(\b)" + symbol + R"(\b)"); std::regex_search(content, symbol_usage)) {
                used_symbols.insert(symbol);
            }
        }

        return used_symbols;
    }

    std::vector<std::vector<double>> HeaderSplitter::compute_affinity_matrix(
        const std::vector<std::vector<int>>& co_usage_matrix
    ) {
        size_t n = co_usage_matrix.size();
        std::vector affinity(n, std::vector(n, 0.0));

        int max_concurrence = 0;
        for (const auto& row : co_usage_matrix) {
            for (int val : row) {
                max_concurrence = std::max(max_concurrence, val);
            }
        }

        if (max_concurrence == 0) {
            max_concurrence = 1;
        }

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                const double normalized = static_cast<double>(co_usage_matrix[i][j]) / max_concurrence;
                affinity[i][j] = std::exp(-normalized * normalized / 0.2);
            }
        }

        return affinity;
    }

    std::vector<int> HeaderSplitter::kmeans_clustering(
        const std::vector<std::vector<double>>& affinity,
        const int k,
        const int max_iterations
    ) {
        const size_t n = affinity.size();
        std::vector<int> labels(n);
        std::vector centroids(k, std::vector(n, 0.0));

        std::mt19937 gen(std::random_device{}());

        std::uniform_int_distribution dis(0, static_cast<int>(n) - 1);

        for (int i = 0; i < k; ++i) {
            const int idx = dis(gen);
            centroids[i] = affinity[idx];
        }

        for (int iter = 0; iter < max_iterations; ++iter) {
            for (size_t i = 0; i < n; ++i) {
                double min_dist = std::numeric_limits<double>::max();
                int best_cluster = 0;

                for (int c = 0; c < k; ++c) {
                    double dist = 0.0;
                    for (size_t j = 0; j < n; ++j) {
                        const double diff = affinity[i][j] - centroids[c][j];
                        dist += diff * diff;
                    }

                    if (dist < min_dist) {
                        min_dist = dist;
                        best_cluster = c;
                    }
                }

                labels[i] = best_cluster;
            }

            for (int c = 0; c < k; ++c) {
                std::ranges::fill(centroids[c], 0.0);
                int count = 0;

                for (size_t i = 0; i < n; ++i) {
                    if (labels[i] == c) {
                        for (size_t j = 0; j < n; ++j) {
                            centroids[c][j] += affinity[i][j];
                        }
                        count++;
                    }
                }

                if (count > 0) {
                    for (size_t j = 0; j < n; ++j) {
                        centroids[c][j] /= count;
                    }
                }
            }
        }

        return labels;
    }

    double HeaderSplitter::estimate_include_reduction(
        const std::map<int, std::vector<std::string>>& clusters,
        const std::vector<std::string>& dependent_files,
        const SymbolUsageCache& usage_cache
    ) {
        double total_reduction = 0.0;

        for (const auto& file : dependent_files) {
            auto it = usage_cache.dependent_to_symbols.find(file);
            if (it == usage_cache.dependent_to_symbols.end()) {
                continue;
            }

            const auto& used_symbols = it->second;
            int needed_clusters = 0;

            for (const auto& cluster_symbols : clusters | std::views::values) {
                bool has_needed = false;
                for (const auto& sym : cluster_symbols) {
                    if (used_symbols.contains(sym)) {
                        has_needed = true;
                        break;
                    }
                }
                if (has_needed) {
                    needed_clusters++;
                }
            }

            const double reduction = static_cast<double>(clusters.size() - needed_clusters) /
                              static_cast<double>(clusters.size());
            total_reduction += reduction;
        }

        return total_reduction / static_cast<double>(dependent_files.size());
    }

    bool HeaderSplitter::is_split_worthwhile(
        const double benefit_ms,
        const std::map<int, std::vector<std::string>>& clusters
    ) {
            return benefit_ms >= 10.0
                && clusters.size() >= 2
                && std::ranges::all_of(clusters | std::views::values,
                                       [](const auto& symbols) {
                                           return symbols.size() >= 2;
                                       });
        }

}
