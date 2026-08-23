#include <gtest/gtest.h>

#include "bha/storage.hpp"

#include <filesystem>

namespace bha::storage::test {

namespace {
analyzers::AnalysisResult make_analysis(
    const std::vector<std::pair<std::string, Duration>>& files,
    const Duration build_time,
    const Duration header_time,
    const Duration template_time
) {
    analyzers::AnalysisResult analysis;
    analysis.performance.total_build_time = build_time;
    analysis.dependencies.total_include_time = header_time;
    analysis.templates.total_template_time = template_time;

    for (const auto& [name, time] : files) {
        analyzers::FileAnalysisResult file;
        file.file = name;
        file.compile_time = time;
        analysis.files.push_back(file);
    }
    return analysis;
}
}  // namespace

TEST(StorageCompareTest, ComputesCategoryPercentChanges) {
    const auto old_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(600)}, {"b.cpp", std::chrono::milliseconds(400)}},
        std::chrono::milliseconds(1000),
        std::chrono::milliseconds(200),
        std::chrono::milliseconds(100)
    );
    const auto new_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(750)}, {"b.cpp", std::chrono::milliseconds(450)}},
        std::chrono::milliseconds(1200),
        std::chrono::milliseconds(230),
        std::chrono::milliseconds(80)
    );

    const auto comparison = compare_analyses(old_analysis, new_analysis, 0.05);

    EXPECT_NEAR(comparison.build_time_percent_change, 20.0, 1e-9);
    EXPECT_NEAR(comparison.translation_unit.percent_change, 20.0, 1e-9);
    EXPECT_NEAR(comparison.headers.percent_change, 15.0, 1e-9);
    EXPECT_NEAR(comparison.templates.percent_change, -20.0, 1e-9);
    EXPECT_NEAR(comparison.significance_threshold_percent, 5.0, 1e-9);
    EXPECT_TRUE(comparison.is_significant());
}

TEST(StorageCompareTest, SignificanceThresholdIsConfigurable) {
    const auto old_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(1000)}},
        std::chrono::milliseconds(1000),
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(100)
    );
    const auto new_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(1030)}},
        std::chrono::milliseconds(1030),
        std::chrono::milliseconds(103),
        std::chrono::milliseconds(100)
    );

    const auto comparison_5 = compare_analyses(old_analysis, new_analysis, 0.05);
    const auto comparison_2 = compare_analyses(old_analysis, new_analysis, 0.02);

    EXPECT_FALSE(comparison_5.is_significant());
    EXPECT_TRUE(comparison_2.is_significant());
}

TEST(StorageCompareTest, FileRegressionThresholdFollowsConfiguredSignificance) {
    const auto old_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(100)}},
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(10),
        std::chrono::milliseconds(5)
    );
    const auto new_analysis = make_analysis(
        {{"a.cpp", std::chrono::milliseconds(104)}},
        std::chrono::milliseconds(104),
        std::chrono::milliseconds(10),
        std::chrono::milliseconds(5)
    );

    const auto comparison_5 = compare_analyses(old_analysis, new_analysis, 0.05);
    const auto comparison_2 = compare_analyses(old_analysis, new_analysis, 0.02);

    EXPECT_TRUE(comparison_5.regressions.empty());
    ASSERT_EQ(comparison_2.regressions.size(), 1u);
    EXPECT_NEAR(comparison_2.regressions.front().percent_change, 4.0, 1e-9);
}

TEST(StorageCompareTest, ReportsObservedTranslationUnitRegressionDistribution) {
    const auto old_analysis = make_analysis(
        {
            {"a.cpp", std::chrono::milliseconds(1000)},
            {"b.cpp", std::chrono::milliseconds(2000)},
            {"c.cpp", std::chrono::milliseconds(4000)}
        },
        std::chrono::milliseconds(7000),
        Duration::zero(),
        Duration::zero()
    );
    const auto new_analysis = make_analysis(
        {
            {"a.cpp", std::chrono::milliseconds(1100)},
            {"b.cpp", std::chrono::milliseconds(2400)},
            {"c.cpp", std::chrono::milliseconds(3000)}
        },
        std::chrono::milliseconds(6500),
        Duration::zero(),
        Duration::zero()
    );

    const auto comparison = compare_analyses(old_analysis, new_analysis, 0.50);
    const auto& distribution = comparison.translation_unit_regressions;

    EXPECT_EQ(distribution.matched_files, 3u);
    EXPECT_EQ(distribution.regressed_files, 2u);
    EXPECT_EQ(distribution.total_delta, std::chrono::milliseconds(500));
    EXPECT_EQ(distribution.min_delta, std::chrono::milliseconds(100));
    EXPECT_EQ(distribution.median_delta, std::chrono::milliseconds(100));
    EXPECT_EQ(distribution.p90_delta, std::chrono::milliseconds(400));
    EXPECT_EQ(distribution.p99_delta, std::chrono::milliseconds(400));
    EXPECT_EQ(distribution.max_delta, std::chrono::milliseconds(400));
    EXPECT_TRUE(comparison.regressions.empty());
}

