    void SuggestionManager::evict_old_backups() {
        // Evict by count limit
        while (config_.max_backups > 0 && backups_.size() > config_.max_backups && !backup_lru_.empty()) {
            const auto oldest_id = backup_lru_.front();
            backup_lru_.pop_front();
            if (config_.use_disk_backups && !config_.workspace_root.empty()) {
                cleanup_disk_backup(oldest_id);
            }
            backups_.erase(oldest_id);
        }

        // Evict by size limit
        while (config_.max_backup_bytes > 0 && calculate_backup_size() > config_.max_backup_bytes && !backup_lru_.empty()) {
            const auto oldest_id = backup_lru_.front();
            backup_lru_.pop_front();
            if (config_.use_disk_backups && !config_.workspace_root.empty()) {
                cleanup_disk_backup(oldest_id);
            }
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
            for (const auto& file : backup.files) {
                total += file.content.size();
            }
        }
        return total;
    }

    AnalysisResult SuggestionManager::analyze_project(
        const fs::path& project_root,
        const std::optional<fs::path>& build_dir,
        const std::optional<fs::path>& trace_dir,
        bool rebuild,
        const ProgressCallback& on_progress,
        const AnalyzeSuggestionOptions& analyze_options,
        const std::function<bool()>& is_cancelled
    ) {
        auto start = std::chrono::steady_clock::now();
        std::map<std::string, std::int64_t> phase_timings;
        const auto record_phase = [&phase_timings](
            const std::string_view name,
            const std::chrono::steady_clock::time_point phase_start
        ) {
            phase_timings[std::string(name)] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - phase_start
            ).count();
        };

        auto report = [&on_progress](const std::string& msg, const int pct) {
            if (on_progress) {
                on_progress(msg, pct);
            }
        };
        const auto report_elapsed = [&report](
            const std::string_view phase,
            const std::chrono::steady_clock::time_point phase_start,
            const int pct
        ) {
            report(
                std::string(phase) + " completed in " +
                    format_elapsed_ms(std::chrono::steady_clock::now() - phase_start),
                pct
            );
        };
        const auto ensure_not_cancelled = [&is_cancelled]() {
            if (is_cancelled && is_cancelled()) {
                throw std::runtime_error("Operation cancelled");
            }
        };

        ensure_not_cancelled();
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

        std::optional<fs::path> compile_commands_path;
        if (build_dir) {
            const fs::path direct_compile_commands = *build_dir / "compile_commands.json";
            if (fs::exists(direct_compile_commands)) {
                compile_commands_path = direct_compile_commands;
            }
        }

        if (rebuild) {
            ensure_not_cancelled();
            report("Running build...", 10);
            if (auto build_result = adapter->build(project_root, options); !build_result.is_ok() || !build_result.value().success) {
                throw std::runtime_error("Build failed");
            } else {
                phase_timings["build"] = build_result.value().build_time.count();
            }
        }

        ensure_not_cancelled();
        report("Loading compile commands...", 20);
        auto phase_start = std::chrono::steady_clock::now();
        if (auto compile_commands_result = adapter->get_compile_commands(project_root, options); !compile_commands_result.is_ok()) {
            if (!config_.allow_missing_compile_commands) {
                throw std::runtime_error("Could not find compile_commands.json");
            }
        } else {
            compile_commands_path = compile_commands_result.value();
        }
        report_elapsed("Loading compile commands", phase_start, 25);
        record_phase("compile_commands", phase_start);

        ensure_not_cancelled();
        report("Parsing trace files...", 30);
        phase_start = std::chrono::steady_clock::now();
        BuildTrace build_trace;
        build_trace.timestamp = std::chrono::system_clock::now();
        build_trace.build_system = detect_build_system_from_build_dir(build_dir);
        if (build_trace.build_system == BuildSystemType::Unknown && adapter) {
            build_trace.build_system = build_system_type_from_adapter_name(adapter->name());
        }

        const fs::path traces_dir = trace_dir.has_value() && !trace_dir->empty()
            ? *trace_dir
            : resolve_trace_root(project_root, build_dir);
        int files_analyzed = 0;

        auto should_skip_unit = [](const fs::path& source_path) {
            const auto filename = source_path.filename().string();
            if (filename.find("cmake_pch") != std::string::npos) {
                return true;
            }
            if (filename.find("CMakeCXXCompilerId") != std::string::npos) {
                return true;
            }
            if (filename.find("CompilerId") != std::string::npos) {
                return true;
            }
            return false;
        };

        if (fs::exists(traces_dir)) {
            const auto trace_files = parsers::collect_trace_files(traces_dir, true);
            constexpr std::size_t kParseBatchSize = 64;
            for (std::size_t offset = 0; offset < trace_files.size(); offset += kParseBatchSize) {
                ensure_not_cancelled();
                const auto batch_end = std::min(offset + kParseBatchSize, trace_files.size());
                const std::vector<fs::path> batch(trace_files.begin() + static_cast<std::ptrdiff_t>(offset),
                                                  trace_files.begin() + static_cast<std::ptrdiff_t>(batch_end));
                auto parse_results = parsers::parse_trace_files(batch);
                for (auto& parse_result : parse_results) {
                    ensure_not_cancelled();
                    if (parse_result.is_ok()) {
                        auto unit = std::move(parse_result.value());
                        if (should_skip_unit(unit.source_file)) {
                            continue;
                        }
                        build_trace.total_time += unit.metrics.total_time;
                        build_trace.units.push_back(std::move(unit));
                        files_analyzed++;
                    }
                }
                report(
                    "Parsed " + std::to_string(std::min(batch_end, trace_files.size())) + "/" +
                        std::to_string(trace_files.size()) + " trace files...",
                    30 + static_cast<int>((10 * batch_end) / std::max<std::size_t>(trace_files.size(), 1))
                );
            }
        }
        report(
            "Parsed " + std::to_string(files_analyzed) + " trace files in " +
                format_elapsed_ms(std::chrono::steady_clock::now() - phase_start),
            40
        );
        record_phase("trace_parse", phase_start);

        if (build_trace.units.empty()) {
            throw std::runtime_error("No trace files found");
        }

        const auto path_resolution_start = std::chrono::steady_clock::now();
        if (!project_root.empty()) {
            std::unordered_map<std::string, std::optional<fs::path>> resolve_cache;
            resolve_cache.reserve(build_trace.units.size() * 2);

            for (auto& unit : build_trace.units) {
                if (path_utils::is_under(unit.source_file, traces_dir)) {
                    if (auto resolved = resolve_trace_file_to_project(project_root, unit.source_file, resolve_cache)) {
                        unit.source_file = *resolved;
                        unit.metrics.path = unit.source_file;
                    }
                }

                for (auto& inc : unit.includes) {
                    if (path_utils::is_under(inc.header, traces_dir)) {
                        if (auto resolved = resolve_trace_file_to_project(project_root, inc.header, resolve_cache)) {
                            inc.header = *resolved;
                        }
                    }
                }
            }
        }

        if (compile_commands_path.has_value()) {
            auto compile_sources = collect_compile_commands_sources(*compile_commands_path, 10000);
            if (!compile_sources.empty()) {
                const auto by_filename = index_sources_by_filename(compile_sources);
                for (auto& unit : build_trace.units) {
                    if (unit.source_file.is_relative() || !fs::exists(unit.source_file)) {
                        if (auto resolved = resolve_trace_source_with_compile_commands(
                            unit.source_file,
                            project_root,
                            by_filename
                        )) {
                            unit.source_file = *resolved;
                            unit.metrics.path = *resolved;
                        }
                    }
                }
            }
        }
        record_phase("path_and_index", path_resolution_start);

        ensure_not_cancelled();
        report("Running analyzers...", 50);
        phase_start = std::chrono::steady_clock::now();

        AnalysisOptions analysis_opts;
        auto analysis_result = analyzers::run_full_analysis(build_trace, analysis_opts);

        if (!analysis_result.is_ok()) {
            throw std::runtime_error("Analysis failed: " + analysis_result.error().message());
        }
        report_elapsed("Running analyzers", phase_start, 65);
        record_phase("analysis", phase_start);

        ensure_not_cancelled();
        report("Generating suggestions...", 70);
        phase_start = std::chrono::steady_clock::now();
        const auto suggestion_phase_start = phase_start;

        // Configure suggester options
        SuggesterOptions suggester_opts;
        suggester_opts.min_confidence = config_.min_confidence;
        suggester_opts.include_unsafe = config_.include_unsafe_suggestions;
        suggester_opts.enable_consolidation = true;
        suggester_opts.compile_commands_path = compile_commands_path;
        suggester_opts.max_include_cleanup_scan_files =
            config_.enable_expensive_include_cleanup_fallbacks ? 25 : 0;
        suggester_opts.max_include_move_candidate_headers =
            config_.enable_expensive_include_cleanup_fallbacks ? 100 : 0;
        suggester_opts.heuristics.forward_decl.max_candidate_headers = 25;
        suggester_opts.heuristics.templates.max_candidate_instantiations = 25;
        suggester_opts.on_suggester_completed =
            [&report](const std::string_view name, const Duration elapsed, const std::size_t count) {
                report(
                    "Suggester " + std::string(name) + " produced " +
                        std::to_string(count) + " suggestions in " + format_elapsed_ms(elapsed),
                    70
                );
            };
        if (!analyze_options.enabled_types.empty()) {
            suggester_opts.enabled_types = analyze_options.enabled_types;
        }
        if (analyze_options.include_unsafe.has_value()) {
            suggester_opts.include_unsafe = *analyze_options.include_unsafe;
        }
        if (analyze_options.min_confidence.has_value()) {
            suggester_opts.min_confidence = *analyze_options.min_confidence;
        }
        if (analyze_options.enable_consolidation.has_value()) {
            suggester_opts.enable_consolidation = *analyze_options.enable_consolidation;
        }
        if (analyze_options.relax_heuristics) {
            auto& pch = suggester_opts.heuristics.pch;
            auto& templates = suggester_opts.heuristics.templates;
            auto& headers = suggester_opts.heuristics.headers;
            auto& unity = suggester_opts.heuristics.unity_build;
            auto& forward_decl = suggester_opts.heuristics.forward_decl;
            auto& codegen = suggester_opts.heuristics.codegen;
            auto& unreal = suggester_opts.heuristics.unreal;
            pch.min_include_count = 1;
            pch.min_aggregate_time = std::chrono::milliseconds(1);
            templates.min_instantiation_count = 1;
            templates.min_total_time = std::chrono::milliseconds(1);
            headers.min_parse_time = std::chrono::milliseconds(1);
            headers.min_includers_for_split = 1;
            unity.min_files_threshold = 2;
            unity.min_group_total_time = std::chrono::milliseconds(1);
            forward_decl.min_parse_time = std::chrono::milliseconds(1);
            codegen.long_codegen_threshold = std::chrono::milliseconds(1);
            unreal.min_module_files_for_unity = 1;
            unreal.min_module_include_time_for_pch = std::chrono::milliseconds(1);
        }
        if (should_force_unreal_mode(project_root, build_trace)) {
            suggester_opts.heuristics.unreal.enabled = true;
        }

        // Generate suggestions using all registered suggesters
        auto suggestions_result = suggestions::generate_all_suggestions(
            build_trace,
            analysis_result.value(),
            suggester_opts,
            project_root
        );

        if (!suggestions_result.is_ok()) {
            throw std::runtime_error("Suggestion generation failed: " + suggestions_result.error().message());
        }

        auto bha_suggestions = std::move(suggestions_result.value());
        report(
            "Generated " + std::to_string(bha_suggestions.size()) + " suggestions in " +
                format_elapsed_ms(std::chrono::steady_clock::now() - phase_start),
            85
        );

        bool has_include_removal = std::ranges::any_of(
            bha_suggestions,
            [](const bha::Suggestion& suggestion) {
                return suggestion.type == bha::SuggestionType::IncludeRemoval;
            }
        );
        if (config_.enable_expensive_include_cleanup_fallbacks && !has_include_removal) {
            phase_start = std::chrono::steady_clock::now();
            report("Running include-cleanup fallback...", 86);
            auto cli_include_removals = load_include_removal_suggestions_via_cli(project_root, traces_dir);
            if (!cli_include_removals.empty()) {
                for (auto& suggestion : cli_include_removals) {
                    bha_suggestions.push_back(std::move(suggestion));
                }
                has_include_removal = true;
            }
            report_elapsed("Include-cleanup CLI fallback", phase_start, 88);
        }
        if (config_.enable_expensive_include_cleanup_fallbacks && !has_include_removal && compile_commands_path.has_value()) {
            phase_start = std::chrono::steady_clock::now();
            report("Running verified include-cleanup fallback...", 88);
            if (auto verified_include = build_verified_include_removal_suggestion(
                *compile_commands_path,
                project_root,
                build_trace,
                config_.protected_include_patterns
            )) {
                bha_suggestions.push_back(std::move(*verified_include));
            }
            report_elapsed("Verified include-cleanup fallback", phase_start, 90);
        }
        record_phase("suggestion_generation", suggestion_phase_start);

        ensure_not_cancelled();
        if (!project_root.empty()) {
            for (auto& sug : bha_suggestions) {
                sug.target_file.path = resolve_relative_path(sug.target_file.path, project_root);
                for (auto& secondary : sug.secondary_files) {
                    secondary.path = resolve_relative_path(secondary.path, project_root);
                }
                for (auto& edit : sug.edits) {
                    edit.file = resolve_relative_path(edit.file, project_root);
                    if (edit.file.filename() == "pch.h" && !edit.new_text.empty()) {
                        edit.new_text = normalize_pch_text(edit.new_text);
                    }
                }

                if (is_build_system_file(sug.target_file.path)) {
                    sug.target_file.path = remap_build_system_path(sug.target_file.path, project_root);
                }
                for (auto& secondary : sug.secondary_files) {
                    if (is_build_system_file(secondary.path)) {
                        secondary.path = remap_build_system_path(secondary.path, project_root);
                    }
                }
                for (auto& edit : sug.edits) {
                    if (is_build_system_file(edit.file)) {
                        edit.file = remap_build_system_path(edit.file, project_root);
                    }
                }
            }
        }

        std::optional<fs::path> compile_commands_for_validation;
        if (compile_commands_path.has_value()) {
            fs::path compile_commands = *compile_commands_path;
            if (compile_commands.is_relative() && !project_root.empty()) {
                compile_commands = (project_root / compile_commands).lexically_normal();
            } else {
                compile_commands = compile_commands.lexically_normal();
            }
            compile_commands_for_validation = compile_commands;
        }

        const std::optional<fs::path> project_root_for_validation =
            project_root.empty() ? std::optional<fs::path>{} : std::optional<fs::path>{project_root};

        enforce_pch_auto_apply_validation_readiness(
            bha_suggestions,
            compile_commands_for_validation,
            build_trace,
            project_root_for_validation,
            config_.enforce_compile_command_syntax_gate
        );

        ensure_not_cancelled();
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
        last_compile_commands_path_ = compile_commands_for_validation;
        last_project_root_ = project_root;
        last_build_dir_ = build_dir;
        last_trace_dir_ = trace_dir;
        last_analyze_options_ = analyze_options;

        // Evict old analysis cache entries if over limit
        evict_old_analysis_cache();

        report("Finalizing results...", 95);

        AnalysisResult result;
        result.analysis_id = analysis_id;
        result.suggestions = std::move(lsp_suggestions);
        result.baseline_metrics = extract_build_metrics(analysis_cache_[analysis_id]);
        result.files_analyzed = files_analyzed;
        result.duration_ms = duration_ms;
        result.phase_timings_ms = std::move(phase_timings);

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
