//
// Created by gregorian on 08/12/2025.
//

#include <gtest/gtest.h>
#include "bha/core/config.h"
#include <fstream>
#include <filesystem>

using namespace bha::core;
namespace fs = std::filesystem;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "config_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    std::string create_test_file(const std::string& filename, const std::string& content) const {
        const fs::path file_path = temp_dir / filename;
        std::ofstream file(file_path);
        file << content;
        file.close();
        return file_path.string();
    }

    fs::path temp_dir;
};

TEST_F(ConfigTest, DefaultConfig) {
    const auto config = Config::default_config();

    EXPECT_EQ(config.build_system, "cmake");
    EXPECT_EQ(config.analysis.hotspot_threshold_ms, 1000.0);
    EXPECT_EQ(config.analysis.top_n_hotspots, 20);
    EXPECT_TRUE(config.analysis.enable_template_analysis);

    EXPECT_TRUE(config.filters.ignore_system_headers);
    EXPECT_EQ(config.filters.min_compile_time_ms, 10.0);

    EXPECT_TRUE(config.suggestions.enabled);
    EXPECT_EQ(config.suggestions.min_confidence, 0.5);

    EXPECT_TRUE(config.visualization.enabled);
    EXPECT_EQ(config.visualization.graph_layout, GraphLayout::FORCE_DIRECTED);
    EXPECT_EQ(config.visualization.max_nodes, 10000);
    EXPECT_EQ(config.visualization.color_scheme, ColorScheme::HEATMAP);

    EXPECT_EQ(config.output.format, OutputFormat::HTML);
    EXPECT_EQ(config.output.output_dir, "./bha-reports");
    EXPECT_TRUE(config.output.include_code_snippets);

    EXPECT_FALSE(config.ci.enabled);
    EXPECT_EQ(config.ci.regression_threshold_percent, 10.0);

    EXPECT_EQ(config.storage.backend, StorageBackend::MEMORY);
    EXPECT_EQ(config.storage.retention_days, 90);

    EXPECT_EQ(config.performance.num_threads, 0);
    EXPECT_EQ(config.performance.memory_limit_mb, 8192);

    EXPECT_TRUE(config.advanced.auto_detect_compiler);
    EXPECT_FALSE(config.advanced.debug_mode);
}

TEST_F(ConfigTest, OutputFormatToString) {
    EXPECT_EQ(to_string(OutputFormat::TEXT), "TEXT");
    EXPECT_EQ(to_string(OutputFormat::JSON), "JSON");
    EXPECT_EQ(to_string(OutputFormat::CSV), "CSV");
    EXPECT_EQ(to_string(OutputFormat::MARKDOWN), "MARKDOWN");
    EXPECT_EQ(to_string(OutputFormat::HTML), "HTML");
}

TEST_F(ConfigTest, OutputFormatFromString) {
    EXPECT_EQ(output_format_from_string("TEXT"), OutputFormat::TEXT);
    EXPECT_EQ(output_format_from_string("JSON"), OutputFormat::JSON);
    EXPECT_EQ(output_format_from_string("CSV"), OutputFormat::CSV);
    EXPECT_EQ(output_format_from_string("MARKDOWN"), OutputFormat::MARKDOWN);
    EXPECT_EQ(output_format_from_string("HTML"), OutputFormat::HTML);
}

TEST_F(ConfigTest, GraphLayoutToString) {
    EXPECT_EQ(to_string(GraphLayout::FORCE_DIRECTED), "FORCE_DIRECTED");
    EXPECT_EQ(to_string(GraphLayout::HIERARCHICAL), "HIERARCHICAL");
    EXPECT_EQ(to_string(GraphLayout::CIRCULAR), "CIRCULAR");
}

