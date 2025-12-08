//
// Created by gregorian on 15/10/2025.
//

#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>

namespace bha::core {

    using timestamp = std::chrono::system_clock::time_point;

    struct TemplateInstantiation {
        std::string template_name;
        std::string instantiation_context;
        double time_ms = 0.0;
        int instantiation_depth = 0.0;
        std::vector<std::string> call_stack;
    };

    struct CompilationUnit {
        std::string id;
        std::string file_path;
        std::string configuration;

        double total_time_ms{};
        double preprocessing_time_ms{};
        double parsing_time_ms{};
        double codegen_time_ms{};
        double optimization_time_ms{};

        std::string compiler_type;
        std::string compiler_version;
        std::vector<std::string> compile_flags;

        std::vector<std::string> direct_includes;
        std::vector<std::string> all_includes;

        std::vector<TemplateInstantiation> template_instantiations;

        timestamp build_timestamp;
        std::string commit_sha;
        size_t file_size_bytes{};
        size_t preprocessed_size_bytes{};

        CompilationUnit() = default;
    };

    enum class EdgeType {
        DIRECT_INCLUDE,
        TRANSITIVE,
        PCH_REFERENCE
    };

    struct DependencyEdge {
        std::string target;
        EdgeType type{};
        int line_number{};
        bool is_system_header{};
        double weight{};

        DependencyEdge() = default;
        explicit DependencyEdge(std::string target, const EdgeType type = EdgeType::DIRECT_INCLUDE)
            : target(std::move(target)), type(type) {}
    };

    class DependencyGraph {
    public:
        DependencyGraph() = default;

        void add_node(const std::string& file);
        void add_edge(const std::string& source, const std::string& target,
                      EdgeType type = EdgeType::DIRECT_INCLUDE);
        void add_edge(const std::string& source, const DependencyEdge& edge);

        bool has_node(const std::string& file) const;
        bool has_edge(const std::string& source, const std::string& target) const;

        std::vector<std::string> get_dependencies(const std::string& file) const;
        std::vector<std::string> get_reverse_dependencies(const std::string& file) const;
        std::vector<DependencyEdge> get_edges(const std::string& file) const;

        size_t node_count() const;
        size_t edge_count() const;

        std::vector<std::string> get_all_nodes() const;

        const std::unordered_map<std::string, std::vector<DependencyEdge>>& get_adjacency_list() const {
            return adjacency_list_;
        }

        void clear();

    private:
        std::unordered_map<std::string, std::vector<DependencyEdge>> adjacency_list_{};
        std::unordered_map<std::string, std::vector<std::string>> reverse_adjacency_list_{};
    };

    struct PCHMetrics {
        std::string pch_file;
        double pch_build_time_ms;
        double average_time_saved_per_file_ms;
        int files_using_pch;
        double total_time_saved_ms;
        double pch_hit_rate;
    };

    struct TemplateHotspot {
        std::string template_name;
        std::string instantiation_context;
        double time_ms;
        int instantiation_count;
        std::vector<std::string> instantiation_stack;
    };

    struct Hotspot {
        std::string file_path;
        double time_ms;
        double impact_score;
        int num_dependent_files;
        std::string category;
    };

    struct MetricsSummary {
        int total_files_compiled{};
        int total_headers_parsed{};
        double average_file_time_ms{};
        double median_file_time_ms{};
        double p95_file_time_ms{};
        double p99_file_time_ms{};

        std::vector<Hotspot> top_slow_files;
        std::vector<Hotspot> top_hot_headers;
        std::vector<Hotspot> critical_path;

        int total_dependencies{};
        int average_include_depth{};
        int max_include_depth{};
        int circular_dependency_count{};

        std::vector<TemplateHotspot> expensive_templates;

        std::optional<PCHMetrics> pch_metrics;
    };

    struct BuildTrace {
        std::string trace_id;
        timestamp build_start;
        timestamp build_end;
        double total_build_time_ms{};

        std::string build_system;
        std::string build_system_version;
        std::string configuration;
        std::string platform;

        std::vector<CompilationUnit> compilation_units;
        DependencyGraph dependency_graph;

        std::unordered_map<std::string, std::vector<std::string>> targets;
        std::vector<std::string> build_order;

        MetricsSummary metrics;

        std::string commit_sha;
        std::string branch;
        bool is_clean_build;
        std::vector<std::string> changed_files;

        BuildTrace() : is_clean_build(true) {}
    };

    enum class SuggestionType {
        FORWARD_DECLARATION,
        HEADER_SPLIT,
        PIMPL_PATTERN,
        PCH_ADDITION,
        PCH_REMOVAL,
        INCLUDE_REMOVAL,
        MOVE_TO_CPP,
        EXPLICIT_TEMPLATE_INSTANTIATION
    };

    enum class Priority {
        CRITICAL,
        HIGH,
        MEDIUM,
        LOW
    };

    enum class ChangeType {
        ADD,
        REMOVE,
        REPLACE
    };

    struct CodeChange {
        std::string file_path;
        int line_number;
        std::string before;
        std::string after;
        ChangeType type;
    };

    struct Suggestion {
        std::string id;
        SuggestionType type;
        Priority priority;
        double confidence;

        std::string title;
        std::string description;
        std::string file_path;
        std::vector<std::string> related_files;

        double estimated_time_savings_ms;
        double estimated_time_savings_percent;
        std::vector<std::string> affected_files;

        std::vector<CodeChange> suggested_changes;
        std::string rationale;
        std::vector<std::string> caveats;

        bool is_safe;
        std::string documentation_link;
    };

    struct ImpactReport {
        std::vector<std::string> affected_files;
        double estimated_rebuild_time_ms;
        int num_cascading_rebuilds;
        std::vector<std::string> fragile_headers;
    };

    struct ComparisonReport {
        std::string baseline_trace_id;
        std::string current_trace_id;

        double baseline_total_time_ms;
        double current_total_time_ms;
        double time_delta_ms;
        double time_delta_percent;

        std::vector<Hotspot> new_hotspots;
        std::vector<Hotspot> resolved_hotspots;
        std::vector<Hotspot> regressed_files;

        std::vector<std::string> new_dependencies;
        std::vector<std::string> removed_dependencies;

        bool is_regression;
    };

    std::string to_string(EdgeType type);
    std::string to_string(SuggestionType type);
    std::string to_string(Priority priority);
    std::string to_string(ChangeType type);

    EdgeType edge_type_from_string(const std::string& str);
    SuggestionType suggestion_type_from_string(const std::string& str);
    Priority priority_from_string(const std::string& str);
    ChangeType change_type_from_string(const std::string& str);

}

#endif //TYPES_H
