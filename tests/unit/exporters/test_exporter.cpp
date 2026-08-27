//
// Created by gregorian-rayne on 12/30/25.
//

#include <gtest/gtest.h>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
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
        header1.self_parse_time = std::chrono::milliseconds(320);
        header1.inclusion_count = 25;
        header1.including_files = 10;
        result.dependencies.headers.push_back(header1);
        result.dependencies.metric_capabilities.push_back({
            "frontend.header.consumer_fanout",
            MetricProvenance{
                EvidenceKind::Derived,
                "DependencyAnalyzer",
                "",
                "-ftime-trace Source events",
                "build",
                TimingDomain::None,
                TimingAggregation::None,
                "Fanout counts translation units with an observed Source event"
            }
        });

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

        result.cache_distribution.compile_requests = 50;
        result.cache_distribution.executed_compilations = 45;
        result.cache_distribution.compilations = 45;
        result.cache_distribution.cache_hits = 36;
        result.cache_distribution.cache_misses = 9;
        result.cache_distribution.cache_errors = 2;
        result.cache_distribution.cache_timeouts = 1;
        result.cache_distribution.non_cacheable_compilations = 4;
        result.cache_distribution.cache_writes = 40;
        result.cache_distribution.hit_rate_percent = 80.0;
        MetricCapability cache_capability;
        cache_capability.metric = "cache.outcomes";
        cache_capability.provenance.evidence = EvidenceKind::Observed;
        cache_capability.provenance.producer = "sccache";
        cache_capability.provenance.capture_mode = "--show-stats --stats-format=json";
        cache_capability.provenance.scope = "cache-server";
        result.cache_distribution.metric_capabilities.push_back(cache_capability);

        result.build_session.timed_commands = 2;
        result.build_session.total_commands = 2;
        result.build_session.wall_clock_time = std::chrono::seconds(2);
        result.build_session.serial_time = std::chrono::seconds(3);
        result.build_session.peak_parallelism = 2;
        result.build_session.average_parallelism = 1.5;
        result.build_session.critical_path_time = std::chrono::seconds(2);
        result.build_session.critical_path = {"compile-a", "link"};
        result.build_session.compile_trace_references = 1;
        result.build_session.step_metrics.push_back({
            BuildStepRole::Custom,
            1,
            1,
            std::chrono::milliseconds(250),
            1,
            1,
            0,
            0,
            std::nullopt,
            std::nullopt
        });
        result.build_session.step_metrics.front().output_observations = 1;
        result.build_session.step_metrics.front().stdout_bytes = 5;
        result.build_session.host_telemetry.memory_samples = 2;
        result.build_session.host_telemetry.peak_memory_used_kib = 2048;
        result.build_session.host_telemetry.cpu_load_samples = 2;
        result.build_session.host_telemetry.peak_before_cpu_load_average = 1.5;
        result.build_session.host_telemetry.peak_after_cpu_load_average = 2.5;
        result.build_session.host_system = BuildHostSystemInfo{};
        result.build_session.host_system->os_name = "Linux";
        result.build_session.host_system->logical_cpu_count = 16;

        MetricCapability session_capability;
        session_capability.metric = "build.scheduler.parallelism";
        session_capability.provenance.evidence = EvidenceKind::Derived;
        session_capability.provenance.producer = "BuildSessionAnalyzer";
        session_capability.provenance.scope = "build-session";
        result.build_session.metric_capabilities.push_back(session_capability);

        result.linker.invocations = 1;
        result.linker.timed_invocations = 1;
        result.linker.output_size_observations = 1;
        result.linker.wall_clock_time = std::chrono::seconds(2);
        result.linker.output_bytes = 4096;
        result.linker.trace_wall_clock_time = std::chrono::milliseconds(2200);
        result.linker.lto_time = std::chrono::milliseconds(900);
        result.linker.metric_capabilities.push_back({
            "link.output_bytes",
            MetricProvenance{
                EvidenceKind::Derived,
                "LinkerAnalyzer",
                "",
                "build-session-events",
                "link-command",
                TimingDomain::WallClock,
                TimingAggregation::Exclusive,
                ""
            }
        });

        result.targets.target_commands = 1;
        result.targets.matched_commands = 1;
        analyzers::BuildTargetAnalysisResult::TargetInfo target;
        target.id = "app-id";
        target.name = "app";
        target.type = "EXECUTABLE";
        target.compile_commands = 1;
        target.timed_compile_commands = 1;
        target.compile_wall_clock_time = std::chrono::milliseconds(700);
        target.output_size_observations = 1;
        target.output_bytes = 2048;
        target.precompile_headers = {"/src/pch.h"};
        result.targets.targets.push_back(target);
        result.targets.pch_targets = 1;
        result.targets.pch_headers = 1;
        result.targets.metric_capabilities.push_back({
            "build.target.command_ownership",
            MetricProvenance{
                EvidenceKind::Derived,
                "BuildTargetAnalyzer",
                "",
                "cmake-file-api-v1+instrumentation-v1",
                "target",
                TimingDomain::WallClock,
                TimingAggregation::Exclusive,
                ""
            }
        });

        result.modules.rules = 2;
        result.modules.provided_modules = 2;
        result.modules.required_modules = 1;
        result.modules.resolved_dependencies = 1;
        result.modules.dependencies.emplace_back("M", "User");
        result.modules.metric_capabilities.push_back({
            "module.dependency_graph",
            MetricProvenance{
                EvidenceKind::Derived,
                "ModuleAnalyzer",
                "",
                "-format=p1689",
                "build",
                TimingDomain::None,
                TimingAggregation::None,
                ""
            }
        });

        result.process_resources.observations = 2;
        result.process_resources.total_process_time = std::chrono::milliseconds(101);
        result.process_resources.total_user_time = std::chrono::milliseconds(92);
        result.process_resources.peak_memory_kib = 87536;
        result.process_resources.metric_capabilities.push_back({
            "process.resource_counters",
            MetricProvenance{
                EvidenceKind::Observed,
                "clang",
                "",
                "-fproc-stat-report=FILE",
                "build",
                TimingDomain::WallClock,
                TimingAggregation::Exclusive,
                "Rows identify tool invocations and output paths"
            }
        });

        MetricCapability capability;
        capability.metric = "compile.translation_unit.wall_time";
        capability.provenance.evidence = EvidenceKind::Observed;
        capability.provenance.producer = "clang";
        capability.provenance.producer_version = "18.1.3";
        capability.provenance.capture_mode = "-ftime-trace";
        capability.provenance.scope = "translation-unit";
        capability.provenance.timing_domain = TimingDomain::WallClock;
        capability.provenance.timing_aggregation = TimingAggregation::Exclusive;
        result.metric_capabilities.push_back(capability);

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
        EXPECT_TRUE(json_str.find("\"suggestions\"") == std::string::npos);
    }

    TEST_F(JsonExporterTest, ExportToStream) {
        std::ostringstream stream;
        const auto result = exporter_->export_to_stream(stream, analysis, suggestions, {}, nullptr);
        ASSERT_TRUE(result.is_ok());

        const auto& json_str = stream.str();
        EXPECT_FALSE(json_str.empty());
        EXPECT_TRUE(json_str.find("\"summary\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"cache_distribution\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"cache_hits\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"hit_rate_percent\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"metric_capabilities\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"compile.translation_unit.wall_time\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"build_session\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"step_metrics\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("custom") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"host_telemetry\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"peak_memory_used_kib\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"host_system\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"logical_cpu_count\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"compile_trace_references\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"output_observations\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"stdout_bytes\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"build.scheduler.parallelism\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"linker\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"link.output_bytes\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"targets\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"build.target.command_ownership\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"self_parse_time_ms\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"frontend.header.consumer_fanout\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"module.dependency_graph\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"provided_modules\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"pch_headers\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("/src/pch.h") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"process_resources\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"peak_memory_kib\"") != std::string::npos);
        EXPECT_TRUE(json_str.find("\"process.resource_counters\"") != std::string::npos);
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

    TEST_F(JsonExporterTest, EmitsCanonicalAnalysisDomains) {
        const auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto document = nlohmann::json::parse(result.value());
        EXPECT_EQ(document["document_type"], "bha-analysis");
        EXPECT_EQ(document["$schema"], "https://json-schema.org/draft/2020-12/schema");
        EXPECT_TRUE(document.contains("$id"));
        EXPECT_TRUE(document["performance"].contains("sequential_time_ms"));
        EXPECT_TRUE(document["performance"].contains("median_file_time_ms"));
        EXPECT_TRUE(document["performance"].contains("p90_file_time_ms"));
        EXPECT_TRUE(document["performance"].contains("p99_file_time_ms"));
        EXPECT_TRUE(document["files"][0].contains("breakdown"));
        EXPECT_TRUE(document["dependencies"].contains("total_include_time_ms"));
        EXPECT_TRUE(document["templates"].contains("template_time_percent"));
        EXPECT_TRUE(document.contains("symbols"));
        EXPECT_TRUE(document.contains("build_session"));
        EXPECT_TRUE(document.contains("linker"));
        EXPECT_TRUE(document.contains("targets"));
        EXPECT_TRUE(document.contains("modules"));
        EXPECT_TRUE(document.contains("process_resources"));
        EXPECT_TRUE(document["summary"].contains("metric_capabilities"));
    }

    TEST_F(JsonExporterTest, IncludesSuggestionsOnlyWhenRequested) {
        const auto without_suggestions = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(without_suggestions.is_ok());
        const auto without_document = nlohmann::json::parse(without_suggestions.value());
        EXPECT_FALSE(without_document.contains("suggestions"));

        ExportOptions options;
        options.include_suggestions = true;
        const auto with_suggestions = exporter_->export_to_string(analysis, suggestions, options);
        ASSERT_TRUE(with_suggestions.is_ok());
        const auto with_document = nlohmann::json::parse(with_suggestions.value());
        ASSERT_TRUE(with_document.contains("suggestions"));
        ASSERT_EQ(with_document["suggestions"].size(), suggestions.size());
        EXPECT_EQ(with_document["suggestions"][0]["id"], suggestions[0].id);
        EXPECT_TRUE(with_document["suggestions"][0].contains("edits"));
    }

    TEST_F(JsonExporterTest, AppliesTimingAndConfidenceOptions) {
        ExportOptions options;
        options.include_timing = false;
        options.include_suggestions = true;
        options.min_confidence = 0.9;

        const auto result = exporter_->export_to_string(analysis, suggestions, options);
        ASSERT_TRUE(result.is_ok());

        const auto document = nlohmann::json::parse(result.value());
        EXPECT_TRUE(document["performance"]["total_build_time_ms"].is_null());
        EXPECT_TRUE(document["files"][0]["breakdown"].is_null());
        ASSERT_TRUE(document.contains("suggestions"));
        ASSERT_EQ(document["suggestions"].size(), 1u);
        EXPECT_EQ(document["suggestions"][0]["id"], "fwd-decl-001");
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

    TEST_F(HtmlExporterTest, AlwaysUsesDarkTheme) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& html_str = result.value();
        EXPECT_TRUE(html_str.find("<body class=\"dark-theme\">") != std::string::npos);
        EXPECT_EQ(html_str.find("light-theme"), std::string::npos);
    }

    TEST_F(HtmlExporterTest, ContainsAnalysisData) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& html_str = result.value();
        EXPECT_TRUE(html_str.find("main.cpp") != std::string::npos);
        EXPECT_TRUE(html_str.find("utils.cpp") != std::string::npos);
        EXPECT_TRUE(html_str.find("Cache Hit Rate") != std::string::npos);
        EXPECT_TRUE(html_str.find("Cache Errors") != std::string::npos);
    }

    TEST_F(HtmlExporterTest, DoesNotContainSuggestionData) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& html_str = result.value();
        EXPECT_TRUE(html_str.find("fwd-decl-001") == std::string::npos);
        EXPECT_TRUE(html_str.find("forward declaration") == std::string::npos);
    }

    TEST_F(HtmlExporterTest, ExposesCanonicalBuildContext) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& html_str = result.value();
        EXPECT_TRUE(html_str.find("build-context") != std::string::npos);
        EXPECT_TRUE(html_str.find("Sequential Time") != std::string::npos);
        EXPECT_TRUE(html_str.find("Build Session") != std::string::npos);
        EXPECT_TRUE(html_str.find("Metric Evidence") != std::string::npos);
        EXPECT_TRUE(html_str.find("unavailable") != std::string::npos);
        EXPECT_TRUE(html_str.find("sequential_time_ms") != std::string::npos);
        EXPECT_TRUE(html_str.find("linker") != std::string::npos);
    }

    TEST_F(HtmlExporterTest, IncludesCanonicalSuggestionPayloadWhenRequested) {
        ExportOptions options;
        options.include_suggestions = true;

        auto result = exporter_->export_to_string(analysis, suggestions, options);
        ASSERT_TRUE(result.is_ok());

        const auto& html_str = result.value();
        EXPECT_TRUE(html_str.find("fwd-decl-001") != std::string::npos);
        EXPECT_TRUE(html_str.find("estimated_savings_evidence") != std::string::npos);
        EXPECT_TRUE(html_str.find("Suggestion Evidence") != std::string::npos);
    }

    TEST_F(HtmlExporterTest, FormatsSavingsUsingSecondsForLargeValues) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& html_str = result.value();
        EXPECT_TRUE(html_str.find("5000.0 ms") != std::string::npos);
        EXPECT_TRUE(html_str.find("3000.0 ms") != std::string::npos);
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
        EXPECT_EQ(csv_str.find("\r\r\n"), std::string::npos);
    }

    TEST_F(CsvExporterTest, ContainsFileData) {
        auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& csv_str = result.value();
        EXPECT_TRUE(csv_str.find("main.cpp") != std::string::npos);
        EXPECT_TRUE(csv_str.find("utils.cpp") != std::string::npos);
    }

    TEST_F(CsvExporterTest, StreamExportIsRectangular) {
        const auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto count_fields = [](const std::string& line) {
            bool quoted = false;
            std::size_t fields = 1;
            for (std::size_t index = 0; index < line.size(); ++index) {
                if (line[index] == '"') {
                    if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                        ++index;
                    } else {
                        quoted = !quoted;
                    }
                } else if (line[index] == ',' && !quoted) {
                    ++fields;
                }
            }
            return fields;
        };

        std::istringstream lines(result.value());
        std::string line;
        ASSERT_TRUE(std::getline(lines, line));
        const auto header_fields = count_fields(line);
        EXPECT_EQ(header_fields, 16u);
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                EXPECT_EQ(count_fields(line), header_fields);
            }
        }
        EXPECT_TRUE(result.value().find("# Files") == std::string::npos);
        EXPECT_TRUE(result.value().find("# Suggestions") == std::string::npos);
    }

    TEST_F(CsvExporterTest, BundleNormalizesSuggestionsAndEvidence) {
        ExportOptions options;
        options.include_suggestions = true;

        const auto count_fields = [](const std::string& line) {
            bool quoted = false;
            std::size_t fields = 1;
            for (std::size_t index = 0; index < line.size(); ++index) {
                if (line[index] == '"') {
                    if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                        ++index;
                    } else {
                        quoted = !quoted;
                    }
                } else if (line[index] == ',' && !quoted) {
                    ++fields;
                }
            }
            return fields;
        };

        const auto bundle = fs::temp_directory_path() / "bha-csv-bundle-export-test";
        std::error_code error;
        fs::remove_all(bundle, error);

        const auto result = exporter_->export_to_file(bundle, analysis, suggestions, options, nullptr);
        ASSERT_TRUE(result.is_ok());
        ASSERT_TRUE(fs::is_directory(bundle));

        EXPECT_TRUE(fs::exists(bundle / "metadata.csv"));
        EXPECT_TRUE(fs::exists(bundle / "summary.csv"));
        EXPECT_TRUE(fs::exists(bundle / "metric_capabilities.csv"));
        EXPECT_TRUE(fs::exists(bundle / "build_session.csv"));
        EXPECT_TRUE(fs::exists(bundle / "critical_path.csv"));
        EXPECT_TRUE(fs::exists(bundle / "linker.csv"));
        EXPECT_TRUE(fs::exists(bundle / "cache.csv"));
        EXPECT_TRUE(fs::exists(bundle / "process_resources.csv"));
        EXPECT_TRUE(fs::exists(bundle / "files.csv"));
        EXPECT_TRUE(fs::exists(bundle / "dependency_edges.csv"));
        EXPECT_TRUE(fs::exists(bundle / "templates.csv"));
        EXPECT_TRUE(fs::exists(bundle / "suggestions.csv"));
        EXPECT_TRUE(fs::exists(bundle / "suggestion_files.csv"));
        EXPECT_TRUE(fs::exists(bundle / "suggestion_steps.csv"));
        EXPECT_TRUE(fs::exists(bundle / "suggestion_examples.csv"));
        EXPECT_TRUE(fs::exists(bundle / "suggestion_edits.csv"));
        EXPECT_TRUE(fs::exists(bundle / "build_steps.csv"));
        EXPECT_TRUE(fs::exists(bundle / "targets.csv"));
        EXPECT_TRUE(fs::exists(bundle / "modules.csv"));
        EXPECT_TRUE(fs::exists(bundle / "symbols.csv"));

        std::ifstream suggestions_file(bundle / "suggestions.csv");
        ASSERT_TRUE(suggestions_file.is_open());
        const std::string suggestions_csv(
            (std::istreambuf_iterator<char>(suggestions_file)),
            std::istreambuf_iterator<char>()
        );
        EXPECT_TRUE(suggestions_csv.find("estimated_savings_evidence") != std::string::npos);
        EXPECT_TRUE(suggestions_csv.find("unavailable") != std::string::npos);
        EXPECT_TRUE(suggestions_csv.find("fwd-decl-001") != std::string::npos);
        EXPECT_TRUE(suggestions_csv.find("pch-001") != std::string::npos);

        std::ifstream files_file(bundle / "files.csv");
        ASSERT_TRUE(files_file.is_open());
        std::string header;
        ASSERT_TRUE(std::getline(files_file, header));
        std::string file_row;
        ASSERT_TRUE(std::getline(files_file, file_row));
        EXPECT_EQ(count_fields(header), count_fields(file_row));

        for (const auto& entry : fs::directory_iterator(bundle)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".csv") {
                continue;
            }
            std::ifstream table(entry.path());
            ASSERT_TRUE(table.is_open());
            std::string line;
            ASSERT_TRUE(std::getline(table, header));
            const auto expected_columns = count_fields(header);
            while (std::getline(table, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    EXPECT_EQ(count_fields(line), expected_columns)
                        << "Non-rectangular row in " << entry.path();
                }
            }
        }

        fs::remove_all(bundle, error);
    }

    TEST_F(CsvExporterTest, StreamRejectsSuggestionsInsteadOfDroppingThem) {
        ExportOptions options;
        options.include_suggestions = true;

        const auto result = exporter_->export_to_string(analysis, suggestions, options);
        EXPECT_TRUE(result.is_err());
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

    TEST_F(MarkdownExporterTest, LabelsUnmeasuredSavingsAsUnavailable) {
        ExportOptions options;
        options.include_suggestions = true;

        const auto result = exporter_->export_to_string(analysis, suggestions, options);
        ASSERT_TRUE(result.is_ok());

        EXPECT_TRUE(result.value().find("**Est. Savings:** unavailable") != std::string::npos);
    }

    TEST_F(MarkdownExporterTest, ContainsCanonicalAnalysisDomains) {
        const auto result = exporter_->export_to_string(analysis, suggestions, {});
        ASSERT_TRUE(result.is_ok());

        const auto& markdown = result.value();
        EXPECT_TRUE(markdown.find("P90 File Time") != std::string::npos);
        EXPECT_TRUE(markdown.find("Dependency Analysis") != std::string::npos);
        EXPECT_TRUE(markdown.find("Template Instantiation Analysis") != std::string::npos);
        EXPECT_TRUE(markdown.find("Build Session") != std::string::npos);
        EXPECT_TRUE(markdown.find("Linker, Targets, Modules, and Resources") != std::string::npos);
        EXPECT_TRUE(markdown.find("Evidence and Limitations") != std::string::npos);
        EXPECT_TRUE(markdown.find("compile.translation_unit.wall_time") != std::string::npos);
    }

    TEST_F(MarkdownExporterTest, AppliesSuggestionLimit) {
        ExportOptions options;
        options.include_suggestions = true;
        options.max_suggestions = 1;

        const auto result = exporter_->export_to_string(analysis, suggestions, options);
        ASSERT_TRUE(result.is_ok());

        const auto& markdown = result.value();
        EXPECT_TRUE(markdown.find("Use forward declaration for Config class") != std::string::npos);
        EXPECT_TRUE(markdown.find("Add frequently used headers to PCH") == std::string::npos);
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