TEST_F(ConfigTest, GraphLayoutFromString) {
    EXPECT_EQ(graph_layout_from_string("FORCE_DIRECTED"), GraphLayout::FORCE_DIRECTED);
    EXPECT_EQ(graph_layout_from_string("HIERARCHICAL"), GraphLayout::HIERARCHICAL);
    EXPECT_EQ(graph_layout_from_string("CIRCULAR"), GraphLayout::CIRCULAR);
}

TEST_F(ConfigTest, ColorSchemeToString) {
    EXPECT_EQ(to_string(ColorScheme::HEATMAP), "HEATMAP");
    EXPECT_EQ(to_string(ColorScheme::CATEGORICAL), "CATEGORICAL");
    EXPECT_EQ(to_string(ColorScheme::MONOCHROME), "MONOCHROME");
}

TEST_F(ConfigTest, ColorSchemeFromString) {
    EXPECT_EQ(color_scheme_from_string("HEATMAP"), ColorScheme::HEATMAP);
    EXPECT_EQ(color_scheme_from_string("CATEGORICAL"), ColorScheme::CATEGORICAL);
    EXPECT_EQ(color_scheme_from_string("MONOCHROME"), ColorScheme::MONOCHROME);
}

TEST_F(ConfigTest, StorageBackendToString) {
    EXPECT_EQ(to_string(StorageBackend::MEMORY), "MEMORY");
    EXPECT_EQ(to_string(StorageBackend::SQLITE), "SQLITE");
    EXPECT_EQ(to_string(StorageBackend::POSTGRESQL), "POSTGRESQL");
}

TEST_F(ConfigTest, StorageBackendFromString) {
    EXPECT_EQ(storage_backend_from_string("MEMORY"), StorageBackend::MEMORY);
    EXPECT_EQ(storage_backend_from_string("SQLITE"), StorageBackend::SQLITE);
    EXPECT_EQ(storage_backend_from_string("POSTGRESQL"), StorageBackend::POSTGRESQL);
}

TEST_F(ConfigTest, ValidateDefaultConfig) {
    const auto config = Config::default_config();
    const auto result = config.validate();
    EXPECT_TRUE(result.is_success());
}

TEST_F(ConfigTest, ValidateInvalidConfig) {
    Config config = Config::default_config();
    config.analysis.hotspot_threshold_ms = -100.0;  // Invalid negative value

    const auto result = config.validate();
    EXPECT_TRUE(result.is_failure());
}

TEST_F(ConfigTest, IsPathIgnored) {
    Config config = Config::default_config();
    config.filters.ignore_paths = {"/usr/include/*", "*/third_party/*", "*.test.cpp"};

    EXPECT_TRUE(config.is_path_ignored("/usr/include/stdio.h"));
    EXPECT_TRUE(config.is_path_ignored("/project/third_party/lib.h"));
    EXPECT_TRUE(config.is_path_ignored("/src/file.test.cpp"));
    EXPECT_FALSE(config.is_path_ignored("/src/main.cpp"));
}

TEST_F(ConfigTest, ShouldAnalyzeFile) {
    Config config = Config::default_config();
    config.filters.min_compile_time_ms = 50.0;
    config.filters.ignore_paths = {"*/test/*"};

    // Should analyze: meets time threshold, not ignored
    EXPECT_TRUE(config.should_analyze_file("/src/main.cpp", 100.0));

    // Should not analyze: below time threshold
    EXPECT_FALSE(config.should_analyze_file("/src/small.cpp", 10.0));

    // Should not analyze: in ignored path
    EXPECT_FALSE(config.should_analyze_file("/src/test/test.cpp", 100.0));
}

TEST_F(ConfigTest, MergeWith) {
    Config config1 = Config::default_config();
    config1.project_name = "Project1";
    config1.analysis.hotspot_threshold_ms = 1000.0;
    config1.output.format = OutputFormat::HTML;

    Config config2;
    config2.project_name = "Project2";
    config2.analysis.hotspot_threshold_ms = 2000.0;
    // output.format not set in config2

    config1.merge_with(config2);

    EXPECT_EQ(config1.project_name, "Project2");  // Overridden
    EXPECT_EQ(config1.analysis.hotspot_threshold_ms, 2000.0);  // Overridden
    EXPECT_EQ(config1.output.format, OutputFormat::HTML);  // Preserved
}

