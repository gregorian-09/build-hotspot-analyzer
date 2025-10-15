//
// Created by gregorian on 15/10/2025.
//

#include "bha/core/config.h"

#include <algorithm>

#include "bha/utils/file_utils.h"
#include "bha/utils/string_utils.h"
#include "bha/utils/path_utils.h"
#include <toml++/toml.h>
#include <sstream>
#include <chrono>

namespace bha::core
{
    Result<Config> Config::load_from_file(const std::string& path) {
        const auto content = utils::read_file(path);
        if (!content) {
            return Result<Config>::failure(ErrorCode::FILE_NOT_FOUND,
                                           "Configuration file not found: " + path);
        }

        return load_from_string(*content);
    }

    Result<Config> Config::load_from_string(const std::string& content) {
        try {
            auto tbl = toml::parse(content);
            Config config;

            if (tbl["general"]) {
                auto& general = *tbl["general"].as_table();
                if (general["project_name"]) config.project_name = general["project_name"].value_or("");
                if (general["build_system"]) config.build_system = general["build_system"].value_or("cmake");
            }

            if (tbl["analysis"]) {
                auto& analysis = *tbl["analysis"].as_table();
                if (analysis["hotspot_threshold_ms"])
                    config.analysis.hotspot_threshold_ms = analysis["hotspot_threshold_ms"].value_or(1000.0);
                if (analysis["top_n_hotspots"])
                    config.analysis.top_n_hotspots = analysis["top_n_hotspots"].value_or(20);
                if (analysis["enable_template_analysis"])
                    config.analysis.enable_template_analysis = analysis["enable_template_analysis"].value_or(true);
                if (analysis["enable_symbol_usage_analysis"])
                    config.analysis.enable_symbol_usage_analysis = analysis["enable_symbol_usage_analysis"].value_or(false);

                if (analysis["metrics"] && analysis["metrics"].is_array()) {
                    config.analysis.metrics.clear();
                    for (auto& metric : *analysis["metrics"].as_array()) {
                        config.analysis.metrics.emplace_back(metric.value_or(""));
                    }
                }
            }

            if (tbl["filters"]) {
                auto& filters = *tbl["filters"].as_table();
                if (filters["ignore_system_headers"])
                    config.filters.ignore_system_headers = filters["ignore_system_headers"].value_or(true);
                if (filters["min_compile_time_ms"])
                    config.filters.min_compile_time_ms = filters["min_compile_time_ms"].value_or(10.0);

                if (filters["ignore_paths"] && filters["ignore_paths"].is_array()) {
                    config.filters.ignore_paths.clear();
                    for (auto& path : *filters["ignore_paths"].as_array()) {
                        config.filters.ignore_paths.emplace_back(path.value_or(""));
                    }
                }
            }

            if (tbl["suggestions"]) {
                auto& suggestions = *tbl["suggestions"].as_table();
                if (suggestions["enabled"])
                    config.suggestions.enabled = suggestions["enabled"].value_or(true);
                if (suggestions["min_confidence"])
                    config.suggestions.min_confidence = suggestions["min_confidence"].value_or(0.5);

                if (suggestions["types"] && suggestions["types"].is_array()) {
                    config.suggestions.types.clear();
                    for (auto& type : *suggestions["types"].as_array()) {
                        config.suggestions.types.emplace_back(type.value_or(""));
                    }
                }

                if (suggestions["exclude_from_suggestions"] && suggestions["exclude_from_suggestions"].is_array()) {
                    config.suggestions.exclude_from_suggestions.clear();
                    for (auto& path : *suggestions["exclude_from_suggestions"].as_array()) {
                        config.suggestions.exclude_from_suggestions.emplace_back(path.value_or(""));
                    }
                }
            }

            if (tbl["visualization"]) {
                auto& viz = *tbl["visualization"].as_table();
                if (viz["enabled"])
                    config.visualization.enabled = viz["enabled"].value_or(true);
                if (viz["max_nodes"])
                    config.visualization.max_nodes = viz["max_nodes"].value_or(10000);
                if (viz["graph_layout"]) {
                    std::string layout = viz["graph_layout"].value_or("force_directed");
                    config.visualization.graph_layout = graph_layout_from_string(layout);
                }
                if (viz["color_scheme"]) {
                    std::string scheme = viz["color_scheme"].value_or("heatmap");
                    config.visualization.color_scheme = color_scheme_from_string(scheme);
                }
            }

            if (tbl["output"]) {
                auto& output = *tbl["output"].as_table();
                if (output["format"]) {
                    std::string fmt = output["format"].value_or("html");
                    config.output.format = output_format_from_string(fmt);
                }
                if (output["output_dir"])
                    config.output.output_dir = output["output_dir"].value_or("./bha-reports");
                if (output["report_name_template"])
                    config.output.report_name_template = output["report_name_template"].value_or("build-report-{timestamp}.{format}");
                if (output["include_code_snippets"])
                    config.output.include_code_snippets = output["include_code_snippets"].value_or(true);
            }

            if (tbl["ci"]) {
                auto& ci = *tbl["ci"].as_table();
                if (ci["enabled"])
                    config.ci.enabled = ci["enabled"].value_or(false);
                if (ci["regression_threshold_percent"])
                    config.ci.regression_threshold_percent = ci["regression_threshold_percent"].value_or(10.0);
                if (ci["fail_on_regression"])
                    config.ci.fail_on_regression = ci["fail_on_regression"].value_or(true);
                if (ci["baseline_file"])
                    config.ci.baseline_file = ci["baseline_file"].value_or("");
                if (ci["post_comment"])
                    config.ci.post_comment = ci["post_comment"].value_or(true);
            }

            if (tbl["storage"]) {
                auto& storage = *tbl["storage"].as_table();
                if (storage["backend"]) {
                    std::string backend = storage["backend"].value_or("memory");
                    config.storage.backend = storage_backend_from_string(backend);
                }
                if (storage["sqlite_path"])
                    config.storage.sqlite_path = storage["sqlite_path"].value_or("./bha-history.db");
                if (storage["postgresql_url"])
                    config.storage.postgresql_url = storage["postgresql_url"].value_or("");
                if (storage["retention_days"])
                    config.storage.retention_days = storage["retention_days"].value_or(90);
            }

            if (tbl["performance"]) {
                auto& perf = *tbl["performance"].as_table();
                if (perf["num_threads"])
                    config.performance.num_threads = perf["num_threads"].value_or(0);
                if (perf["memory_limit_mb"])
                    config.performance.memory_limit_mb = perf["memory_limit_mb"].value_or(8192);
                if (perf["streaming_mode"])
                    config.performance.streaming_mode = perf["streaming_mode"].value_or(false);
                if (perf["cache_size"])
                    config.performance.cache_size = perf["cache_size"].value_or(10);
            }

            if (tbl["advanced"]) {
                auto& adv = *tbl["advanced"].as_table();
                if (adv["auto_detect_compiler"])
                    config.advanced.auto_detect_compiler = adv["auto_detect_compiler"].value_or(true);
                if (adv["use_wrapper"])
                    config.advanced.use_wrapper = adv["use_wrapper"].value_or(true);
                if (adv["plugin_dir"])
                    config.advanced.plugin_dir = adv["plugin_dir"].value_or("./bha-plugins");
                if (adv["debug_mode"])
                    config.advanced.debug_mode = adv["debug_mode"].value_or(false);
                if (adv["dump_intermediate_data"])
                    config.advanced.dump_intermediate_data = adv["dump_intermediate_data"].value_or(false);
            }

            if (tbl["logging"]) {
                auto& log = *tbl["logging"].as_table();
                if (log["level"])
                    config.logging.level = log["level"].value_or("INFO");
                if (log["file"])
                    config.logging.file = log["file"].value_or("bha.log");
                if (log["console"])
                    config.logging.console = log["console"].value_or(true);
                if (log["format"])
                    config.logging.format = log["format"].value_or("[{timestamp}] [{level}] [{source}] {message}");
            }

            if (auto validation_result = config.validate(); !validation_result.is_success()) {
                return Result<Config>::failure(validation_result.error());
            }

            return Result<Config>::success(std::move(config));

        } catch (const toml::parse_error& err) {
            return Result<Config>::failure(ErrorCode::PARSE_ERROR,
                                           "Failed to parse TOML configuration: " + std::string(err.what()));
        }
    }

