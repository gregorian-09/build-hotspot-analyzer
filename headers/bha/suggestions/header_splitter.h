//
// Created by gregorian on 20/10/2025.
//

#ifndef HEADER_SPLITTER_H
#define HEADER_SPLITTER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <vector>
#include <string>
#include <map>
#include <set>

namespace bha::suggestions {

    /**
     * @struct HeaderSplitSuggestion
     * Represents a suggested way to split a large header file into smaller logical parts.
     *
     * Each suggested split identifies a group of symbols that could be isolated into a new header file
     * to reduce compile-time dependencies and improve build modularity.
     */
    struct HeaderSplitSuggestion {
        std::string original_file; ///< Path to the original header file.
        std::vector<std::pair<std::string, std::vector<std::string>>> suggested_splits; ///< Pairs of new header names and associated symbols.
        double estimated_benefit_ms; ///< Estimated compile time reduction (in milliseconds).
        double confidence; ///< Confidence score between 0.0 and 1.0 indicating suggestion reliability.
        std::string rationale; ///< Explanation of why the split is recommended.
    };

    /**
     * @struct SymbolCoUsage
     * Describes co-usage frequency between symbols across dependent files.
     *
     * This matrix helps in clustering symbols that are frequently used together,
     * forming the basis for header file split decisions.
     */
    struct SymbolCoUsage {
        std::vector<std::vector<int>> co_usage_matrix; ///< Square matrix of symbol co-usage counts.
        std::vector<std::string> symbols; ///< List of analyzed symbol names.
        int num_files_analyzed; ///< Total number of dependent files considered.
    };

    /**
     * @struct ClusteringResult
     * Represents the result of clustering analysis on symbols.
     *
     * Used to determine which symbols should be grouped into the same split header.
     */
    struct ClusteringResult {
        std::vector<int> labels; ///< Cluster label assigned to each symbol (index-based).
        int num_clusters; ///< Total number of clusters generated.
        double quality_score; ///< Metric indicating how distinct clusters are (higher = better separation).
    };

    /**
     * @class HeaderSplitter
     * Analyzes symbol usage to propose optimal header file splits.
     *
     * This class identifies logical partitions within large headers based on
     * symbol co-usage patterns across dependent files. It uses clustering techniques
     * such as spectral analysis and K-means to suggest data-driven header reorganizations.
     */
    class HeaderSplitter {
    public:
        /**
         * @struct SymbolUsageCache
         * Caches which symbols are used by which dependent files.
         *
         * Used to efficiently compute symbol co-usage without redundant parsing.
         */
        struct SymbolUsageCache {
            std::map<std::string, std::set<std::string>> dependent_to_symbols; ///< Mapping from dependent file → used symbols.
            std::set<std::string> all_symbols; ///< All symbols found in the analyzed header.
        };

        /**
         * Constructs a HeaderSplitter with access to the global dependency graph.
         * @param graph Reference to the dependency graph used for analysis.
         */
        explicit HeaderSplitter(const core::DependencyGraph& graph);

        /**
         * Suggests how to split a given header file based on symbol co-usage.
         *
         * @param header_file Path to the header being analyzed.
         * @param dependent_files List of files that include or depend on the header.
         * @param min_cluster_size Minimum number of symbols per cluster to consider a split valid.
         * @return Result containing a HeaderSplitSuggestion, or an error if analysis fails.
         */
        [[nodiscard]] static core::Result<HeaderSplitSuggestion> suggest_split(
            const std::string& header_file,
            const std::vector<std::string>& dependent_files,
            size_t min_cluster_size = 2
        );

        /**
         * Builds a co-usage matrix between symbols.
         *
         * @param symbols List of symbol names to analyze.
         * @param usage_cache Pre-computed cache of symbol usage relationships.
         * @return SymbolCoUsage structure containing the co-usage matrix.
         */
        static core::Result<SymbolCoUsage> build_co_usage_matrix(
            const std::vector<std::string>& symbols,
            const SymbolUsageCache& usage_cache
        );

        /**
         * Performs spectral clustering on a co-usage matrix.
         *
         * @param co_usage_matrix Co-usage matrix between symbols.
         * @param target_clusters Desired number of output clusters.
         * @return ClusteringResult containing labels and cluster quality metrics.
         */
        static core::Result<ClusteringResult> perform_spectral_clustering(
            const std::vector<std::vector<int>>& co_usage_matrix,
            int target_clusters = 2
        );

        /**
         * Estimates the compile-time benefit of splitting a header based on clusters.
         *
         * @param clusters Mapping of cluster index to associated symbols.
         * @param usage_cache Cached symbol usage data.
         * @return Estimated compile-time reduction in milliseconds.
         */
        static double calculate_split_benefit(
            const std::map<int, std::vector<std::string>>& clusters,
            const SymbolUsageCache& usage_cache
        );

        /**
         * Extracts symbol usage relationships for a header.
         *
         * @param header_file Header file to analyze.
         * @param dependent_files Files that depend on the header.
         * @return SymbolUsageCache containing mappings of symbol usages.
         */
        static core::Result<SymbolUsageCache> extract_symbol_usage(
            const std::string& header_file,
            const std::vector<std::string>& dependent_files
        );

    private:
        const core::DependencyGraph& graph_; ///< Reference to the dependency graph.

        /**
         * Parses symbol names from a header file.
         * @param header_file Path to the header file.
         * @return List of detected symbol names.
         */
        static std::vector<std::string> parse_symbols_from_header(
            const std::string& header_file
        );

        /**
         * Retrieves symbols used by a dependent file.
         * @param dependent_file File that includes the header.
         * @param header_file The header being analyzed.
         * @return Set of symbol names used from the header.
         */
        static std::set<std::string> get_used_symbols_for_file(
            const std::string& dependent_file,
            const std::string& header_file
        );

        /**
         * Computes a normalized affinity matrix from the co-usage matrix.
         * @param co_usage_matrix Raw co-usage counts.
         * @return Affinity matrix representing symbol similarity.
         */
        static std::vector<std::vector<double>> compute_affinity_matrix(
            const std::vector<std::vector<int>>& co_usage_matrix
        );

        /**
         * Performs K-means clustering on an affinity matrix.
         * @param affinity Affinity matrix between symbols.
         * @param k Number of clusters to generate.
         * @param max_iterations Maximum number of iterations (default: 100).
         * @return List of cluster labels (index-based).
         */
        static std::vector<int> kmeans_clustering(
            const std::vector<std::vector<double>>& affinity,
            int k,
            int max_iterations = 100
        );

        /**
         * Estimates the reduction in includes after performing the split.
         * @param clusters Clustered symbol groups.
         * @param dependent_files List of dependent files.
         * @param usage_cache Cached symbol usage data.
         * @return Fraction or score representing potential include reduction.
         */
        static double estimate_include_reduction(
            const std::map<int, std::vector<std::string>>& clusters,
            const std::vector<std::string>& dependent_files,
            const SymbolUsageCache& usage_cache
        );

        /**
         * Determines if a proposed split provides sufficient benefit.
         * @param benefit_ms Estimated time savings (ms).
         * @param clusters Cluster mapping used for the split.
         * @return True if the split is worthwhile, false otherwise.
         */
        static bool is_split_worthwhile(
            double benefit_ms,
            const std::map<int, std::vector<std::string>>& clusters
        );
    };

} // namespace bha::suggestions

#endif //HEADER_SPLITTER_H
