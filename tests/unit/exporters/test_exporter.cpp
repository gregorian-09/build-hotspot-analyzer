//
// Created by gregorian-rayne on 12/30/25.
//

#include <gtest/gtest.h>
#include <sstream>
#include <filesystem>

#include "bha/exporters/exporter.hpp"
#include "bha/analyzers/analyzer.hpp"

namespace bha::exporters::test
{
    namespace fs = std::filesystem;

    /**
     * Creates a sample analysis result for testing.
     */
    analyzers::AnalysisResult create_sample_analysis() {
        analyzers::AnalysisResult result;

        result.performance.total_build_time = std::chrono::seconds(120);
        result.performance.sequential_time = std::chrono::seconds(300);
        result.performance.parallel_time = std::chrono::seconds(120);
        result.performance.parallelism_efficiency = 2.5;
        result.performance.total_files = 50;
        result.performance.avg_file_time = std::chrono::milliseconds(2400);
        result.performance.median_file_time = std::chrono::milliseconds(1500);
        result.performance.p90_file_time = std::chrono::milliseconds(5000);
        result.performance.p99_file_time = std::chrono::milliseconds(10000);

        analyzers::FileAnalysisResult file1;
        file1.file = "src/main.cpp";
        file1.compile_time = std::chrono::milliseconds(5000);
        file1.frontend_time = std::chrono::milliseconds(3500);
        file1.backend_time = std::chrono::milliseconds(1500);
        file1.time_percent = 25.0;
        file1.rank = 1;
        file1.include_count = 15;
        file1.template_count = 5;
        result.files.push_back(file1);

        analyzers::FileAnalysisResult file2;
        file2.file = "src/utils.cpp";
        file2.compile_time = std::chrono::milliseconds(3000);
        file2.frontend_time = std::chrono::milliseconds(2000);
        file2.backend_time = std::chrono::milliseconds(1000);
        file2.time_percent = 15.0;
        file2.rank = 2;
        file2.include_count = 8;
        file2.template_count = 2;
        result.files.push_back(file2);

        result.dependencies.total_includes = 100;
        result.dependencies.unique_headers = 45;
        result.dependencies.max_include_depth = 12;
        result.dependencies.total_include_time = std::chrono::milliseconds(8000);

        analyzers::DependencyAnalysisResult::HeaderInfo header1;
        header1.path = "include/config.h";
        header1.total_parse_time = std::chrono::milliseconds(500);
        header1.inclusion_count = 25;
        header1.including_files = 10;
        header1.impact_score = 0.85;
        result.dependencies.headers.push_back(header1);

        result.templates.total_template_time = std::chrono::milliseconds(3000);
        result.templates.template_time_percent = 15.0;
        result.templates.total_instantiations = 150;

        analyzers::TemplateAnalysisResult::TemplateInfo tmpl1;
        tmpl1.name = "std::vector";
        tmpl1.full_signature = "std::vector<int>";
        tmpl1.total_time = std::chrono::milliseconds(800);
        tmpl1.instantiation_count = 45;
        tmpl1.time_percent = 4.0;
        result.templates.templates.push_back(tmpl1);

        result.analysis_time = std::chrono::system_clock::now();
        result.analysis_duration = std::chrono::milliseconds(500);

        return result;
    }

    /**
     * Creates sample suggestions for testing.
     */
    std::vector<Suggestion> create_sample_suggestions() {
        std::vector<Suggestion> suggestions;

        Suggestion s1;
        s1.id = "fwd-decl-001";
        s1.type = SuggestionType::ForwardDeclaration;
        s1.priority = Priority::High;
        s1.confidence = 0.92;
        s1.title = "Use forward declaration for Config class";
        s1.description = "The Config class is only used by pointer/reference in header.h";
        s1.rationale = "Forward declarations reduce compile-time dependencies.";
        s1.estimated_savings = std::chrono::milliseconds(500);
        s1.estimated_savings_percent = 2.5;
        s1.target_file.path = "include/header.h";
        s1.target_file.line_start = 10;
        s1.target_file.line_end = 10;
        s1.target_file.action = FileAction::Modify;
        s1.before_code.code = "#include \"config.h\"";
        s1.after_code.code = "class Config;";
        s1.implementation_steps = {
            "Replace #include \"config.h\" with forward declaration",
            "Add #include \"config.h\" to the .cpp file"
        };
        s1.is_safe = true;
        suggestions.push_back(s1);

        Suggestion s2;
        s2.id = "pch-001";
        s2.type = SuggestionType::PCHOptimization;
        s2.priority = Priority::Medium;
        s2.confidence = 0.85;
        s2.title = "Add frequently used headers to PCH";
        s2.description = "Several headers are included in 80% of compilation units.";
        s2.rationale = "Precompiled headers can significantly reduce compilation time.";
        s2.estimated_savings = std::chrono::milliseconds(2000);
        s2.estimated_savings_percent = 10.0;
        s2.target_file.path = "pch.h";
        s2.target_file.action = FileAction::Create;
        s2.is_safe = true;
        suggestions.push_back(s2);

        return suggestions;
    }