    Config Config::default_config() {
        return Config{};
    }

    Result<void> Config::save_to_file(const std::string& path) const {
        const std::string content = to_string();

        if (!utils::write_file(path, content)) {
            return Result<void>::failure(ErrorCode::FILE_WRITE_ERROR,
                                         "Failed to write configuration to file: " + path);
        }

        return Result<void>::success();
    }

    std::string Config::to_string() const {
        std::ostringstream ss;

        ss << "[general]\n";
        ss << "project_name = \"" << project_name << "\"\n";
        ss << "build_system = \"" << build_system << "\"\n\n";

        ss << "[analysis]\n";
        ss << "hotspot_threshold_ms = " << analysis.hotspot_threshold_ms << "\n";
        ss << "top_n_hotspots = " << analysis.top_n_hotspots << "\n";
        ss << "enable_template_analysis = " << (analysis.enable_template_analysis ? "true" : "false") << "\n";
        ss << "enable_symbol_usage_analysis = " << (analysis.enable_symbol_usage_analysis ? "true" : "false") << "\n";
        ss << "metrics = [";
        for (size_t i = 0; i < analysis.metrics.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "\"" << analysis.metrics[i] << "\"";
        }
        ss << "]\n\n";

        ss << "[filters]\n";
        ss << "ignore_system_headers = " << (filters.ignore_system_headers ? "true" : "false") << "\n";
        ss << "min_compile_time_ms = " << filters.min_compile_time_ms << "\n";
        ss << "ignore_paths = [";
        for (size_t i = 0; i < filters.ignore_paths.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "\"" << filters.ignore_paths[i] << "\"";
        }
        ss << "]\n\n";

        ss << "[suggestions]\n";
        ss << "enabled = " << (suggestions.enabled ? "true" : "false") << "\n";
        ss << "min_confidence = " << suggestions.min_confidence << "\n\n";

        ss << "[output]\n";
        ss << "format = \"" << bha::core::to_string(output.format) << "\"\n";
        ss << "output_dir = \"" << output.output_dir << "\"\n\n";

        return ss.str();
    }

