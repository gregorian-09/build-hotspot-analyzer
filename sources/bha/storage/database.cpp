//
// Created by gregorian on 25/10/2025.
//

#include "bha/storage/database.h"
#include <chrono>
#include <sstream>

namespace bha::storage {

    Database::Database(std::unique_ptr<DatabaseBackend> backend)
        : backend_(std::move(backend)) {}

    core::Result<void> Database::initialize() const
    {
        return backend_->initialize();
    }

    core::Result<void> Database::close() const
    {
        return backend_->close();
    }

    core::Result<std::string> Database::store_build_trace(const core::BuildTrace& trace) const
    {
        if (auto txn_result = backend_->begin_transaction(); !txn_result.is_success()) {
            return core::Result<std::string>::failure(txn_result.error());
        }

        auto build_record = trace_to_record(trace);
        if (auto store_result = backend_->store_build(build_record); !store_result.is_success()) {
            backend_->rollback_transaction();
            return core::Result<std::string>::failure(store_result.error());
        }

        auto units = units_to_records(trace, build_record.id);
        if (auto units_result = backend_->store_compilation_units(units); !units_result.is_success()) {
            backend_->rollback_transaction();
            return core::Result<std::string>::failure(units_result.error());
        }

        auto deps = graph_to_records(trace.dependency_graph, build_record.id);
        if (auto deps_result = backend_->store_dependencies(deps); !deps_result.is_success()) {
            backend_->rollback_transaction();
            return core::Result<std::string>::failure(deps_result.error());
        }

        auto hotspots = hotspots_to_records(trace.metrics, build_record.id);
        if (auto hotspots_result = backend_->store_hotspots(hotspots); !hotspots_result.is_success()) {
            backend_->rollback_transaction();
            return core::Result<std::string>::failure(hotspots_result.error());
        }

        if (auto commit_result = backend_->commit_transaction(); !commit_result.is_success()) {
            return core::Result<std::string>::failure(commit_result.error());
        }

        return core::Result<std::string>::success(build_record.id);
    }

    core::Result<std::optional<BuildRecord>> Database::get_baseline(
        const std::string& branch) const
    {

        auto builds_result = backend_->list_builds(1, branch);
        if (!builds_result.is_success()) {
            return core::Result<std::optional<BuildRecord>>::failure(
                builds_result.error());
        }

        auto builds = builds_result.value();
        if (builds.empty()) {
            return core::Result<std::optional<BuildRecord>>::success(std::nullopt);
        }

        return core::Result<std::optional<BuildRecord>>::success(builds[0]);
    }

    core::Result<ComparisonResult> Database::compare_with_baseline(
        const core::BuildTrace& current_trace,
        const std::string& branch) const
    {

        auto baseline_result = get_baseline(branch);
        if (!baseline_result.is_success()) {
            return core::Result<ComparisonResult>::failure(baseline_result.error());
        }

        const auto& baseline_opt = baseline_result.value();
        if (!baseline_opt.has_value()) {
            return core::Result<ComparisonResult>::failure(core::Error(
                core::ErrorCode::NOT_FOUND,
                "No baseline build found for branch: " + branch
            ));
        }

        auto store_result = store_build_trace(current_trace);
        if (!store_result.is_success()) {
            return core::Result<ComparisonResult>::failure(store_result.error());
        }

        return backend_->compare_builds(
            baseline_opt.value().id,
            store_result.value());
    }

    core::Result<std::vector<BuildRecord>> Database::get_recent_builds(const int limit) const
    {
        return backend_->list_builds(limit, "");
    }

    core::Result<void> Database::cleanup(const int retention_days) const
    {
        return backend_->cleanup_old_builds(retention_days);
    }

    BuildRecord Database::trace_to_record(const core::BuildTrace& trace) {
        const auto now = std::chrono::system_clock::now();
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        std::stringstream id_stream;
        id_stream << trace.commit_sha << "_"
                  << trace.configuration << "_"
                  << timestamp;

        return BuildRecord{
            .id = id_stream.str(),
            .timestamp = timestamp,
            .commit_sha = trace.commit_sha,
            .branch = trace.branch,
            .configuration = trace.configuration,
            .platform = trace.platform,
            .build_system = trace.build_system,
            .total_time_ms = trace.total_build_time_ms,
            .is_clean_build = trace.is_clean_build,
            .file_count = static_cast<int>(trace.compilation_units.size())
        };
    }

