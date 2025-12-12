//
// Created by gregorian on 10/12/2025.
//

#include <gtest/gtest.h>
#include "bha/export/csv_exporter.h"
#include <filesystem>
#include <fstream>

using namespace bha::export_module;
using namespace bha::core;

class CSVExporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_output_path_ = "/tmp/test_export.csv";
    }

    void TearDown() override {
        if (std::filesystem::exists(test_output_path_)) {
            std::filesystem::remove(test_output_path_);
        }
    }

    std::string test_output_path_;

    static MetricsSummary create_test_metrics() {
        MetricsSummary metrics;
        metrics.total_files_compiled = 50;
        metrics.average_file_time_ms = 200.0;

        Hotspot hotspot1;
        hotspot1.file_path = "test1.cpp";
        hotspot1.time_ms = 500.0;
        hotspot1.impact_score = 0.9;
        metrics.top_slow_files.push_back(hotspot1);

        Hotspot hotspot2;
        hotspot2.file_path = "test2.h";
        hotspot2.time_ms = 300.0;
        hotspot2.impact_score = 0.7;
        metrics.top_slow_files.push_back(hotspot2);

        return metrics;
    }

    static std::vector<Suggestion> create_test_suggestions() {
        std::vector<Suggestion> suggestions;

        Suggestion suggestion;
        suggestion.file_path = "test1.cpp";
        suggestion.type = SuggestionType::PCH_ADDITION;
        suggestion.description = "Use precompiled headers";
        suggestion.estimated_time_savings_ms = 100.0;
        suggestion.confidence = 0.85;
        suggestions.push_back(suggestion);

        return suggestions;
    }

    static BuildTrace create_test_trace() {
        BuildTrace trace;
        trace.commit_sha = "abc123";
        trace.branch = "main";
        return trace;
    }

    static bool file_contains(const std::string& file_path, const std::string& content) {
        std::ifstream file(file_path);
        std::string line;
        while (std::getline(file, line)) {
            if (line.find(content) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

TEST_F(CSVExporterTest, BasicExport) {
    CSVExporter exporter;

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = exporter.export_report(metrics, suggestions, trace, test_output_path_);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(test_output_path_));
}

TEST_F(CSVExporterTest, GetDefaultExtension) {
    const CSVExporter exporter;
    EXPECT_EQ(exporter.get_default_extension(), ".csv");
}

TEST_F(CSVExporterTest, GetFormat) {
    const CSVExporter exporter;
    EXPECT_EQ(exporter.get_format(), ExportFormat::CSV);
}

TEST_F(CSVExporterTest, CustomDelimiter) {
    CSVExporter::Options options;
    options.delimiter = ';';
    CSVExporter exporter(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = exporter.export_report(metrics, suggestions, trace, test_output_path_);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(file_contains(test_output_path_, ";"));
}

TEST_F(CSVExporterTest, NoHeader) {
    CSVExporter::Options options;
    options.include_header = false;
    CSVExporter exporter(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = exporter.export_report(metrics, suggestions, trace, test_output_path_);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(test_output_path_));
}

TEST_F(CSVExporterTest, ExportContainsMetrics) {
    CSVExporter exporter;

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = exporter.export_report(metrics, suggestions, trace, test_output_path_);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(file_contains(test_output_path_, "test1.cpp"));
}