TEST(StorageCompareTest, SummarizesRepeatedObservedBuildTimes) {
    const std::vector<analyzers::AnalysisResult> analyses = {
        make_analysis({}, std::chrono::milliseconds(100), Duration::zero(), Duration::zero()),
        make_analysis({}, std::chrono::milliseconds(110), Duration::zero(), Duration::zero()),
        make_analysis({}, std::chrono::milliseconds(120), Duration::zero(), Duration::zero())
    };

    const auto result = summarize_repeated_analyses(analyses);

    ASSERT_TRUE(result.is_ok());
    const auto& distribution = result.value();
    EXPECT_EQ(distribution.run_count, 3u);
    EXPECT_EQ(distribution.min_build_time, std::chrono::milliseconds(100));
    EXPECT_EQ(distribution.mean_build_time, std::chrono::milliseconds(110));
    EXPECT_EQ(distribution.median_build_time, std::chrono::milliseconds(110));
    EXPECT_EQ(distribution.p90_build_time, std::chrono::milliseconds(120));
    EXPECT_EQ(distribution.p99_build_time, std::chrono::milliseconds(120));
    EXPECT_EQ(distribution.max_build_time, std::chrono::milliseconds(120));
    ASSERT_TRUE(distribution.sample_standard_deviation.has_value());
    EXPECT_EQ(*distribution.sample_standard_deviation, std::chrono::milliseconds(10));
}

TEST(StorageCompareTest, RejectsEmptyRepeatedRunSet) {
    const auto result = summarize_repeated_analyses({});

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().code(), ErrorCode::InvalidArgument);
}

TEST(StorageCompareTest, RejectsNegativeRepeatedBuildTime) {
    const std::vector<analyzers::AnalysisResult> analyses = {
        make_analysis({}, std::chrono::milliseconds(-1), Duration::zero(), Duration::zero()),
        make_analysis({}, std::chrono::milliseconds(1), Duration::zero(), Duration::zero())
    };

    const auto result = summarize_repeated_analyses(analyses);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().code(), ErrorCode::InvalidArgument);
}