    Result<void> Config::validate() const {
        std::vector<std::string> errors;

        if (analysis.hotspot_threshold_ms < 0) {
            errors.emplace_back("hotspot_threshold_ms must be non-negative");
        }

        if (analysis.top_n_hotspots <= 0) {
            errors.emplace_back("top_n_hotspots must be positive");
        }

        if (suggestions.min_confidence < 0.0 || suggestions.min_confidence > 1.0) {
            errors.emplace_back("min_confidence must be between 0.0 and 1.0");
        }

        if (performance.num_threads < 0) {
            errors.emplace_back("num_threads must be non-negative");
        }

        if (storage.backend == StorageBackend::POSTGRESQL && storage.postgresql_url.empty()) {
            errors.emplace_back("postgresql_url required when backend is postgresql");
        }

        if (ci.regression_threshold_percent < 0) {
            errors.emplace_back("regression_threshold_percent must be non-negative");
        }

        if (!errors.empty()) {
            return Result<void>::failure(ErrorCode::INVALID_CONFIG,
                                         "Configuration validation failed:\n  " + utils::join(errors, "\n  "));
        }

        return Result<void>::success();
    }

    void Config::merge_with(const Config& other) {
        if (!other.project_name.empty()) project_name = other.project_name;
        if (!other.build_system.empty()) build_system = other.build_system;
    }

    bool Config::is_path_ignored(const std::string& path) const {
        return std::ranges::any_of(filters.ignore_paths,
        [&](const std::string& pattern) {
            return utils::contains(path, pattern);
        });
    }

    bool Config::should_analyze_file(const std::string& path, const double compile_time_ms) const {
        if (is_path_ignored(path)) {
            return false;
        }

        if (compile_time_ms < filters.min_compile_time_ms) {
            return false;
        }

        return true;
    }

    std::string to_string(const OutputFormat format) {
        switch (format) {
            case OutputFormat::TEXT: return "text";
            case OutputFormat::JSON: return "json";
            case OutputFormat::CSV: return "csv";
            case OutputFormat::MARKDOWN: return "markdown";
            case OutputFormat::HTML: return "html";
            default: return "unknown";
        }
    }

    std::string to_string(const GraphLayout layout) {
        switch (layout) {
            case GraphLayout::FORCE_DIRECTED: return "force_directed";
            case GraphLayout::HIERARCHICAL: return "hierarchical";
            case GraphLayout::CIRCULAR: return "circular";
            default: return "unknown";
        }
    }

    std::string to_string(const ColorScheme scheme) {
        switch (scheme) {
            case ColorScheme::HEATMAP: return "heatmap";
            case ColorScheme::CATEGORICAL: return "categorical";
            case ColorScheme::MONOCHROME: return "monochrome";
            default: return "unknown";
        }
    }

    std::string to_string(const StorageBackend backend) {
        switch (backend) {
            case StorageBackend::MEMORY: return "memory";
            case StorageBackend::SQLITE: return "sqlite";
            case StorageBackend::POSTGRESQL: return "postgresql";
            default: return "unknown";
        }
    }

    OutputFormat output_format_from_string(const std::string& str) {
        if (str == "text") return OutputFormat::TEXT;
        if (str == "json") return OutputFormat::JSON;
        if (str == "csv") return OutputFormat::CSV;
        if (str == "markdown") return OutputFormat::MARKDOWN;
        if (str == "html") return OutputFormat::HTML;
        return OutputFormat::TEXT;
    }

    GraphLayout graph_layout_from_string(const std::string& str) {
        if (str == "force_directed") return GraphLayout::FORCE_DIRECTED;
        if (str == "hierarchical") return GraphLayout::HIERARCHICAL;
        if (str == "circular") return GraphLayout::CIRCULAR;
        return GraphLayout::FORCE_DIRECTED;
    }

    ColorScheme color_scheme_from_string(const std::string& str) {
        if (str == "heatmap") return ColorScheme::HEATMAP;
        if (str == "categorical") return ColorScheme::CATEGORICAL;
        if (str == "monochrome") return ColorScheme::MONOCHROME;
        return ColorScheme::HEATMAP;
    }

    StorageBackend storage_backend_from_string(const std::string& str) {
        if (str == "memory") return StorageBackend::MEMORY;
        if (str == "sqlite") return StorageBackend::SQLITE;
        if (str == "postgresql") return StorageBackend::POSTGRESQL;
        return StorageBackend::MEMORY;
    }

} // namespace bha::core