    std::vector<CompilationRecord> Database::units_to_records(
        const core::BuildTrace& trace,
        const std::string& build_id) {

        std::vector<CompilationRecord> records;
        records.reserve(trace.compilation_units.size());

        for (const auto& unit : trace.compilation_units) {
            records.push_back(CompilationRecord{
                .build_id = build_id,
                .file_path = unit.file_path,
                .total_time_ms = unit.total_time_ms,
                .preprocessing_time_ms = unit.preprocessing_time_ms,
                .parsing_time_ms = unit.parsing_time_ms,
                .codegen_time_ms = unit.codegen_time_ms,
                .file_size_bytes = unit.file_size_bytes
            });
        }

        return records;
    }

    std::vector<DependencyRecord> Database::graph_to_records(
        const core::DependencyGraph& graph,
        const std::string& build_id) {

        std::vector<DependencyRecord> records;

        for (const auto& [source, edges] : graph.get_adjacency_list()) {
            for (const auto& edge : edges) {
                records.push_back(DependencyRecord{
                    .build_id = build_id,
                    .source_file = source,
                    .target_file = edge.target,
                    .is_direct = edge.type == core::EdgeType::DIRECT_INCLUDE,
                    .line_number = edge.line_number
                });
            }
        }

        return records;
    }

    std::vector<HotspotRecord> Database::hotspots_to_records(
        const core::MetricsSummary& metrics,
        const std::string& build_id) {

        std::vector<HotspotRecord> records;
        records.reserve(metrics.top_slow_files.size());

        for (const auto& [file_path, time_ms, impact_score, num_dependent_files, category] : metrics.top_slow_files) {
            records.push_back(HotspotRecord{
                .build_id = build_id,
                .file_path = file_path,
                .time_ms = time_ms,
                .impact_score = impact_score,
                .num_dependents = num_dependent_files,
                .category = category
            });
        }

        return records;
    }

    core::Result<std::optional<core::BuildTrace>> Database::load_build_trace(
        const std::string& build_id) const
    {
        auto build_result = backend_->get_build(build_id);
        if (!build_result.is_success()) {
            return core::Result<std::optional<core::BuildTrace>>::failure(
                build_result.error());
        }

        const auto& build_opt = build_result.value();
        if (!build_opt.has_value()) {
            return core::Result<std::optional<core::BuildTrace>>::success(std::nullopt);
        }

        const auto& build = build_opt.value();

        auto units_result = backend_->get_compilation_units(build_id);
        if (!units_result.is_success()) {
            return core::Result<std::optional<core::BuildTrace>>::failure(
                units_result.error());
        }

        auto deps_result = backend_->get_dependencies(build_id);
        if (!deps_result.is_success()) {
            return core::Result<std::optional<core::BuildTrace>>::failure(
                deps_result.error());
        }

        core::BuildTrace trace;
        trace.commit_sha = build.commit_sha;
        trace.branch = build.branch;
        trace.configuration = build.configuration;
        trace.platform = build.platform;
        trace.build_system = build.build_system;
        trace.total_build_time_ms = build.total_time_ms;
        trace.is_clean_build = build.is_clean_build;

        trace.compilation_units.reserve(units_result.value().size());
        for (const auto& record : units_result.value()) {
            core::CompilationUnit unit;
            unit.file_path = record.file_path;
            unit.total_time_ms = record.total_time_ms;
            unit.preprocessing_time_ms = record.preprocessing_time_ms;
            unit.parsing_time_ms = record.parsing_time_ms;
            unit.codegen_time_ms = record.codegen_time_ms;
            unit.file_size_bytes = record.file_size_bytes;
            trace.compilation_units.push_back(unit);
        }

        for (const auto& dep : deps_result.value()) {
            core::DependencyEdge edge(
                dep.target_file,
                dep.is_direct ? core::EdgeType::DIRECT_INCLUDE : core::EdgeType::TRANSITIVE
            );
            edge.line_number = dep.line_number;
            trace.dependency_graph.add_edge(dep.source_file, edge);
        }

        return core::Result<std::optional<core::BuildTrace>>::success(trace);
    }

} // namespace bha::storage