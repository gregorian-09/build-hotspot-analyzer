//
// Created by gregorian on 10/12/2025.
//

#include <gtest/gtest.h>
#include "bha/export/report_generator.h"
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace bha::export_module;
using namespace bha::core;

class ReportGeneratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_output_path_ = "/tmp/test_report";
    }

    void TearDown() override {
        for (const std::vector<std::string> extensions = {".html", ".json", ".csv", ".md", ".txt"}; const auto& ext : extensions) {
            if (std::string path = base_output_path_ + ext; std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }
    }

    std::string base_output_path_;

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
        trace.total_build_time_ms = 10000.0;
        trace.commit_sha = "abc123";
        trace.branch = "main";
        return trace;
    }

    static std::string read_file(const std::string& file_path) {
        std::ifstream file(file_path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
};

TEST_F(ReportGeneratorTest, GenerateJSONReport) {
    ReportGenerator::Options options;
    options.format = ExportFormat::JSON;
    options.output_path = base_output_path_ + ".json";
    ReportGenerator generator(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = generator.generate(metrics, suggestions, trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".json"));
}

TEST_F(ReportGeneratorTest, GenerateHTMLReport) {
    ReportGenerator::Options options;
    options.format = ExportFormat::HTML;
    options.output_path = base_output_path_ + ".html";
    ReportGenerator generator(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = generator.generate(metrics, suggestions, trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".html"));
}

TEST_F(ReportGeneratorTest, GenerateCSVReport) {
    ReportGenerator::Options options;
    options.format = ExportFormat::CSV;
    options.output_path = base_output_path_ + ".csv";
    ReportGenerator generator(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = generator.generate(metrics, suggestions, trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".csv"));
}

TEST_F(ReportGeneratorTest, GenerateMarkdownReport) {
    ReportGenerator::Options options;
    options.format = ExportFormat::MARKDOWN;
    options.output_path = base_output_path_ + ".md";
    ReportGenerator generator(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = generator.generate(metrics, suggestions, trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".md"));
}

TEST_F(ReportGeneratorTest, GenerateTextReport) {
    ReportGenerator::Options options;
    options.format = ExportFormat::TEXT;
    options.output_path = base_output_path_ + ".txt";
    ReportGenerator generator(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = generator.generate(metrics, suggestions, trace);

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".txt"));
}

TEST_F(ReportGeneratorTest, GenerateMultipleFormats) {
    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    std::vector formats = {
        ExportFormat::JSON,
        ExportFormat::HTML,
        ExportFormat::CSV
    };

    auto result = ReportGenerator::generate_multi_format(
        metrics, suggestions, trace, formats, base_output_path_
    );

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".json"));
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".html"));
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".csv"));
}

TEST_F(ReportGeneratorTest, GenerateMultipleFormatsAllTypes) {
    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    std::vector formats = {
        ExportFormat::JSON,
        ExportFormat::HTML,
        ExportFormat::CSV,
        ExportFormat::MARKDOWN,
        ExportFormat::TEXT
    };

    auto result = ReportGenerator::generate_multi_format(
        metrics, suggestions, trace, formats, base_output_path_
    );

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".json"));
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".html"));
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".csv"));
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".md"));
    EXPECT_TRUE(std::filesystem::exists(base_output_path_ + ".txt"));
}

TEST_F(ReportGeneratorTest, JSONReportContainsData) {
    ReportGenerator::Options options;
    options.format = ExportFormat::JSON;
    options.output_path = base_output_path_ + ".json";
    ReportGenerator generator(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = generator.generate(metrics, suggestions, trace);

    ASSERT_TRUE(result.is_success());

    std::string json_content = read_file(base_output_path_ + ".json");
    EXPECT_TRUE(json_content.find("test1.cpp") != std::string::npos);
}

TEST_F(ReportGeneratorTest, HTMLReportContainsHTMLTags) {
    ReportGenerator::Options options;
    options.format = ExportFormat::HTML;
    options.output_path = base_output_path_ + ".html";
    ReportGenerator generator(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = generator.generate(metrics, suggestions, trace);

    ASSERT_TRUE(result.is_success());

    std::string html_content = read_file(base_output_path_ + ".html");
    EXPECT_TRUE(html_content.find("<html") != std::string::npos ||
                html_content.find("<!DOCTYPE") != std::string::npos);
    EXPECT_TRUE(html_content.find("</html>") != std::string::npos);
}

TEST_F(ReportGeneratorTest, CustomHTMLOptions) {
    ReportGenerator::Options options;
    options.format = ExportFormat::HTML;
    options.output_path = base_output_path_ + ".html";
    options.html_options.title = "Custom Report Title";
    ReportGenerator generator(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = generator.generate(metrics, suggestions, trace);

    ASSERT_TRUE(result.is_success());

    std::string html_content = read_file(base_output_path_ + ".html");

    EXPECT_FALSE(html_content.empty());
    EXPECT_GT(html_content.length(), 0);
}

TEST_F(ReportGeneratorTest, MarkdownReportContainsMarkdownSyntax) {
    ReportGenerator::Options options;
    options.format = ExportFormat::MARKDOWN;
    options.output_path = base_output_path_ + ".md";
    ReportGenerator generator(options);

    auto metrics = create_test_metrics();
    auto suggestions = create_test_suggestions();
    auto trace = create_test_trace();

    auto result = generator.generate(metrics, suggestions, trace);

    ASSERT_TRUE(result.is_success());

    std::string md_content = read_file(base_output_path_ + ".md");
    EXPECT_TRUE(md_content.find("#") != std::string::npos);
}