    // ============================================================================
    // ExporterFactory Tests
    // ============================================================================

    class ExporterFactoryTest : public ::testing::Test {
    protected:
        void SetUp() override {}
        void TearDown() override {}
    };

    TEST_F(ExporterFactoryTest, CreateJsonExporter) {
        auto result = ExporterFactory::create(ExportFormat::JSON);
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value()->format(), ExportFormat::JSON);
        EXPECT_EQ(result.value()->file_extension(), ".json");
        EXPECT_EQ(result.value()->format_name(), "JSON");
    }

    TEST_F(ExporterFactoryTest, CreateHtmlExporter) {
        auto result = ExporterFactory::create(ExportFormat::HTML);
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value()->format(), ExportFormat::HTML);
        EXPECT_EQ(result.value()->file_extension(), ".html");
        EXPECT_EQ(result.value()->format_name(), "HTML");
    }

    TEST_F(ExporterFactoryTest, CreateCsvExporter) {
        auto result = ExporterFactory::create(ExportFormat::CSV);
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value()->format(), ExportFormat::CSV);
        EXPECT_EQ(result.value()->file_extension(), ".csv");
        EXPECT_EQ(result.value()->format_name(), "CSV");
    }

    TEST_F(ExporterFactoryTest, CreateMarkdownExporter) {
        auto result = ExporterFactory::create(ExportFormat::Markdown);
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value()->format(), ExportFormat::Markdown);
        EXPECT_EQ(result.value()->file_extension(), ".md");
        EXPECT_EQ(result.value()->format_name(), "Markdown");
    }

    TEST_F(ExporterFactoryTest, AvailableFormats) {
        auto formats = ExporterFactory::available_formats();
        EXPECT_GE(formats.size(), 4u);

        auto has_format = [&formats](const ExportFormat fmt) {
            return std::ranges::find(formats, fmt) != formats.end();
        };

        EXPECT_TRUE(has_format(ExportFormat::JSON));
        EXPECT_TRUE(has_format(ExportFormat::HTML));
        EXPECT_TRUE(has_format(ExportFormat::CSV));
        EXPECT_TRUE(has_format(ExportFormat::Markdown));
    }

    // ============================================================================
    // JSON Exporter Tests
    // ============================================================================

    class JsonExporterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            auto result = ExporterFactory::create(ExportFormat::JSON);
            ASSERT_TRUE(result.is_ok());
            exporter_ = std::move(result.value());
            analysis = create_sample_analysis();
            suggestions = create_sample_suggestions();
        }

        std::unique_ptr<IExporter> exporter_;
        analyzers::AnalysisResult analysis;
        std::vector<Suggestion> suggestions;
    };

    TEST_F(JsonExporterTest, ExportToString) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& json_str = result.value();
        EXPECT_FALSE(json_str.empty());
        EXPECT_TRUE(json_str.find("\"bha_version\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"files\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"suggestions\"") != std::string::npos);
    }

    TEST_F(JsonExporterTest, ExportToStream) {
        std::ostringstream stream;
        const auto result = exporter_->export_to_stream(stream, analysis, suggestions, {}, nullptr);
        ASSERT_TRUE(result.is_ok());

        const auto& json_str = stream.str();
        EXPECT_FALSE(json_str.empty());
        EXPECT_TRUE(json_str.find("\"summary\"") != std::string::npos);
    }

    TEST_F(JsonExporterTest, ExportWithOptions) {
        ExportOptions options;
        options.pretty_print = false;
        options.include_suggestions = false;

        auto result = exporter_->export_to_string(analysis, suggestions, options);
        ASSERT_TRUE(result.is_ok());

        const auto& json_str = result.value();
        // Without pretty print, there should be fewer newlines
        // Without suggestions, there shouldn't be suggestion data
        EXPECT_FALSE(json_str.empty());
    }

    // ============================================================================
    // HTML Exporter Tests
    // ============================================================================

    class HtmlExporterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            auto result = ExporterFactory::create(ExportFormat::HTML);
            ASSERT_TRUE(result.is_ok());
            exporter_ = std::move(result.value());
            analysis = create_sample_analysis();
            suggestions = create_sample_suggestions();
        }

        std::unique_ptr<IExporter> exporter_;
        analyzers::AnalysisResult analysis;
        std::vector<Suggestion> suggestions;
    };

    TEST_F(HtmlExporterTest, ExportToString) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& html_str = result.value();
        EXPECT_FALSE(html_str.empty());
        EXPECT_TRUE(html_str.find("<!DOCTYPE html>") != std::string::npos);
        EXPECT_TRUE(html_str.find("<html") != std::string::npos);
        EXPECT_TRUE(html_str.find("</html>") != std::string::npos);
    }

    TEST_F(HtmlExporterTest, ContainsAnalysisData) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& html_str = result.value();
        EXPECT_TRUE(html_str.find("main.cpp") != std::string::npos);
        EXPECT_TRUE(html_str.find("utils.cpp") != std::string::npos);
    }

    TEST_F(HtmlExporterTest, ContainsSuggestionData) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& html_str = result.value();
        EXPECT_TRUE(html_str.find("forward declaration") != std::string::npos ||
                    html_str.find("Forward Declaration") != std::string::npos ||
                    html_str.find("ForwardDeclaration") != std::string::npos);
    }

    // ============================================================================
    // CSV Exporter Tests
    // ============================================================================

    class CsvExporterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            auto result = ExporterFactory::create(ExportFormat::CSV);
            ASSERT_TRUE(result.is_ok());
            exporter_ = std::move(result.value());
            analysis = create_sample_analysis();
            suggestions = create_sample_suggestions();
        }

        std::unique_ptr<IExporter> exporter_;
        analyzers::AnalysisResult analysis;
        std::vector<Suggestion> suggestions;
    };

    TEST_F(CsvExporterTest, ExportToString) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& csv_str = result.value();
        EXPECT_FALSE(csv_str.empty());
        // CSV should have header row with commas
        EXPECT_TRUE(csv_str.find(",") != std::string::npos);
    }

    TEST_F(CsvExporterTest, ContainsFileData) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& csv_str = result.value();
        EXPECT_TRUE(csv_str.find("main.cpp") != std::string::npos);
        EXPECT_TRUE(csv_str.find("utils.cpp") != std::string::npos);
    }

    // ============================================================================
    // Markdown Exporter Tests
    // ============================================================================

    class MarkdownExporterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            auto result = ExporterFactory::create(ExportFormat::Markdown);
            ASSERT_TRUE(result.is_ok());
            exporter_ = std::move(result.value());
            analysis = create_sample_analysis();
            suggestions = create_sample_suggestions();
        }

        std::unique_ptr<IExporter> exporter_;
        analyzers::AnalysisResult analysis;
        std::vector<Suggestion> suggestions;
    };

    TEST_F(MarkdownExporterTest, ExportToString) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& md_str = result.value();
        EXPECT_FALSE(md_str.empty());
        EXPECT_TRUE(md_str.find("#") != std::string::npos);
    }

    TEST_F(MarkdownExporterTest, ContainsStructure) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& md_str = result.value();
        EXPECT_TRUE(md_str.find("# ") != std::string::npos);
        EXPECT_TRUE(md_str.find("|") != std::string::npos);
    }

    // ============================================================================
    // Format Conversion Tests
    // ============================================================================

    TEST(FormatConversionTest, FormatToString) {
        EXPECT_EQ(format_to_string(ExportFormat::JSON), "json");
        EXPECT_EQ(format_to_string(ExportFormat::HTML), "html");
        EXPECT_EQ(format_to_string(ExportFormat::CSV), "csv");
        EXPECT_EQ(format_to_string(ExportFormat::Markdown), "markdown");
    }

    TEST(FormatConversionTest, StringToFormat) {
        EXPECT_EQ(string_to_format("json"), ExportFormat::JSON);
        EXPECT_EQ(string_to_format("JSON"), ExportFormat::JSON);
        EXPECT_EQ(string_to_format("html"), ExportFormat::HTML);
        EXPECT_EQ(string_to_format("HTML"), ExportFormat::HTML);
        EXPECT_EQ(string_to_format("csv"), ExportFormat::CSV);
        EXPECT_EQ(string_to_format("CSV"), ExportFormat::CSV);
        EXPECT_EQ(string_to_format("markdown"), ExportFormat::Markdown);
        EXPECT_EQ(string_to_format("md"), ExportFormat::Markdown);

        EXPECT_FALSE(string_to_format("invalid").has_value());
    }
}  // namespace bha::exporters::test