TEST_F(ConfigTest, LoadFromStringJSON) {
    const std::string json_config = R"({
        "project_name": "TestProject",
        "build_system": "ninja",
        "analysis": {
            "hotspot_threshold_ms": 500.0,
            "top_n_hotspots": 10
        }
    })";

    const auto result = Config::load_from_string(json_config);
    if (result.is_success()) {
        const auto& config = result.value();
        EXPECT_EQ(config.project_name, "TestProject");
        EXPECT_EQ(config.build_system, "ninja");
        EXPECT_EQ(config.analysis.hotspot_threshold_ms, 500.0);
        EXPECT_EQ(config.analysis.top_n_hotspots, 10);
    }
}

TEST_F(ConfigTest, LoadFromInvalidString) {
    const std::string invalid_config = "{ invalid json content }";
    const auto result = Config::load_from_string(invalid_config);
    EXPECT_TRUE(result.is_failure());
}

TEST_F(ConfigTest, LoadFromFile) {
    const std::string json_config = R"({
        "project_name": "FileProject",
        "build_system": "make"
    })";

    const std::string config_path = create_test_file("config.json", json_config);
    const auto result = Config::load_from_file(config_path);

    if (result.is_success()) {
        const auto& config = result.value();
        EXPECT_EQ(config.project_name, "FileProject");
        EXPECT_EQ(config.build_system, "make");
    }
}

TEST_F(ConfigTest, LoadFromNonExistentFile) {
    const auto result = Config::load_from_file("/nonexistent/config.json");
    EXPECT_TRUE(result.is_failure());
}

TEST_F(ConfigTest, SaveToFile) {
    Config config = Config::default_config();
    config.project_name = "SaveTest";
    config.build_system = "cmake";

    const std::string save_path = (temp_dir / "saved_config.json").string();
    const auto save_result = config.save_to_file(save_path);

    if (save_result.is_success()) {
        EXPECT_TRUE(fs::exists(save_path));

        // Try to load it back
        const auto load_result = Config::load_from_file(save_path);
        if (load_result.is_success()) {
            const auto& loaded = load_result.value();
            EXPECT_EQ(loaded.project_name, "SaveTest");
            EXPECT_EQ(loaded.build_system, "cmake");
        }
    }
}

TEST_F(ConfigTest, ToString) {
    Config config = Config::default_config();
    config.project_name = "ToStringTest";

    const std::string str = config.to_string();
    EXPECT_FALSE(str.empty());
    EXPECT_TRUE(str.find("ToStringTest") != std::string::npos);
}

TEST_F(ConfigTest, AnalysisConfigCustomization) {
    Config config = Config::default_config();
    config.analysis.hotspot_threshold_ms = 2000.0;
    config.analysis.top_n_hotspots = 50;
    config.analysis.enable_template_analysis = false;
    config.analysis.enable_symbol_usage_analysis = true;
    config.analysis.metrics = {"absolute_time", "impact_score"};

    EXPECT_EQ(config.analysis.hotspot_threshold_ms, 2000.0);
    EXPECT_EQ(config.analysis.top_n_hotspots, 50);
    EXPECT_FALSE(config.analysis.enable_template_analysis);
    EXPECT_TRUE(config.analysis.enable_symbol_usage_analysis);
    ASSERT_EQ(config.analysis.metrics.size(), 2);
}

