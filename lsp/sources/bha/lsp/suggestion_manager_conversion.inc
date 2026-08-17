    bool SuggestionManager::apply_file_changes(const bha::Suggestion& suggestion, std::vector<fs::path>& changed_files) {
        if (!suggestion.edits.empty()) {
            std::unordered_map<std::string, std::vector<bha::TextEdit>> edits_by_file;
            for (const auto& edit : suggestion.edits) {
                bha::TextEdit normalized = edit;
                if (edit.file.filename() == "pch.h" && !edit.new_text.empty()) {
                    normalized.new_text = normalize_pch_text(edit.new_text);
                }
                edits_by_file[normalized.file.string()].push_back(std::move(normalized));
            }

            for (auto& [file_path_str, file_edits] : edits_by_file) {
                fs::path file_path(file_path_str);

                // For new files, create them first
                if (!fs::exists(file_path)) {
                    if (const fs::path parent = file_path.parent_path(); !parent.empty()) {
                        fs::create_directories(parent);
                    }
                    std::ofstream out(file_path);
                    if (!out) {
                        return false;
                    }
                    // Write empty file, edits will add content
                    out.close();
                }

                if (!apply_edits_to_file(file_path, std::move(file_edits))) {
                    return false;
                }
                if (file_path.filename() == "pch.h") {
                    std::ifstream in(file_path);
                    if (in) {
                        std::string content(
                            (std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>()
                        );
                        in.close();
                        if (!content.empty()) {
                            std::ofstream out(file_path);
                            if (out) {
                                out << normalize_pch_text(content);
                            }
                        }
                    }
                }
                changed_files.push_back(file_path);
            }

            return true;
        }

        // Fallback: apply using FileTarget actions (for suggestions without precise edits)
        auto apply_file_target = [&](const FileTarget& target) -> bool {
            try {
                if (target.action == FileAction::Create) {
                    if (fs::exists(target.path)) {
                        return false;
                    }
                    if (const fs::path parent = target.path.parent_path(); !parent.empty()) {
                        fs::create_directories(parent);
                    }
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
                        (std::istreambuf_iterator(in)),
                        std::istreambuf_iterator<char>()
                    );
                    in.close();

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

    const bha::Suggestion* SuggestionManager::get_bha_suggestion(const std::string& id) const {
        if (const auto it = bha_suggestions_.find(id); it != bha_suggestions_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::optional<BuildMetrics> SuggestionManager::get_last_baseline_metrics() const {
        if (last_analysis_id_.empty()) {
            return std::nullopt;
        }
        const auto analysis_it = analysis_cache_.find(last_analysis_id_);
        if (analysis_it == analysis_cache_.end()) {
            return std::nullopt;
        }
        return extract_build_metrics(analysis_it->second);
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
        lsp_sug.id = bha_sug.id;

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
        lsp_sug.auto_applicable = is_auto_applicable_suggestion(bha_sug);
        lsp_sug.application_mode = std::string(
            bha::to_string(bha::resolve_application_mode(bha_sug))
        );
        if (bha_sug.refactor_class_name) {
            lsp_sug.refactor_class_name = *bha_sug.refactor_class_name;
        }
        if (bha_sug.refactor_compile_commands_path) {
            lsp_sug.compile_commands_path = bha_sug.refactor_compile_commands_path->string();
        }
        lsp_sug.application_summary = format_application_summary(bha_sug);
        lsp_sug.application_guidance = format_application_guidance(bha_sug);
        lsp_sug.auto_apply_blocked_reason = format_auto_apply_blocked_reason(bha_sug);
        if (is_unreal_suggestion(bha_sug)) {
            lsp_sug.project_context = "unreal";
            std::unordered_set<std::string> seen_module_rules;
            std::unordered_set<std::string> seen_target_rules;
            const auto add_file = [&](const fs::path& path) {
                const std::string normalized = path.lexically_normal().string();
                if (is_unreal_module_rules_file(path)) {
                    if (seen_module_rules.insert(normalized).second) {
                        lsp_sug.module_rules_files.push_back(normalized);
                    }
                }
                if (is_unreal_target_rules_file(path)) {
                    if (seen_target_rules.insert(normalized).second) {
                        lsp_sug.target_rules_files.push_back(normalized);
                    }
                }
            };
            add_file(bha_sug.target_file.path);
            for (const auto& file : bha_sug.secondary_files) {
                add_file(file.path);
            }
            lsp_sug.safety_guard = infer_unreal_safety_guard(bha_sug);
        }

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
            lsp_sug.target_uri = uri::path_to_uri(bha_sug.target_file.path);

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
        detailed.application_summary = format_application_summary(bha_sug);
        detailed.application_guidance = format_application_guidance(bha_sug);
        detailed.auto_apply_blocked_reason = format_auto_apply_blocked_reason(bha_sug);

        return detailed;
    }

    ApplyAllResult SuggestionManager::apply_all_suggestions(
        const std::optional<std::string>& min_priority,
        const bool safe_only,
        const std::function<void(const std::string&)>& progress_log
    ) {
        struct PendingSuggestion {
            std::string key;
        };

        ApplyAllResult result;
        result.success = true;
        result.applied_count = 0;
        result.skipped_count = 0;

        const auto priority_threshold = parse_priority_threshold(min_priority);

        std::vector<fs::path> all_files_to_backup;
        std::vector<PendingSuggestion> pending;
        std::unordered_set<std::string> seen_pending_keys;

        for (const auto& [id, bha_sug] : bha_suggestions_) {
            if (!is_apply_all_candidate_enabled(bha_sug, priority_threshold, safe_only)) {
                result.skipped_count++;
                continue;
            }

            const std::string key = stable_suggestion_key(bha_sug);
            if (!seen_pending_keys.insert(key).second) {
                continue;
            }
            pending.push_back(PendingSuggestion{key});

            for (const auto& file : collect_backup_files(bha_sug)) {
                all_files_to_backup.push_back(file);
            }
        }

        {
            const auto index = build_apply_all_candidate_index(
                bha_suggestions_,
                priority_threshold,
                safe_only
            );
            std::ranges::sort(
                pending,
                [&](const PendingSuggestion& lhs, const PendingSuggestion& rhs) {
                    const auto lhs_it = index.find(lhs.key);
                    const auto rhs_it = index.find(rhs.key);
                    if (lhs_it == index.end() && rhs_it == index.end()) {
                        return lhs.key < rhs.key;
                    }
                    if (lhs_it == index.end()) {
                        return false;
                    }
                    if (rhs_it == index.end()) {
                        return true;
                    }
                    return is_higher_ranked_suggestion(lhs_it->second, rhs_it->second, bha_suggestions_);
                }
            );
        }

        // Single backup for all changes
        if (!all_files_to_backup.empty()) {
            result.backup_id = create_backup(all_files_to_backup);
            if (result.backup_id.empty()) {
                result.success = false;
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Failed to create durable backup before apply-all operation";
                result.errors.push_back(std::move(diag));
                return result;
            }
        }

        std::unordered_set<std::string> changed_file_set;
        bool rerank_remaining = config_.rerank_remaining_after_each_apply &&
            last_project_root_.has_value() && !last_project_root_->empty();

        while (!pending.empty()) {
            auto key_index = build_apply_all_candidate_index(
                bha_suggestions_,
                priority_threshold,
                safe_only
            );
            pending.erase(
                std::remove_if(
                    pending.begin(),
                    pending.end(),
                    [&](const PendingSuggestion& item) {
                        if (key_index.contains(item.key)) {
                            return false;
                        }
                        result.skipped_count++;
                        return true;
                    }
                ),
                pending.end()
            );
            if (pending.empty()) {
                break;
            }

            auto best_it = pending.begin();
            std::string best_id = key_index.at(best_it->key);
            for (auto it = std::next(pending.begin()); it != pending.end(); ++it) {
                const std::string candidate_id = key_index.at(it->key);
                if (is_higher_ranked_suggestion(candidate_id, best_id, bha_suggestions_)) {
                    best_it = it;
                    best_id = candidate_id;
                }
            }

            const std::string selected_key = best_it->key;
            pending.erase(best_it);

            auto apply_result = apply_suggestion(best_id, false, true, false);
            if (apply_result.success) {
                merge_apply_all_success(result, best_id, apply_result, changed_file_set);

                if (!pending.empty() && rerank_remaining) {
                    try {
                        if (progress_log) {
                            progress_log(
                                "Re-analyzing project to rerank remaining suggestions after applying " + best_id
                            );
                        }
                        analyze_project(
                            *last_project_root_,
                            last_build_dir_,
                            last_trace_dir_,
                            true,
                            nullptr,
                            last_analyze_options_
                        );
                        if (progress_log) {
                            progress_log("Rerank analysis complete");
                        }
                    } catch (const std::exception&) {
                        if (progress_log) {
                            progress_log("Rerank analysis failed; stopping remaining apply operations");
                        }
                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.source = "bha-lsp";
                        diag.message =
                            "Stopped apply-all after a successful edit because fresh trace analysis "
                            "for remaining suggestions failed; stale savings were not applied.";
                        result.errors.push_back(std::move(diag));
                        result.skipped_count += pending.size();
                        pending.clear();
                        rerank_remaining = false;
                    }
                }
            } else {
                // Validation "apply blocked" diagnostics indicate missing compile-command
                // evidence for safe auto-apply in this workspace, not a code-edit failure.
                // Treat these as skipped suggestions for apply-all so one unverifiable item
                // does not fail the whole batch.
                if (is_compile_command_validation_blocked_result(apply_result)) {
                    result.skipped_count++;
                    continue;
                }
                merge_apply_all_failure(result, apply_result);
            }
        }

        result.success = result.errors.empty();
        return result;
    }

    RevertResult SuggestionManager::revert_changes_detailed(const std::string& backup_id, const bool preserve_backup) {
        RevertResult result;
        result.success = true;

        const auto it = backups_.find(backup_id);
        const bool is_disk_backup = config_.use_disk_backups && !config_.workspace_root.empty() &&
                                    fs::exists(get_backup_path(backup_id));

        if (it == backups_.end() && !is_disk_backup) {
            result.success = false;
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Invalid backup ID: " + backup_id;
            result.errors.push_back(diag);
            return result;
        }

        if (is_disk_backup) {
            const fs::path backup_path = get_backup_path(backup_id);
            auto metadata = read_backup_metadata(backup_path);
            if (!metadata) {
                result.success = false;
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "Failed to read backup metadata";
                result.errors.push_back(diag);
                return result;
            }

            for (const auto& file : metadata->files) {
                if (!file.existed_before) {
                    std::error_code remove_ec;
                    fs::remove(file.path, remove_ec);
                    if (remove_ec) {
                        result.success = false;
                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.message = "Failed to remove created file during rollback: " + file.path.string();
                        result.errors.push_back(diag);
                    } else {
                        result.restored_files.push_back(file.path.string());
                    }
                    continue;
                }

                const fs::path relative = fs::relative(file.path, config_.workspace_root);
                const fs::path src = backup_path / "files" / relative;

                try {
                    if (!fs::exists(src)) {
                        result.success = false;
                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.message = "Backup file not found: " + src.string();
                        result.errors.push_back(diag);
                        continue;
                    }
                    if (const fs::path parent = file.path.parent_path(); !parent.empty()) {
                        fs::create_directories(parent);
                    }
                    fs::copy_file(src, file.path, fs::copy_options::overwrite_existing);
                    result.restored_files.push_back(file.path.string());
                } catch (const std::exception& e) {
                    result.success = false;
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.message = "Error restoring " + file.path.string() + ": " + e.what();
                    result.errors.push_back(diag);
                }
            }

            if (result.success && !preserve_backup && !config_.keep_backups) {
                cleanup_disk_backup(backup_id);
            }
        } else if (it != backups_.end()) {
            for (const auto& backup = it->second; const auto& file : backup.files) {
                try {
                    if (!file.existed_before) {
                        std::error_code remove_ec;
                        fs::remove(file.path, remove_ec);
                        if (remove_ec) {
                            result.success = false;
                            Diagnostic diag;
                            diag.severity = DiagnosticSeverity::Error;
                            diag.message = "Failed to remove created file during rollback: " + file.path.string();
                            result.errors.push_back(diag);
                        } else {
                            result.restored_files.push_back(file.path.string());
                        }
                    } else if (std::ofstream out(file.path, std::ios::binary); !out) {
                        result.success = false;
                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.message = "Failed to restore file: " + file.path.string();
                        result.errors.push_back(diag);
                    } else {
                        out << file.content;
                        result.restored_files.push_back(file.path.string());
                    }
                } catch (const std::exception& e) {
                    result.success = false;
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.message = "Error restoring " + file.path.string() + ": " + e.what();
                    result.errors.push_back(diag);
                }
            }
        }

        if (result.success && !preserve_backup && it != backups_.end()) {
            backups_.erase(it);
            backup_lru_.remove(backup_id);
        }
        return result;
    }

    std::vector<BackupSummary> SuggestionManager::list_backups() const {
        std::map<std::string, BackupSummary> summaries;

        for (const auto& [id, backup] : backups_) {
            summaries[id] = BackupSummary{
                .id = id,
                .timestamp = backup.timestamp,
                .file_count = backup.files.size(),
                .on_disk = config_.use_disk_backups && !config_.workspace_root.empty() &&
                    fs::exists(get_backup_path(id))
            };
        }

        if (config_.use_disk_backups && !config_.workspace_root.empty()) {
            const fs::path backup_root = (config_.workspace_root / config_.backup_directory).lexically_normal();
            std::error_code ec;
            if (fs::exists(backup_root, ec) && fs::is_directory(backup_root, ec)) {
                for (const auto& entry : fs::directory_iterator(backup_root, ec)) {
                    if (ec || !entry.is_directory()) {
                        continue;
                    }

                    const auto metadata = read_backup_metadata(entry.path());
                    if (!metadata.has_value() || metadata->id.empty()) {
                        continue;
                    }

                    auto& summary = summaries[metadata->id];
                    summary.id = metadata->id;
                    summary.timestamp = metadata->timestamp;
                    summary.file_count = metadata->files.size();
                    summary.on_disk = true;
                }
            }
        }

        std::vector<BackupSummary> result;
        result.reserve(summaries.size());
        for (const auto& summary : summaries | std::views::values) {
            result.push_back(summary);
        }

        std::ranges::sort(
            result,
            [](const BackupSummary& lhs, const BackupSummary& rhs) {
                if (lhs.timestamp != rhs.timestamp) {
                    return lhs.timestamp > rhs.timestamp;
                }
                return lhs.id > rhs.id;
            }
        );

        return result;
    }