TEST(StorageSnapshotTest, SummarizesNamedRunsAndRejectsDuplicates) {
    namespace fs = std::filesystem;

    const auto unique = std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    const fs::path root = fs::temp_directory_path() / ("bha-repeat-snapshot-" + unique);

    SnapshotStore store(root);
    auto first = make_analysis({}, std::chrono::milliseconds(100), Duration::zero(), Duration::zero());
    auto second = make_analysis({}, std::chrono::milliseconds(120), Duration::zero(), Duration::zero());
    ASSERT_TRUE(store.save("run-1", first).is_ok());
    ASSERT_TRUE(store.save("run-2", second).is_ok());

    const auto summary = store.summarize_repeated_runs({"run-1", "run-2"});
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(summary.value().run_count, 2u);
    EXPECT_EQ(summary.value().mean_build_time, std::chrono::milliseconds(110));

    const auto duplicate = store.summarize_repeated_runs({"run-1", "run-1"});
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_EQ(duplicate.error().code(), ErrorCode::InvalidArgument);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(StorageSnapshotTest, PersistsCacheDistributionMetrics) {
    namespace fs = std::filesystem;

    const auto unique = std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    const fs::path root = fs::temp_directory_path() / ("bha-cache-snapshot-" + unique);

    SnapshotStore store(root);
    analyzers::AnalysisResult analysis;
    analysis.performance.total_build_time = std::chrono::milliseconds(4200);
    analyzers::DependencyAnalysisResult::HeaderInfo frontend_header;
    frontend_header.path = "include/config.h";
    frontend_header.total_parse_time = std::chrono::milliseconds(500);
    frontend_header.self_parse_time = std::chrono::milliseconds(320);
    analysis.dependencies.headers.push_back(frontend_header);
    MetricCapability frontend_capability;
    frontend_capability.metric = "frontend.source_self_time";
    frontend_capability.provenance.evidence = EvidenceKind::Derived;
    frontend_capability.provenance.producer = "clang";
    frontend_capability.provenance.capture_mode = "-ftime-trace";
    frontend_capability.provenance.scope = "build";
    frontend_capability.provenance.timing_domain = TimingDomain::WallClock;
    frontend_capability.provenance.timing_aggregation = TimingAggregation::Exclusive;
    analysis.dependencies.metric_capabilities.push_back(frontend_capability);
    analysis.cache_distribution.compile_requests = 12;
    analysis.cache_distribution.executed_compilations = 10;
    analysis.cache_distribution.non_compilation_requests = 1;
    analysis.cache_distribution.unsupported_compiler_requests = 1;
    analysis.cache_distribution.non_cacheable_requests = 0;
    analysis.cache_distribution.compilations = 10;
    analysis.cache_distribution.cache_hits = 7;
    analysis.cache_distribution.cache_misses = 3;
    analysis.cache_distribution.cache_errors = 1;
    analysis.cache_distribution.cache_timeouts = 2;
    analysis.cache_distribution.cache_read_errors = 3;
    analysis.cache_distribution.non_cacheable_compilations = 1;
    analysis.cache_distribution.forced_recaches = 2;
    analysis.cache_distribution.cache_write_errors = 1;
    analysis.cache_distribution.cache_writes = 8;
    analysis.cache_distribution.compilation_failures = 1;
    analysis.cache_distribution.hit_rate_percent = 70.0;
    MetricCapability cache_capability;
    cache_capability.metric = "cache.outcomes";
    cache_capability.provenance.evidence = EvidenceKind::Observed;
    cache_capability.provenance.producer = "sccache";
    cache_capability.provenance.producer_version = "0.14.0";
    cache_capability.provenance.capture_mode = "--show-stats --stats-format=json";
    cache_capability.provenance.scope = "cache-server";
    analysis.cache_distribution.metric_capabilities.push_back(cache_capability);
    analysis.build_session.timed_commands = 10;
    analysis.build_session.total_commands = 12;
    analysis.build_session.wall_clock_time = std::chrono::milliseconds(5000);
    analysis.build_session.serial_time = std::chrono::milliseconds(9000);
    analysis.build_session.peak_parallelism = 4;
    analysis.build_session.average_parallelism = 1.8;
    analysis.build_session.critical_path_time = std::chrono::milliseconds(4200);
    analysis.build_session.critical_path = {"compile-a", "link"};
    analysis.build_session.step_metrics = {
        analyzers::BuildStepAnalysis{
            BuildStepRole::Custom,
            2,
            2,
            std::chrono::milliseconds(700),
            2,
            1,
            1
        },
        analyzers::BuildStepAnalysis{
            BuildStepRole::Test,
            1,
            1,
            std::chrono::milliseconds(300),
            1,
            1,
            0
        }
    };
    analysis.build_session.host_telemetry.memory_samples = 4;
    analysis.build_session.host_telemetry.peak_memory_used_kib = 4096;
    analysis.build_session.host_telemetry.cpu_load_samples = 6;
    analysis.build_session.host_telemetry.peak_before_cpu_load_average = 2.5;
    analysis.build_session.host_telemetry.peak_after_cpu_load_average = 3.5;

    MetricCapability session_capability;
    session_capability.metric = "build.command.wall_time";
    session_capability.provenance.evidence = EvidenceKind::Observed;
    session_capability.provenance.producer = "cmake-instrumentation";
    session_capability.provenance.capture_mode = "api-v1-index";
    session_capability.provenance.scope = "command";
    session_capability.provenance.timing_domain = TimingDomain::WallClock;
    session_capability.provenance.timing_aggregation = TimingAggregation::Exclusive;
    analysis.build_session.metric_capabilities.push_back(session_capability);
    analysis.linker.invocations = 2;
    analysis.linker.timed_invocations = 1;
    analysis.linker.output_size_observations = 1;
    analysis.linker.wall_clock_time = std::chrono::milliseconds(2200);
    analysis.linker.output_bytes = 8192;
    analysis.linker.trace_wall_clock_time = std::chrono::milliseconds(2400);
    analysis.linker.lto_time = std::chrono::milliseconds(1100);
    MetricCapability linker_capability;
    linker_capability.metric = "lto.wall_time";
    linker_capability.provenance.evidence = EvidenceKind::Observed;
    linker_capability.provenance.producer = "lld";
    linker_capability.provenance.capture_mode = "--time-trace";
    linker_capability.provenance.scope = "Total LTO";
    linker_capability.provenance.timing_domain = TimingDomain::WallClock;
    linker_capability.provenance.timing_aggregation = TimingAggregation::Inclusive;
    analysis.linker.metric_capabilities.push_back(linker_capability);
    analysis.targets.target_commands = 1;
    analysis.targets.matched_commands = 1;
    analyzers::BuildTargetAnalysisResult::TargetInfo target;
    target.id = "app-id";
    target.name = "app";
    target.type = "EXECUTABLE";
    target.dependencies = {"lib-id"};
    target.compile_commands = 1;
    target.timed_compile_commands = 1;
    target.compile_wall_clock_time = std::chrono::milliseconds(1200);
    target.output_size_observations = 1;
    target.output_bytes = 2048;
    target.precompile_headers = {"/src/pch.h"};
    analysis.targets.targets.push_back(target);
    analysis.targets.pch_targets = 1;
    analysis.targets.pch_headers = 1;
    MetricCapability target_capability;
    target_capability.metric = "build.target.command_ownership";
    target_capability.provenance.evidence = EvidenceKind::Derived;
    target_capability.provenance.producer = "BuildTargetAnalyzer";
    target_capability.provenance.capture_mode = "cmake-file-api-v1+instrumentation-v1";
    target_capability.provenance.scope = "target";
    analysis.targets.metric_capabilities.push_back(target_capability);

    analysis.modules.rules = 2;
    analysis.modules.provided_modules = 2;
    analysis.modules.required_modules = 1;
    analysis.modules.resolved_dependencies = 1;
    analysis.modules.dependencies.emplace_back("M", "User");
    analysis.modules.metric_capabilities.push_back({
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

    analysis.process_resources.observations = 2;
    analysis.process_resources.total_process_time = std::chrono::milliseconds(101);
    analysis.process_resources.total_user_time = std::chrono::milliseconds(92);
    analysis.process_resources.peak_memory_kib = 87536;
    analysis.process_resources.metric_capabilities.push_back({
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
    analysis.metric_capabilities.push_back(capability);

    const auto save_result = store.save("cache-metrics", analysis);
    ASSERT_TRUE(save_result.is_ok());

    const auto load_result = store.load("cache-metrics");
    ASSERT_TRUE(load_result.is_ok());
    const auto& cache = load_result.value().analysis.cache_distribution;
    const auto& dependencies = load_result.value().analysis.dependencies;
    const auto& modules = load_result.value().analysis.modules;
    const auto& process_resources = load_result.value().analysis.process_resources;

    ASSERT_EQ(dependencies.headers.size(), 1u);
    ASSERT_TRUE(dependencies.headers.front().self_parse_time.has_value());
    EXPECT_EQ(*dependencies.headers.front().self_parse_time, std::chrono::milliseconds(320));
    ASSERT_EQ(dependencies.metric_capabilities.size(), 1u);
    EXPECT_EQ(dependencies.metric_capabilities.front().metric, "frontend.source_self_time");

    EXPECT_EQ(modules.rules, 2u);
    EXPECT_EQ(modules.provided_modules, 2u);
    EXPECT_EQ(modules.required_modules, 1u);
    EXPECT_EQ(modules.resolved_dependencies, 1u);
    ASSERT_EQ(modules.dependencies.size(), 1u);
    const std::pair<std::string, std::string> expected_module_edge{"M", "User"};
    EXPECT_EQ(modules.dependencies.front(), expected_module_edge);
    ASSERT_EQ(modules.metric_capabilities.size(), 1u);
    EXPECT_EQ(modules.metric_capabilities.front().metric, "module.dependency_graph");

    EXPECT_EQ(process_resources.observations, 2u);
    EXPECT_EQ(process_resources.total_process_time, std::chrono::milliseconds(101));
    EXPECT_EQ(process_resources.total_user_time, std::chrono::milliseconds(92));
    EXPECT_EQ(process_resources.peak_memory_kib, 87536u);
    ASSERT_EQ(process_resources.metric_capabilities.size(), 1u);
    EXPECT_EQ(process_resources.metric_capabilities.front().metric, "process.resource_counters");

    EXPECT_EQ(cache.compile_requests, 12u);
    EXPECT_EQ(cache.executed_compilations, 10u);
    EXPECT_EQ(cache.non_compilation_requests, 1u);
    EXPECT_EQ(cache.unsupported_compiler_requests, 1u);
    EXPECT_EQ(cache.compilations, 10u);
    EXPECT_EQ(cache.cache_hits, 7u);
    EXPECT_EQ(cache.cache_misses, 3u);
    EXPECT_EQ(cache.cache_errors, 1u);
    EXPECT_EQ(cache.cache_timeouts, 2u);
    EXPECT_EQ(cache.cache_read_errors, 3u);
    EXPECT_EQ(cache.non_cacheable_compilations, 1u);
    EXPECT_EQ(cache.forced_recaches, 2u);
    EXPECT_EQ(cache.cache_write_errors, 1u);
    EXPECT_EQ(cache.cache_writes, 8u);
    EXPECT_EQ(cache.compilation_failures, 1u);
    ASSERT_TRUE(cache.hit_rate_percent.has_value());
    EXPECT_DOUBLE_EQ(*cache.hit_rate_percent, 70.0);
    ASSERT_EQ(cache.metric_capabilities.size(), 1u);
    EXPECT_EQ(cache.metric_capabilities.front().metric, "cache.outcomes");

    const auto& session = load_result.value().analysis.build_session;
    EXPECT_EQ(session.timed_commands, 10u);
    EXPECT_EQ(session.total_commands, 12u);
    EXPECT_EQ(session.wall_clock_time, std::chrono::milliseconds(5000));
    EXPECT_EQ(session.serial_time, std::chrono::milliseconds(9000));
    EXPECT_EQ(session.peak_parallelism, 4u);
    EXPECT_DOUBLE_EQ(session.average_parallelism, 1.8);
    EXPECT_EQ(session.critical_path_time, std::chrono::milliseconds(4200));
    ASSERT_EQ(session.critical_path.size(), 2u);
    EXPECT_EQ(session.critical_path.front(), "compile-a");
    ASSERT_EQ(session.step_metrics.size(), 2u);
    EXPECT_EQ(session.step_metrics.front().role, BuildStepRole::Custom);
    EXPECT_EQ(session.step_metrics.front().wall_clock_time, std::chrono::milliseconds(700));
    EXPECT_EQ(session.step_metrics.front().failed_commands, 1u);
    EXPECT_EQ(session.step_metrics.back().role, BuildStepRole::Test);
    EXPECT_EQ(session.step_metrics.back().successful_commands, 1u);
    EXPECT_EQ(session.host_telemetry.memory_samples, 4u);
    ASSERT_TRUE(session.host_telemetry.peak_memory_used_kib.has_value());
    EXPECT_EQ(*session.host_telemetry.peak_memory_used_kib, 4096u);
    EXPECT_EQ(session.host_telemetry.cpu_load_samples, 6u);
    ASSERT_TRUE(session.host_telemetry.peak_before_cpu_load_average.has_value());
    EXPECT_DOUBLE_EQ(*session.host_telemetry.peak_before_cpu_load_average, 2.5);
    ASSERT_TRUE(session.host_telemetry.peak_after_cpu_load_average.has_value());
    EXPECT_DOUBLE_EQ(*session.host_telemetry.peak_after_cpu_load_average, 3.5);
    ASSERT_EQ(session.metric_capabilities.size(), 1u);
    EXPECT_EQ(session.metric_capabilities.front().metric, "build.command.wall_time");
    EXPECT_EQ(session.metric_capabilities.front().provenance.capture_mode, "api-v1-index");

    const auto& linker = load_result.value().analysis.linker;
    EXPECT_EQ(linker.invocations, 2u);
    EXPECT_EQ(linker.timed_invocations, 1u);
    EXPECT_EQ(linker.output_size_observations, 1u);
    EXPECT_EQ(linker.wall_clock_time, std::chrono::milliseconds(2200));
    EXPECT_EQ(linker.output_bytes, 8192u);
    ASSERT_TRUE(linker.trace_wall_clock_time.has_value());
    EXPECT_EQ(*linker.trace_wall_clock_time, std::chrono::milliseconds(2400));
    ASSERT_TRUE(linker.lto_time.has_value());
    EXPECT_EQ(*linker.lto_time, std::chrono::milliseconds(1100));
    const auto& targets = load_result.value().analysis.targets;
    EXPECT_EQ(targets.target_commands, 1u);
    EXPECT_EQ(targets.matched_commands, 1u);
    ASSERT_EQ(targets.targets.size(), 1u);
    EXPECT_EQ(targets.targets.front().id, "app-id");
    EXPECT_EQ(targets.targets.front().dependencies, std::vector<std::string>{"lib-id"});
    EXPECT_EQ(targets.targets.front().compile_wall_clock_time, std::chrono::milliseconds(1200));
    EXPECT_EQ(targets.targets.front().output_bytes, 2048u);
    EXPECT_EQ(targets.pch_targets, 1u);
    EXPECT_EQ(targets.pch_headers, 1u);
    ASSERT_EQ(targets.targets.front().precompile_headers.size(), 1u);
    EXPECT_EQ(targets.targets.front().precompile_headers.front(), fs::path("/src/pch.h"));
    ASSERT_EQ(linker.metric_capabilities.size(), 1u);
    EXPECT_EQ(linker.metric_capabilities.front().metric, "lto.wall_time");
    EXPECT_EQ(linker.metric_capabilities.front().provenance.evidence, EvidenceKind::Observed);

    ASSERT_EQ(load_result.value().analysis.metric_capabilities.size(), 1u);
    const auto& loaded_capability = load_result.value().analysis.metric_capabilities.front();
    EXPECT_EQ(loaded_capability.metric, "compile.translation_unit.wall_time");
    EXPECT_EQ(loaded_capability.provenance.evidence, EvidenceKind::Observed);
    EXPECT_EQ(loaded_capability.provenance.producer, "clang");
    EXPECT_EQ(loaded_capability.provenance.timing_domain, TimingDomain::WallClock);
    EXPECT_EQ(loaded_capability.provenance.timing_aggregation, TimingAggregation::Exclusive);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(StorageSnapshotTest, PersistsSuggestionHotspotOrigins) {
    namespace fs = std::filesystem;

    const auto unique = std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    const fs::path root = fs::temp_directory_path() / ("bha-hotspot-origin-" + unique);

    SnapshotStore store(root);
    analyzers::AnalysisResult analysis;
    analysis.performance.total_build_time = std::chrono::milliseconds(100);

    Suggestion suggestion;
    suggestion.id = "hotspot-origin";
    suggestion.type = SuggestionType::ExplicitTemplate;
    suggestion.title = "Use an explicit template declaration";
    suggestion.confidence = 0.9;
    suggestion.is_safe = true;

    HotspotOrigin origin;
    origin.kind = "template_origin";
    origin.source = "include/widget.hpp";
    origin.target = "Widget<int>";
    origin.estimated_cost = std::chrono::milliseconds(12);
    origin.chain = {"template: Widget<int>", "used in: src/main.cpp"};
    origin.note = "Observed per-specialization timing matched to a validated Clang AST record.";
    suggestion.hotspot_origins.push_back(origin);

    const auto save_result = store.save("origin-metrics", analysis, {suggestion});
    ASSERT_TRUE(save_result.is_ok());

    const auto load_result = store.load("origin-metrics");
    ASSERT_TRUE(load_result.is_ok());
    ASSERT_EQ(load_result.value().suggestions.size(), 1u);
    ASSERT_EQ(load_result.value().suggestions.front().hotspot_origins.size(), 1u);
    const auto& loaded_origin = load_result.value().suggestions.front().hotspot_origins.front();
    EXPECT_EQ(loaded_origin.kind, "template_origin");
    EXPECT_EQ(loaded_origin.chain.size(), 2u);
    EXPECT_EQ(loaded_origin.note, "Observed per-specialization timing matched to a validated Clang AST record.");

    std::error_code ec;
    fs::remove_all(root, ec);
}

}  // namespace bha::storage::test
