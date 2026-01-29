#include "bha/lsp/suggestion_manager.hpp"
#include "bha/build_systems/adapter.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/suggestions/all_suggesters.hpp"
#include "bha/suggestions/consolidator.hpp"
#include <chrono>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace bha::lsp
{
    SuggestionManager::SuggestionManager(const SuggestionManagerConfig& config)
        : config_(config)
    {
        suggestions::register_all_suggesters();
    }

    void SuggestionManager::evict_old_backups() {
        // Evict by count limit
        while (config_.max_backups > 0 && backups_.size() > config_.max_backups && !backup_lru_.empty()) {
            const auto oldest_id = backup_lru_.front();
            backup_lru_.pop_front();
            backups_.erase(oldest_id);
        }

        // Evict by size limit
        while (config_.max_backup_bytes > 0 && calculate_backup_size() > config_.max_backup_bytes && !backup_lru_.empty()) {
            const auto oldest_id = backup_lru_.front();
            backup_lru_.pop_front();
            backups_.erase(oldest_id);
        }
    }

    void SuggestionManager::evict_old_analysis_cache() {
        while (config_.max_analysis_cache > 0 && analysis_cache_.size() > config_.max_analysis_cache && !analysis_lru_.empty()) {
            const auto oldest_id = analysis_lru_.front();
            analysis_lru_.pop_front();
            analysis_cache_.erase(oldest_id);
        }
    }

    std::size_t SuggestionManager::calculate_backup_size() const {
        std::size_t total = 0;
        for (const auto& backup : backups_ | std::views::values) {
            for (const auto& [path, content] : backup.files) {
                total += content.size();
            }
        }
        return total;
    }

    AnalysisResult SuggestionManager::analyze_project(
        const fs::path& project_root,
        const std::optional<fs::path>& build_dir,
        bool rebuild,
        const ProgressCallback& on_progress
    ) {
        auto start = std::chrono::steady_clock::now();

        auto report = [&on_progress](const std::string& msg, const int pct) {
            if (on_progress) on_progress(msg, pct);
        };

        report("Detecting build system...", 5);

        auto& registry = build_systems::BuildSystemRegistry::instance();
        auto* adapter = registry.detect(project_root);

        if (!adapter) {
            throw std::runtime_error("Could not detect build system");
        }

        build_systems::BuildOptions options;
        options.enable_tracing = true;
        if (build_dir) {
            options.build_dir = *build_dir;
        }

        if (rebuild) {
            report("Running build...", 10);
            if (auto build_result = adapter->build(project_root, options); !build_result.is_ok() || !build_result.value().success) {
                throw std::runtime_error("Build failed");
            }
        }

        report("Loading compile commands...", 20);
        if (auto compile_commands_result = adapter->get_compile_commands(project_root, options); !compile_commands_result.is_ok()) {
            throw std::runtime_error("Could not find compile_commands.json");
        }

        report("Parsing trace files...", 30);
        BuildTrace build_trace;
        build_trace.timestamp = std::chrono::system_clock::now();

        fs::path traces_dir = build_dir.value_or(project_root / "build");
        int files_analyzed = 0;

        if (fs::exists(traces_dir)) {
            for (const auto& entry : fs::recursive_directory_iterator(traces_dir)) {
                if (!entry.is_regular_file()) continue;

                if (entry.path().extension() == ".json") {
                    if (auto parse_result = parsers::parse_trace_file(entry.path()); parse_result.is_ok()) {
                        build_trace.units.push_back(std::move(parse_result.value()));
                        build_trace.total_time += parse_result.value().metrics.total_time;
                        files_analyzed++;
                    }
                }
            }
        }

        if (build_trace.units.empty()) {
            throw std::runtime_error("No trace files found");
        }

        report("Running analyzers...", 50);

        AnalysisOptions analysis_opts;
        auto analysis_result = analyzers::run_full_analysis(build_trace, analysis_opts);

        if (!analysis_result.is_ok()) {
            throw std::runtime_error("Analysis failed: " + analysis_result.error().message());
        }

        report("Generating suggestions...", 70);

        // Configure suggester options
        SuggesterOptions suggester_opts;
        suggester_opts.min_confidence = 0.5;
        suggester_opts.include_unsafe = false;
        suggester_opts.enable_consolidation = true;

        // Generate suggestions using all registered suggesters
        auto suggestions_result = suggestions::generate_all_suggestions(
            build_trace,
            analysis_result.value(),
            suggester_opts
        );

        if (!suggestions_result.is_ok()) {
            throw std::runtime_error("Suggestion generation failed: " + suggestions_result.error().message());
        }

        report("Consolidating suggestions...", 85);

        // Consolidate related suggestions
        suggestions::SuggestionConsolidator consolidator;
        auto bha_suggestions = consolidator.consolidate(std::move(suggestions_result.value()));

        // Convert bha::Suggestion to lsp::Suggestion
        suggestions_.clear();
        bha_suggestions_.clear();
        std::vector<Suggestion> lsp_suggestions;

        for (auto& bha_sug : bha_suggestions) {

            std::string sug_id = generate_analysis_id();
            bha_sug.id = sug_id;

            bha_suggestions_[sug_id] = bha_sug;
            auto lsp_sug = convert_suggestion(bha_sug);
            lsp_suggestions.push_back(lsp_sug);

            suggestions_[sug_id] = convert_to_detailed(bha_sug);
        }

        auto end = std::chrono::steady_clock::now();
        auto duration_count = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        int duration_ms = static_cast<int>(duration_count);

        std::string analysis_id = generate_analysis_id();
        last_analysis_id_ = analysis_id;
        analysis_cache_[analysis_id] = std::move(build_trace);
        analysis_lru_.push_back(analysis_id);

        // Evict old analysis cache entries if over limit
        evict_old_analysis_cache();

        report("Finalizing results...", 95);

        AnalysisResult result;
        result.analysis_id = analysis_id;
        result.suggestions = std::move(lsp_suggestions);
        result.baseline_metrics = extract_build_metrics(analysis_cache_[analysis_id]);
        result.files_analyzed = files_analyzed;
        result.duration_ms = duration_ms;

        report("Analysis complete", 100);

        return result;
    }

    DetailedSuggestion SuggestionManager::get_suggestion_details(const std::string& suggestion_id) {
        const auto it = suggestions_.find(suggestion_id);
        if (it == suggestions_.end()) {
            throw std::runtime_error("Invalid suggestion ID: " + suggestion_id);
        }
        return it->second;
    }

    ApplySuggestionResult SuggestionManager::apply_suggestion(
        const std::string& suggestion_id,
        bool /*skip_validation*/,
        bool skip_rebuild,
        bool create_backup_flag
    ) {
        ApplySuggestionResult result;
        result.success = false;

        auto bha_it = bha_suggestions_.find(suggestion_id);
        if (bha_it == bha_suggestions_.end()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Invalid suggestion ID: " + suggestion_id;
            result.errors.push_back(diag);
            return result;
        }

        const auto& bha_sug = bha_it->second;

        std::vector<fs::path> files_to_backup;
        if (bha_sug.target_file.action == FileAction::Modify ||
            bha_sug.target_file.action == FileAction::AddInclude) {
            files_to_backup.push_back(bha_sug.target_file.path);
            }
        for (const auto& secondary : bha_sug.secondary_files) {
            if (secondary.action == bha::FileAction::Modify ||
                secondary.action == bha::FileAction::AddInclude) {
                files_to_backup.push_back(secondary.path);
                }
        }

        if (create_backup_flag && !files_to_backup.empty()) {
            result.backup_id = create_backup(files_to_backup);
        }

        std::vector<fs::path> changed_files;
        if (!apply_file_changes(bha_sug, changed_files)) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Failed to apply file changes";
            result.errors.push_back(diag);

            if (result.backup_id) {
                revert_changes(*result.backup_id);
            }
            return result;
        }

        for (const auto& file : changed_files) {
            result.changed_files.push_back(file.string());
        }

        if (!skip_rebuild && !last_analysis_id_.empty()) {
            auto& analysis = analysis_cache_[last_analysis_id_];
            auto& registry = build_systems::BuildSystemRegistry::instance();

            if (!analysis.units.empty()) {
                fs::path project_root = analysis.units[0].source_file.parent_path();
                while (project_root.has_parent_path() && !fs::exists(project_root / "CMakeLists.txt") &&
                       !fs::exists(project_root / "Makefile") && !fs::exists(project_root / "meson.build")) {
                    project_root = project_root.parent_path();
                       }

                if (auto* adapter = registry.detect(project_root)) {
                    build_systems::BuildOptions options;

                    if (auto build_result = adapter->build(project_root, options); build_result.is_ok() && build_result.value().success) {
                        BuildResult lsp_build_result;
                        lsp_build_result.success = true;
                        result.build_result = lsp_build_result;
                    } else {
                        BuildResult lsp_build_result;
                        lsp_build_result.success = false;
                        result.build_result = lsp_build_result;

                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.message = "Build failed after applying suggestion";
                        result.errors.push_back(diag);

                        if (result.backup_id) {
                            revert_changes(*result.backup_id);
                        }
                        return result;
                    }
                }
            }
        }

        result.success = true;
        return result;
    }

    ApplySuggestionResult SuggestionManager::apply_all_suggestions(
        const std::vector<std::string>& suggestion_ids,
        const bool stop_on_error
    ) {
        ApplySuggestionResult combined_result;
        combined_result.success = true;

        for (const auto& id : suggestion_ids) {
            if (auto result = apply_suggestion(id); !result.success) {
                combined_result.success = false;
                combined_result.errors.insert(combined_result.errors.end(),
                                            result.errors.begin(),
                                            result.errors.end());
                if (stop_on_error) {
                    break;
                }
            } else {
                combined_result.changed_files.insert(combined_result.changed_files.end(),
                                                   result.changed_files.begin(),
                                                   result.changed_files.end());
            }
        }

        return combined_result;
    }

    bool SuggestionManager::revert_changes(const std::string& backup_id) {
        const auto it = backups_.find(backup_id);
        if (it == backups_.end()) {
            return false;
        }

        for (const auto& backup = it->second; const auto& [path, content] : backup.files) {
            try {
                std::ofstream out(path, std::ios::binary);
                if (!out) {
                    return false;
                }
                out << content;
            } catch (...) {
                return false;
            }
        }

        backups_.erase(it);
        backup_lru_.remove(backup_id);
        return true;
    }

    std::string SuggestionManager::create_backup(const std::vector<fs::path>& files) {
        Backup backup;
        backup.id = generate_backup_id();
        backup.timestamp = std::chrono::system_clock::now();

        for (const auto& file : files) {
            if (fs::exists(file)) {
                FileBackup file_backup;
                file_backup.path = file;

                if (std::ifstream in(file, std::ios::binary); in) {
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    file_backup.content = ss.str();
                    backup.files.push_back(std::move(file_backup));
                }
            }
        }

        backups_[backup.id] = std::move(backup);
        backup_lru_.push_back(backup.id);

        evict_old_backups();

        return backup.id;
    }

    bool SuggestionManager::validate_files_exist(const std::vector<fs::path>& files) {
        return std::ranges::all_of(files, [](const auto& file) {
            return fs::exists(file);
        });
    }

    bool SuggestionManager::apply_file_changes(const bha::Suggestion& suggestion, std::vector<fs::path>& changed_files) {
        auto apply_file_target = [&](const FileTarget& target) -> bool {
            try {
                if (target.action == FileAction::Create) {
                    if (fs::exists(target.path)) {
                        return false;
                    }
                    fs::create_directories(target.path.parent_path());
                    std::ofstream out(target.path);
                    if (!out) {
                        return false;
                    }
                    if (!suggestion.after_code.code.empty()) {
                        out << suggestion.after_code.code;
                    }
                    changed_files.push_back(target.path);
                }
                else if (target.action == FileAction::Modify) {
                    if (!fs::exists(target.path)) {
                        return false;
                    }
                    if (!suggestion.after_code.code.empty()) {
                        std::ofstream out(target.path);
                        if (!out) {
                            return false;
                        }
                        out << suggestion.after_code.code;
                        changed_files.push_back(target.path);
                    }
                }
                else if (target.action == FileAction::AddInclude) {
                    if (!fs::exists(target.path)) {
                        return false;
                    }
                    std::ifstream in(target.path);
                    if (!in) {
                        return false;
                    }
                    std::string content(
                        (std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>()
                    );

                    if (target.note && !target.note->empty()) {
                        if (size_t first_include = content.find("#include"); first_include != std::string::npos) {
                            content.insert(first_include, *target.note + "\n");
                        } else {
                            content = *target.note + "\n" + content;
                        }
                        std::ofstream out(target.path);
                        if (!out) {
                            return false;
                        }
                        out << content;
                        changed_files.push_back(target.path);
                    }
                }
            } catch (...) {
                return false;
            }
            return true;
        };

        return apply_file_target(suggestion.target_file) &&
               std::ranges::all_of(
                   suggestion.secondary_files,
                   apply_file_target
               );
    }

    std::vector<Suggestion> SuggestionManager::get_all_suggestions() const {
        std::vector<Suggestion> result;
        result.reserve(suggestions_.size());
        for (const auto& detailed : suggestions_ | std::views::values) {
            result.push_back(detailed);
        }
        return result;
    }

    std::optional<Suggestion> SuggestionManager::get_suggestion(const std::string& id) const {
        if (const auto it = suggestions_.find(id); it != suggestions_.end()) {
            return static_cast<const Suggestion&>(it->second);
        }
        return std::nullopt;
    }


    BuildMetrics SuggestionManager::extract_build_metrics(const BuildTrace& trace) {
        BuildMetrics metrics;

        const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(trace.total_time).count();
        metrics.total_duration_ms = static_cast<int>(total_ms);
        metrics.files_compiled = static_cast<int>(trace.units.size());
        metrics.files_up_to_date = 0;

        std::vector<std::pair<std::string, int>> file_times;
        for (const auto& unit : trace.units) {
            const auto unit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(unit.metrics.total_time).count();
            file_times.emplace_back(unit.source_file.filename().string(), static_cast<int>(unit_ms));
        }

        std::ranges::sort(file_times,
                          [](const auto& a, const auto& b) { return a.second > b.second; });

        for (size_t i = 0; i < std::min(file_times.size(), static_cast<size_t>(10)); ++i) {
            BuildMetrics::FileMetric fm;
            fm.file = file_times[i].first;
            fm.duration_ms = file_times[i].second;
            fm.percentage = (static_cast<double>(file_times[i].second) / static_cast<double>(total_ms)) * 100.0;
            metrics.slowest_files.push_back(fm);
        }

        return metrics;
    }

    Priority SuggestionManager::calculate_priority(const double improvement_percentage) {
        if (improvement_percentage >= 20.0) {
            return Priority::High;
        }
        if (improvement_percentage >= 5.0) {
            return Priority::Medium;
        }
        return Priority::Low;
    }

    std::string SuggestionManager::generate_analysis_id() {
        return "ana-" + std::to_string(++analysis_counter_);
    }

    std::string SuggestionManager::generate_backup_id() {
        return "backup-" + std::to_string(++backup_counter_);
    }

    Suggestion SuggestionManager::convert_suggestion(const bha::Suggestion& bha_sug) {
        Suggestion lsp_sug{};

        switch (bha_sug.type) {
        case bha::SuggestionType::PCHOptimization:
            lsp_sug.type = SuggestionType::PrecompiledHeader;
            break;
        case bha::SuggestionType::HeaderSplit:
            lsp_sug.type = SuggestionType::HeaderSplit;
            break;
        case bha::SuggestionType::UnityBuild:
            lsp_sug.type = SuggestionType::UnityBuild;
            break;
        case bha::SuggestionType::ExplicitTemplate:
            lsp_sug.type = SuggestionType::TemplateOptimization;
            break;
        case bha::SuggestionType::IncludeRemoval:
            lsp_sug.type = SuggestionType::IncludeReduction;
            break;
        case bha::SuggestionType::ForwardDeclaration:
            lsp_sug.type = SuggestionType::ForwardDeclaration;
            break;
        case bha::SuggestionType::PIMPLPattern:
            lsp_sug.type = SuggestionType::PIMPLPattern;
            break;
        case bha::SuggestionType::MoveToCpp:
            lsp_sug.type = SuggestionType::MoveToCpp;
            break;
        }

        switch (bha_sug.priority) {
        case bha::Priority::Critical:
        case bha::Priority::High:
            lsp_sug.priority = Priority::High;
            break;
        case bha::Priority::Medium:
            lsp_sug.priority = Priority::Medium;
            break;
        case bha::Priority::Low:
            lsp_sug.priority = Priority::Low;
            break;
        }

        lsp_sug.title = bha_sug.title;
        lsp_sug.description = bha_sug.description;
        lsp_sug.confidence = bha_sug.confidence;
        lsp_sug.auto_applicable = bha_sug.is_safe;

        const auto savings_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                bha_sug.estimated_savings
            ).count();

        lsp_sug.estimated_impact.time_saved_ms = static_cast<int>(savings_ms);
        lsp_sug.estimated_impact.percentage = bha_sug.estimated_savings_percent;
        lsp_sug.estimated_impact.files_affected =
            static_cast<int>(bha_sug.impact.total_files_affected);

        const auto steps = bha_sug.implementation_steps.size();
        lsp_sug.estimated_impact.complexity =
            steps <= 2 ? Complexity::Simple :
            steps <= 5 ? Complexity::Moderate :
                         Complexity::Complex;

        // Populate source location from FileTarget
        if (!bha_sug.target_file.path.empty()) {
            lsp_sug.target_uri = "file://" + fs::absolute(bha_sug.target_file.path).string();

            if (bha_sug.target_file.has_line_range()) {
                Range range{};
                // LSP uses 0-based lines, BHA uses 1-based
                range.start.line = static_cast<int>(bha_sug.target_file.line_start) - 1;
                range.start.character = bha_sug.target_file.has_column_range()
                    ? static_cast<int>(bha_sug.target_file.col_start) - 1
                    : 0;
                range.end.line = static_cast<int>(bha_sug.target_file.line_end) - 1;
                range.end.character = bha_sug.target_file.has_column_range()
                    ? static_cast<int>(bha_sug.target_file.col_end) - 1
                    : 0;
                lsp_sug.range = range;
            }
        }

        return lsp_sug;
    }

    DetailedSuggestion SuggestionManager::convert_to_detailed(const bha::Suggestion& bha_sug) {
        DetailedSuggestion detailed;

        static_cast<Suggestion&>(detailed) = convert_suggestion(bha_sug);
        detailed.rationale = bha_sug.rationale;

        if (bha_sug.target_file.action == FileAction::Create) {
            detailed.files_to_create.push_back(bha_sug.target_file.path.string());
        }
        for (const auto& file : bha_sug.secondary_files) {
            if (file.action == FileAction::Create) {
                detailed.files_to_create.push_back(file.path.string());
            }
        }

        if (bha_sug.target_file.action == FileAction::Modify ||
            bha_sug.target_file.action == FileAction::AddInclude) {
            detailed.files_to_modify.push_back(bha_sug.target_file.path.string());
            }
        for (const auto& file : bha_sug.secondary_files) {
            if (file.action == FileAction::Modify ||
                file.action == FileAction::AddInclude) {
                detailed.files_to_modify.push_back(file.path.string());
                }
        }

        detailed.dependencies = bha_sug.implementation_steps;

        return detailed;
    }

    ApplyAllResult SuggestionManager::apply_all_suggestions(
        const std::optional<std::string>& min_priority,
        const bool safe_only
    ) {
        ApplyAllResult result;
        result.success = true;
        result.applied_count = 0;
        result.skipped_count = 0;

        std::optional<bha::Priority> priority_threshold;
        if (min_priority) {
            std::string prio_lower = *min_priority;
            std::ranges::transform(prio_lower, prio_lower.begin(), ::tolower);
            if (prio_lower == "critical") {
                priority_threshold = bha::Priority::Critical;
            } else if (prio_lower == "high") {
                priority_threshold = bha::Priority::High;
            } else if (prio_lower == "medium") {
                priority_threshold = bha::Priority::Medium;
            } else if (prio_lower == "low") {
                priority_threshold = bha::Priority::Low;
            }
        }

        std::vector<fs::path> all_files_to_backup;
        std::vector<std::string> ids_to_apply;

        for (const auto& [id, bha_sug] : bha_suggestions_) {
            if (safe_only && !bha_sug.is_safe) {
                result.skipped_count++;
                continue;
            }

            if (priority_threshold) {
                bool meets_threshold = false;
                switch (*priority_threshold) {
                    case bha::Priority::Low:
                        meets_threshold = true;
                        break;
                    case bha::Priority::Medium:
                        meets_threshold = (bha_sug.priority == bha::Priority::Medium ||
                                          bha_sug.priority == bha::Priority::High ||
                                          bha_sug.priority == bha::Priority::Critical);
                        break;
                    case bha::Priority::High:
                        meets_threshold = (bha_sug.priority == bha::Priority::High ||
                                          bha_sug.priority == bha::Priority::Critical);
                        break;
                    case bha::Priority::Critical:
                        meets_threshold = (bha_sug.priority == bha::Priority::Critical);
                        break;
                }
                if (!meets_threshold) {
                    result.skipped_count++;
                    continue;
                }
            }

            ids_to_apply.push_back(id);

            if (bha_sug.target_file.action == bha::FileAction::Modify ||
                bha_sug.target_file.action == bha::FileAction::AddInclude) {
                all_files_to_backup.push_back(bha_sug.target_file.path);
            }
            for (const auto& secondary : bha_sug.secondary_files) {
                if (secondary.action == bha::FileAction::Modify ||
                    secondary.action == bha::FileAction::AddInclude) {
                    all_files_to_backup.push_back(secondary.path);
                }
            }
        }

        // Single backup for all changes
        if (!all_files_to_backup.empty()) {
            result.backup_id = create_backup(all_files_to_backup);
        }

        for (const auto& id : ids_to_apply) {
            if (auto apply_result = apply_suggestion(id, false, true, false); apply_result.success) {
                result.applied_count++;
                result.changed_files.insert(result.changed_files.end(),
                                           apply_result.changed_files.begin(),
                                           apply_result.changed_files.end());
            } else {
                result.skipped_count++;
                result.errors.insert(result.errors.end(),
                                    apply_result.errors.begin(),
                                    apply_result.errors.end());
            }
        }

        result.success = result.errors.empty();
        return result;
    }

    RevertResult SuggestionManager::revert_changes_detailed(const std::string& backup_id) {
        RevertResult result;

        const auto it = backups_.find(backup_id);
        if (it == backups_.end()) {
            result.success = false;
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Invalid backup ID: " + backup_id;
            result.errors.push_back(diag);
            return result;
        }

        result.success = true;
        for (const auto& backup = it->second; const auto& [path, content] : backup.files) {
            try {
                if (std::ofstream out(path, std::ios::binary); !out) {
                    result.success = false;
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.message = "Failed to restore file: " + path.string();
                    result.errors.push_back(diag);
                } else {
                    out << content;
                    result.restored_files.push_back(path.string());
                }
            } catch (const std::exception& e) {
                result.success = false;
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "Error restoring " + path.string() + ": " + e.what();
                result.errors.push_back(diag);
            }
        }

        if (result.success) {
            backups_.erase(it);
            backup_lru_.remove(backup_id);
        }
        return result;
    }
}