TEST_F(ConfigTest, FilterConfigCustomization) {
    Config config = Config::default_config();
    config.filters.ignore_paths = {"/usr/*", "*/build/*"};
    config.filters.ignore_system_headers = false;
    config.filters.min_compile_time_ms = 100.0;

    ASSERT_EQ(config.filters.ignore_paths.size(), 2);
    EXPECT_FALSE(config.filters.ignore_system_headers);
    EXPECT_EQ(config.filters.min_compile_time_ms, 100.0);
}

TEST_F(ConfigTest, SuggestionConfigCustomization) {
    Config config = Config::default_config();
    config.suggestions.enabled = false;
    config.suggestions.min_confidence = 0.8;
    config.suggestions.types = {"forward_declaration", "pch_optimization"};
    config.suggestions.exclude_from_suggestions = {"legacy.h", "generated.h"};

    EXPECT_FALSE(config.suggestions.enabled);
    EXPECT_EQ(config.suggestions.min_confidence, 0.8);
    ASSERT_EQ(config.suggestions.types.size(), 2);
    ASSERT_EQ(config.suggestions.exclude_from_suggestions.size(), 2);
}

TEST_F(ConfigTest, CIConfigCustomization) {
    Config config = Config::default_config();
    config.ci.enabled = true;
    config.ci.regression_threshold_percent = 5.0;
    config.ci.fail_on_regression = false;
    config.ci.baseline_file = "/path/to/baseline.json";
    config.ci.post_comment = false;

    EXPECT_TRUE(config.ci.enabled);
    EXPECT_EQ(config.ci.regression_threshold_percent, 5.0);
    EXPECT_FALSE(config.ci.fail_on_regression);
    EXPECT_EQ(config.ci.baseline_file, "/path/to/baseline.json");
    EXPECT_FALSE(config.ci.post_comment);
}

TEST_F(ConfigTest, StorageConfigCustomization) {
    Config config = Config::default_config();
    config.storage.backend = StorageBackend::SQLITE;
    config.storage.sqlite_path = "/data/bha.db";
    config.storage.retention_days = 30;

    EXPECT_EQ(config.storage.backend, StorageBackend::SQLITE);
    EXPECT_EQ(config.storage.sqlite_path, "/data/bha.db");
    EXPECT_EQ(config.storage.retention_days, 30);
}

TEST_F(ConfigTest, PerformanceConfigCustomization) {
    Config config = Config::default_config();
    config.performance.num_threads = 8;
    config.performance.memory_limit_mb = 4096;
    config.performance.streaming_mode = true;
    config.performance.cache_size = 50;

    EXPECT_EQ(config.performance.num_threads, 8);
    EXPECT_EQ(config.performance.memory_limit_mb, 4096);
    EXPECT_TRUE(config.performance.streaming_mode);
    EXPECT_EQ(config.performance.cache_size, 50);
}

TEST_F(ConfigTest, AdvancedConfigCustomization) {
    Config config = Config::default_config();
    config.advanced.auto_detect_compiler = false;
    config.advanced.use_wrapper = false;
    config.advanced.plugin_dir = "/custom/plugins";
    config.advanced.debug_mode = true;
    config.advanced.dump_intermediate_data = true;

    EXPECT_FALSE(config.advanced.auto_detect_compiler);
    EXPECT_FALSE(config.advanced.use_wrapper);
    EXPECT_EQ(config.advanced.plugin_dir, "/custom/plugins");
    EXPECT_TRUE(config.advanced.debug_mode);
    EXPECT_TRUE(config.advanced.dump_intermediate_data);
}

TEST_F(ConfigTest, LoggingConfigCustomization) {
    Config config = Config::default_config();
    config.logging.level = "DEBUG";
    config.logging.file = "/var/log/bha.log";
    config.logging.console = false;
    config.logging.format = "[{level}] {message}";

    EXPECT_EQ(config.logging.level, "DEBUG");
    EXPECT_EQ(config.logging.file, "/var/log/bha.log");
    EXPECT_FALSE(config.logging.console);
    EXPECT_EQ(config.logging.format, "[{level}] {message}");
}