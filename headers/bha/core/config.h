//
// Created by gregorian on 15/10/2025.
//

#ifndef CONFIG_H
#define CONFIG_H

#include "bha/core/result.h"
#include <string>
#include <vector>
#include <unordered_set>

namespace bha::core {

    enum class OutputFormat {
        TEXT,
        JSON,
        CSV,
        MARKDOWN,
        HTML
    };

    enum class GraphLayout {
        FORCE_DIRECTED,
        HIERARCHICAL,
        CIRCULAR
    };

    enum class ColorScheme {
        HEATMAP,
        CATEGORICAL,
        MONOCHROME
    };

    enum class StorageBackend {
        MEMORY,
        SQLITE,
        POSTGRESQL
    };

    struct AnalysisConfig {
        double hotspot_threshold_ms = 1000.0;
        int top_n_hotspots = 20;
        std::vector<std::string> metrics = {"absolute_time", "impact_score", "critical_path"};
        bool enable_template_analysis = true;
        bool enable_symbol_usage_analysis = false;
    };

    struct FilterConfig {
        std::vector<std::string> ignore_paths;
        bool ignore_system_headers = true;
        double min_compile_time_ms = 10.0;
    };

    struct SuggestionConfig {
        bool enabled = true;
        double min_confidence = 0.5;
        std::vector<std::string> types = {
            "forward_declaration",
            "header_split",
            "pch_optimization"
        };
        std::vector<std::string> exclude_from_suggestions;
    };

    struct VisualizationConfig {
        bool enabled = true;
        GraphLayout graph_layout = GraphLayout::FORCE_DIRECTED;
        int max_nodes = 10000;
        ColorScheme color_scheme = ColorScheme::HEATMAP;
    };

    struct OutputConfig {
        OutputFormat format = OutputFormat::HTML;
        std::string output_dir = "./bha-reports";
        std::string report_name_template = "build-report-{timestamp}.{format}";
        bool include_code_snippets = true;
    };

    struct CIConfig {
        bool enabled = false;
        double regression_threshold_percent = 10.0;
        bool fail_on_regression = true;
        std::string baseline_file;
        bool post_comment = true;
    };

    struct StorageConfig {
        StorageBackend backend = StorageBackend::MEMORY;
        std::string sqlite_path = "./bha-history.db";
        std::string postgresql_url;
        int retention_days = 90;
    };

    struct PerformanceConfig {
        int num_threads = 0;
        size_t memory_limit_mb = 8192;
        bool streaming_mode = false;
        int cache_size = 10;
    };

    struct AdvancedConfig {
        bool auto_detect_compiler = true;
        bool use_wrapper = true;
        std::string plugin_dir = "./bha-plugins";
        bool debug_mode = false;
        bool dump_intermediate_data = false;
    };

    struct LoggingConfig {
        std::string level = "INFO";
        std::string file = "bha.log";
        bool console = true;
        std::string format = "[{timestamp}] [{level}] [{source}] {message}";
    };

    class Config {
    public:
        Config() = default;

        std::string project_name;
        std::string build_system = "cmake";

        AnalysisConfig analysis;
        FilterConfig filters;
        SuggestionConfig suggestions;
        VisualizationConfig visualization;
        OutputConfig output;
        CIConfig ci;
        StorageConfig storage;
        PerformanceConfig performance;
        AdvancedConfig advanced;
        LoggingConfig logging;

        /**
         * Load configuration from a file.
         *
         * @param path Filesystem path to the config file (JSON, TOML, YAML, etc.).
         * @return Result containing a Config if successful, or an Error on failure.
         */
        static Result<Config> load_from_file(const std::string& path);

        /**
         * Load configuration from a string content.
         *
         * @param content Text content of a configuration file.
         * @return Result containing a Config if parsing succeeds, or an Error on failure.
         */
        static Result<Config> load_from_string(const std::string& content);

        /**
         * Produce a default configuration instance with sane defaults.
         *
         * @return A Config populated with default values.
         */
        static Config default_config();

        /**
         * Save this configuration to a file on disk.
         *
         * @param path Filesystem path where the config should be written.
         * @return Result<void> — success or Error if writing fails.
         */
        [[nodiscard]] Result<void> save_to_file(const std::string& path) const;

        /**
         * Serialize the configuration to a text representation.
         *
         * @return String representing this configuration (e.g. JSON, TOML, etc.).
         */
        [[nodiscard]] std::string to_string() const;

        /**
         * Validate that this configuration is internally consistent.
         *
         * Checks for invalid values, conflicting options, or missing required fields.
         *
         * @return Result<void> — success if valid, or Error describing the validation issue.
         */
        [[nodiscard]] Result<void> validate() const;

        /**
         * Merge another configuration into this one.
         *
         * Existing settings in this instance are preserved unless `other` overrides them.
         *
         * @param other The other Config whose non-default values override those here.
         */
        void merge_with(const Config& other);

        /**
         * Determine whether `path` should be ignored according to filter rules.
         *
         * @param path Filesystem path to check.
         * @return True if the path matches ignore rules; false otherwise.
         */
        [[nodiscard]] bool is_path_ignored(const std::string& path) const;

        /**
         * Determine whether a file should be analyzed.
         *
         * Uses filter rules, compile time thresholds, and other config parameters.
         *
         * @param path The file path.
         * @param compile_time_ms Time taken to compile the file, in milliseconds.
         * @return True if the file meets criteria for analysis; false if it should be skipped.
         */
        [[nodiscard]] bool should_analyze_file(const std::string& path, double compile_time_ms) const;
    };


    std::string to_string(OutputFormat format);
    std::string to_string(GraphLayout layout);
    std::string to_string(ColorScheme scheme);
    std::string to_string(StorageBackend backend);

    OutputFormat output_format_from_string(const std::string& str);
    GraphLayout graph_layout_from_string(const std::string& str);
    ColorScheme color_scheme_from_string(const std::string& str);
    StorageBackend storage_backend_from_string(const std::string& str);

}

#endif //CONFIG_H
