    bool SuggestionManager::capture_transactional_snapshot(
        const std::vector<fs::path>& files,
        std::vector<FileBackup>& snapshot,
        std::vector<Diagnostic>& errors
    ) {
        std::unordered_set<std::string> seen;
        for (const auto& file : files) {
            const fs::path normalized = file.lexically_normal();
            const std::string key = normalized.generic_string();
            if (!seen.insert(key).second) {
                continue;
            }

            if (!fs::exists(normalized)) {
                snapshot.push_back(FileBackup{
                    normalized,
                    false,
                    {}
                });
                continue;
            }

            std::ifstream in(normalized, std::ios::binary);
            if (!in) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Failed to capture rollback snapshot for " + normalized.string();
                errors.push_back(std::move(diag));
                return false;
            }

            snapshot.push_back(FileBackup{
                normalized,
                true,
                std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>())
            });
        }
        return true;
    }

    bool SuggestionManager::restore_transactional_snapshot(
        const std::vector<FileBackup>& snapshot,
        std::vector<Diagnostic>& errors
    ) {
        bool success = true;
        for (const auto& backup : snapshot) {
            try {
                if (!backup.existed_before) {
                    std::error_code remove_ec;
                    fs::remove(backup.path, remove_ec);
                    if (remove_ec) {
                        success = false;
                        Diagnostic diag;
                        diag.severity = DiagnosticSeverity::Error;
                        diag.source = "bha-lsp";
                        diag.message = "Failed to remove newly created file during rollback: " +
                            backup.path.string();
                        errors.push_back(std::move(diag));
                    }
                    continue;
                }

                if (const fs::path parent = backup.path.parent_path(); !parent.empty()) {
                    fs::create_directories(parent);
                }
                std::ofstream out(backup.path, std::ios::binary | std::ios::trunc);
                if (!out) {
                    success = false;
                    Diagnostic diag;
                    diag.severity = DiagnosticSeverity::Error;
                    diag.source = "bha-lsp";
                    diag.message = "Failed to restore snapshot file: " + backup.path.string();
                    errors.push_back(std::move(diag));
                    continue;
                }
                out << backup.content;
            } catch (const std::exception& e) {
                success = false;
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Exception while restoring snapshot for " + backup.path.string() + ": " + e.what();
                errors.push_back(std::move(diag));
            }
        }
        return success;
    }

    void SuggestionManager::append_changed_files(
        ApplySuggestionResult& result,
        const std::vector<fs::path>& changed_files
    ) {
        for (const auto& file : changed_files) {
            result.changed_files.push_back(file.string());
        }
    }

    std::optional<SuggestionManager::FileState> SuggestionManager::read_file_state(const fs::path& file) {
        FileState state;
        std::error_code ec;
        state.exists = fs::exists(file, ec);
        if (ec) {
            return std::nullopt;
        }
        if (!state.exists) {
            return state;
        }
        if (!fs::is_regular_file(file, ec) || ec) {
            return std::nullopt;
        }

        std::ifstream in(file, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }

        constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;
        std::uint64_t hash = kFnvOffsetBasis;
        std::array<char, 8192> buffer{};
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = in.gcount();
            if (count <= 0) {
                continue;
            }
            state.size += static_cast<std::uintmax_t>(count);
            for (std::streamsize index = 0; index < count; ++index) {
                hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
                hash *= kFnvPrime;
            }
        }

        if (!in.eof()) {
            return std::nullopt;
        }
        state.content_hash = hash;
        return state;
    }

    bool SuggestionManager::validate_suggestion_source_state(
        const bha::Suggestion& suggestion,
        std::vector<Diagnostic>& errors
    ) const {
        const auto base = last_project_root_.has_value()
            ? std::optional<fs::path>(*last_project_root_)
            : std::nullopt;
        for (const auto& file : collect_backup_files(suggestion)) {
            const fs::path normalized = normalize_path_for_match(file, base);
            if (normalized.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Apply blocked: suggestion contains an empty or invalid file path";
                errors.push_back(std::move(diag));
                continue;
            }

            const std::string key = normalized.generic_string();
            const auto expected = last_file_states_.find(key);
            if (expected == last_file_states_.end()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Apply blocked: no baseline file state was recorded for " + normalized.string();
                errors.push_back(std::move(diag));
                continue;
            }

            const auto current = read_file_state(normalized);
            if (!current.has_value() || *current != expected->second) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = "Apply blocked: file changed since analysis; re-run analysis for " +
                    normalized.string();
                errors.push_back(std::move(diag));
            }
        }
        return errors.empty();
    }

    void SuggestionManager::merge_apply_all_success(
        ApplyAllResult& result,
        const std::string& suggestion_id,
        const ApplySuggestionResult& apply_result,
        std::unordered_set<std::string>& changed_file_set
    ) {
        result.applied_count++;
        result.applied_suggestion_ids.push_back(suggestion_id);
        for (const auto& file : apply_result.changed_files) {
            if (changed_file_set.insert(file).second) {
                result.changed_files.push_back(file);
            }
        }
    }

    void SuggestionManager::merge_apply_all_failure(
        ApplyAllResult& result,
        const ApplySuggestionResult& apply_result
    ) {
        result.skipped_count++;
        result.errors.insert(
            result.errors.end(),
            apply_result.errors.begin(),
            apply_result.errors.end()
        );
    }

    bool is_compile_command_validation_blocked_diagnostic(const Diagnostic& diagnostic) {
        constexpr std::string_view blocked_marker = " apply blocked: ";
        return diagnostic.source == "bha-lsp" &&
            diagnostic.message.find(blocked_marker) != std::string::npos;
    }

    bool is_compile_command_validation_blocked_result(const ApplySuggestionResult& result) {
        return !result.errors.empty() &&
            std::ranges::all_of(result.errors, [](const Diagnostic& diagnostic) {
                return is_compile_command_validation_blocked_diagnostic(diagnostic);
            });
    }

    bool SuggestionManager::create_backup_for_files(
        const std::vector<fs::path>& files,
        const std::string_view failure_message,
        ApplySuggestionResult& result
    ) {
        if (files.empty()) {
            return true;
        }

        const std::string backup_id = create_backup(files);
        if (backup_id.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = std::string(failure_message);
            result.errors.push_back(std::move(diag));
            return false;
        }

        result.backup_id = backup_id;
        return true;
    }

    bool SuggestionManager::apply_external_refactor_suggestion(
        const std::string& /*suggestion_id*/,
        const bha::Suggestion& bha_sug,
        const bool create_backup_flag,
        ApplySuggestionResult& result,
        std::vector<fs::path>& changed_files
    ) {
        if (bha_sug.type != bha::SuggestionType::PIMPLPattern ||
            !bha_sug.refactor_class_name ||
            !bha_sug.refactor_compile_commands_path ||
            bha_sug.secondary_files.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "This external refactor suggestion is missing the metadata required to invoke bha-refactor";
            result.errors.push_back(diag);
            return false;
        }

        const fs::path refactor_binary = resolve_bha_refactor_binary();
        if (!fs::exists(refactor_binary)) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "bha-refactor binary was not found; build with BHA_BUILD_REFACTOR_TOOLS=ON or set BHA_REFACTOR";
            result.errors.push_back(diag);
            return false;
        }

        std::ostringstream cmd;
        cmd << shell_quote(refactor_binary.string())
            << " pimpl"
            << " --compile-commands " << shell_quote(bha_sug.refactor_compile_commands_path->string())
            << " --source " << shell_quote(bha_sug.target_file.path.string())
            << " --header " << shell_quote(bha_sug.secondary_files.front().path.string())
            << " --class " << shell_quote(*bha_sug.refactor_class_name)
            << " --dry-run --output-format json";

        std::array<char, 512> buffer{};
        std::string output;
        if (FILE* pipe = popen(cmd.str().c_str(), "r")) {
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                output.append(buffer.data());
            }
            pclose(pipe);
        } else {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Failed to launch bha-refactor";
            result.errors.push_back(diag);
            return false;
        }

        nlohmann::json refactor_json;
        try {
            refactor_json = nlohmann::json::parse(output);
        } catch (const nlohmann::json::exception&) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "bha-refactor returned invalid JSON";
            result.errors.push_back(diag);
            return false;
        }

        if (!refactor_json.value("success", false)) {
            if (refactor_json.contains("diagnostics") && refactor_json["diagnostics"].is_array()) {
                for (const auto& item : refactor_json["diagnostics"]) {
                    result.errors.push_back(parse_refactor_diagnostic(item));
                }
            }
            if (result.errors.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "bha-refactor did not produce an applicable refactor";
                diag.source = "bha-refactor";
                result.errors.push_back(diag);
            }
            return false;
        }

        std::map<std::string, std::vector<ExternalReplacement>> replacements_by_file;
        std::vector<fs::path> files_to_backup;
        if (refactor_json.contains("replacements") && refactor_json["replacements"].is_array()) {
            for (const auto& item : refactor_json["replacements"]) {
                if (!item.is_object()) {
                    continue;
                }
                ExternalReplacement replacement;
                replacement.file = item.value("file", "");
                replacement.offset = item.value("offset", std::size_t{0});
                replacement.length = item.value("length", std::size_t{0});
                replacement.replacement_text = item.value("replacement_text", "");
                if (replacement.file.empty()) {
                    continue;
                }
                const std::string file_key = replacement.file.lexically_normal().generic_string();
                if (!replacements_by_file.contains(file_key)) {
                    files_to_backup.push_back(replacement.file);
                }
                replacements_by_file[file_key].push_back(std::move(replacement));
            }
        }

        if (replacements_by_file.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "bha-refactor returned no replacements";
            result.errors.push_back(diag);
            return false;
        }

        if (create_backup_flag &&
            !create_backup_for_files(
                files_to_backup,
                "Failed to create durable backup before applying external refactor suggestion",
                result
            )) {
            return false;
        }

        for (auto& [file_path_str, file_replacements] : replacements_by_file) {
            const fs::path file_path(file_path_str);
            if (!apply_replacements_to_file(file_path, std::move(file_replacements))) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.message = "Failed to apply bha-refactor replacements to " + file_path.string();
                diag.source = "bha-refactor";
                result.errors.push_back(diag);
                if (result.backup_id) {
                    rollback_apply_suggestion(
                        result,
                        {},
                        "Automatic rollback failed after external refactor apply failure"
                    );
                }
                return false;
            }
            changed_files.push_back(file_path);
        }

        return true;
    }

    bool SuggestionManager::apply_direct_edit_suggestion(
        const std::string& suggestion_id,
        const bha::Suggestion& bha_sug,
        const bool create_backup_flag,
        const bool enforce_syntax_validation,
        ApplySuggestionResult& result,
        std::vector<FileBackup>& transactional_snapshot,
        std::vector<fs::path>& changed_files
    ) {
        if (bha_sug.type == bha::SuggestionType::PIMPLPattern && bha_sug.edits.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "PIMPL suggestions without concrete edits remain advisory-only";
            result.errors.push_back(diag);
            return false;
        }

        std::vector<fs::path> files_to_backup = collect_backup_files(bha_sug);

        if (create_backup_flag &&
            !create_backup_for_files(
                files_to_backup,
                "Failed to create durable backup before applying suggestion " + suggestion_id,
                result
            )) {
            return false;
        }
        if (enforce_syntax_validation && !files_to_backup.empty() &&
            !capture_transactional_snapshot(files_to_backup, transactional_snapshot, result.errors)) {
            return false;
        }

        if (!apply_file_changes(bha_sug, changed_files)) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Failed to apply file changes for suggestion " + suggestion_id +
                           " (" + bha_sug.title + ")";
            result.errors.push_back(diag);

            if (result.backup_id || (enforce_syntax_validation && !transactional_snapshot.empty())) {
                rollback_apply_suggestion(
                    result,
                    transactional_snapshot,
                    enforce_syntax_validation
                        ? "Automatic rollback failed after syntax-validated apply failure"
                        : "Automatic rollback failed after apply failure"
                );
            }
            return false;
        }

        return true;
    }

    bool SuggestionManager::rollback_apply_suggestion(
        ApplySuggestionResult& result,
        const std::vector<FileBackup>& transactional_snapshot,
        const std::string_view rollback_failure_message
    ) {
        bool rollback_success = true;
        if (result.backup_id) {
            if (auto revert = revert_changes_detailed(*result.backup_id); !revert.success) {
                rollback_success = false;
                for (auto& revert_error : revert.errors) {
                    result.errors.push_back(std::move(revert_error));
                }
            }
        } else if (!transactional_snapshot.empty()) {
            rollback_success = restore_transactional_snapshot(transactional_snapshot, result.errors);
        }

        if (!rollback_success) {
            Diagnostic rollback_diag;
            rollback_diag.severity = DiagnosticSeverity::Error;
            rollback_diag.message = std::string(rollback_failure_message);
            rollback_diag.source = "bha-lsp";
            result.errors.push_back(std::move(rollback_diag));
        }

        return rollback_success;
    }

    bool SuggestionManager::validate_post_apply_rebuild(ApplySuggestionResult& result) {
        if (last_analysis_id_.empty()) {
            return true;
        }

        auto& analysis = analysis_cache_[last_analysis_id_];
        auto& registry = build_systems::BuildSystemRegistry::instance();

        if (analysis.units.empty()) {
            return true;
        }

        const fs::path source_dir = analysis.units[0].source_file.parent_path();
        const auto project_root = detect_project_root_with_registered_adapters(source_dir, registry);
        build_systems::IBuildSystemAdapter* adapter =
            project_root.has_value() ? registry.detect(*project_root) : nullptr;

        if (!project_root.has_value() || adapter == nullptr) {
            return true;
        }

        build_systems::BuildOptions options;
        if (auto build_result = adapter->build(*project_root, options);
            build_result.is_ok() && build_result.value().success) {
            BuildResult lsp_build_result;
            lsp_build_result.success = true;
            result.build_result = lsp_build_result;
            return true;
        }

        BuildResult lsp_build_result;
        lsp_build_result.success = false;
        result.build_result = lsp_build_result;

        Diagnostic diag;
        diag.severity = DiagnosticSeverity::Error;
        diag.message = "Build failed after applying suggestion";
        result.errors.push_back(std::move(diag));
        return false;
    }

    std::optional<std::vector<fs::path>> SuggestionManager::collect_compile_command_validation_sources(
        const std::optional<fs::path>& compile_commands_path,
        const BuildTrace& analysis_trace,
        const std::optional<fs::path>& project_root,
        const bha::Suggestion& suggestion,
        const std::vector<fs::path>& changed_files,
        const std::string& validation_label,
        std::vector<Diagnostic>& errors
    ) {
        if (!compile_commands_path.has_value() || !fs::exists(*compile_commands_path)) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = validation_label +
                " apply blocked: compile_commands.json is unavailable for syntax validation";
            errors.push_back(std::move(diag));
            return std::nullopt;
        }

        std::unordered_set<std::string> touched_paths;
        const auto add_touched = [&touched_paths](const fs::path& path) {
            const fs::path normalized = normalize_path_for_match(path);
            if (!normalized.empty()) {
                touched_paths.insert(normalized.generic_string());
            }
        };

        for (const auto& file : changed_files) {
            add_touched(file);
        }
        add_touched(suggestion.target_file.path);
        for (const auto& secondary : suggestion.secondary_files) {
            add_touched(secondary.path);
        }

        if (touched_paths.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = validation_label +
                " apply blocked: could not determine affected files for validation";
            errors.push_back(std::move(diag));
            return std::nullopt;
        }

        const auto compile_sources = collect_compile_commands_sources(*compile_commands_path, 0);
        std::unordered_set<std::string> compile_source_set;
        compile_source_set.reserve(compile_sources.size());
        for (const auto& source : compile_sources) {
            compile_source_set.insert(source.generic_string());
        }
        const auto compile_sources_by_filename = index_sources_by_filename(compile_sources);

        std::vector<fs::path> candidate_sources;
        std::unordered_set<std::string> seen_sources;
        const auto add_candidate_source = [&](const fs::path& source) {
            const fs::path normalized = normalize_path_for_match(source);
            if (normalized.empty() || !is_cpp_source_path(normalized)) {
                return;
            }
            if (!compile_source_set.contains(normalized.generic_string())) {
                return;
            }
            if (seen_sources.insert(normalized.generic_string()).second) {
                candidate_sources.push_back(normalized);
            }
        };

        const fs::path normalized_project_root = project_root.value_or(fs::path{});
        if (suggestion.type == bha::SuggestionType::PCHOptimization) {
            const fs::path normalized_target_header = normalize_path_for_match(
                suggestion.target_file.path,
                project_root
            );
            if (normalized_target_header.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = validation_label +
                    " apply blocked: could not resolve the optimized header for validation";
                errors.push_back(std::move(diag));
                return std::nullopt;
            }

            for (const auto& unit : analysis_trace.units) {
                auto compile_backed_source = resolve_compile_command_backed_source(
                    unit.source_file,
                    normalized_project_root,
                    compile_source_set,
                    compile_sources_by_filename
                );
                if (!compile_backed_source.has_value()) {
                    continue;
                }

                const bool includes_target_header = std::any_of(
                    unit.includes.begin(),
                    unit.includes.end(),
                    [&](const IncludeInfo& include) {
                        const fs::path include_header = normalize_path_for_match(include.header, project_root);
                        return !include_header.empty() && include_header == normalized_target_header;
                    }
                );
                if (includes_target_header) {
                    add_candidate_source(*compile_backed_source);
                }
            }
        } else {
            std::vector<std::string> ordered_touched_paths(touched_paths.begin(), touched_paths.end());
            std::ranges::sort(ordered_touched_paths);
            for (const auto& touched : ordered_touched_paths) {
                add_candidate_source(fs::path(touched));
            }

            for (const auto& unit : analysis_trace.units) {
                auto compile_backed_source = resolve_compile_command_backed_source(
                    unit.source_file,
                    normalized_project_root,
                    compile_source_set,
                    compile_sources_by_filename
                );
                if (!compile_backed_source.has_value()) {
                    continue;
                }
                const fs::path& source = *compile_backed_source;

                bool impacted = touched_paths.contains(source.generic_string());
                if (!impacted) {
                    for (const auto& include : unit.includes) {
                        const fs::path header = normalize_path_for_match(include.header, project_root);
                        if (!header.empty() && touched_paths.contains(header.generic_string())) {
                            impacted = true;
                            break;
                        }
                    }
                }

                if (impacted) {
                    add_candidate_source(source);
                }
            }
        }

        if (candidate_sources.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            if (suggestion.type == bha::SuggestionType::PCHOptimization) {
                diag.message = validation_label +
                    " apply blocked: no compile-command-backed translation units that include the optimized header were found for syntax validation";
            } else {
                diag.message = validation_label +
                    " apply blocked: no affected compile-command-backed translation units were found for syntax validation";
            }
            errors.push_back(std::move(diag));
            return std::nullopt;
        }

        return candidate_sources;
    }

    void SuggestionManager::enforce_pch_auto_apply_validation_readiness(
        std::vector<bha::Suggestion>& suggestions,
        const std::optional<fs::path>& compile_commands_path,
        const BuildTrace& analysis_trace,
        const std::optional<fs::path>& project_root,
        const bool enforce_compile_command_syntax_gate
    ) {
        if (!enforce_compile_command_syntax_gate) {
            return;
        }

        for (auto& suggestion : suggestions) {
            if (suggestion.type != bha::SuggestionType::PCHOptimization ||
                bha::resolve_application_mode(suggestion) != bha::SuggestionApplicationMode::DirectEdits) {
                continue;
            }

            std::vector<Diagnostic> ignored_errors;
            const auto candidate_sources = collect_compile_command_validation_sources(
                compile_commands_path,
                analysis_trace,
                project_root,
                suggestion,
                collect_backup_files(suggestion),
                "PCH",
                ignored_errors
            );
            if (candidate_sources.has_value()) {
                continue;
            }

            bha::suggestions::downgrade_suggestion_to_manual_review(
                suggestion,
                "PCH auto-apply requires at least one compile-command-backed translation unit that includes the optimized header; none was found.",
                "Review the PCH candidate manually or regenerate traces from a build where compile_commands.json contains a source that includes the optimized header."
            );
        }
    }

    std::optional<fs::path> resolve_generated_pch_header_path(
        const bha::Suggestion& suggestion,
        const std::vector<fs::path>& changed_files
    ) {
        const auto is_header_path = [](const fs::path& path) {
            const std::string ext = to_lower_ascii(path.extension().string());
            return ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx";
        };
        const auto expected_name = suggestion.target_file.path.filename().string();
        if (!expected_name.empty()) {
            for (const auto& changed : changed_files) {
                if (normalize_path_for_match(changed).filename() == expected_name) {
                    return normalize_path_for_match(changed);
                }
            }
        }

        const auto is_pch_filename = [](const fs::path& path) {
            const std::string filename = to_lower_ascii(path.filename().string());
            return filename == "pch.h" || filename == "stdafx.h";
        };

        for (const auto& changed : changed_files) {
            const fs::path normalized = normalize_path_for_match(changed);
            if (!normalized.empty() && is_header_path(normalized) && is_pch_filename(normalized)) {
                return normalized;
            }
        }

        return std::nullopt;
    }

    bool validate_source_suggestion_against_compile_commands(
        const std::optional<fs::path>& compile_commands_path,
        const std::vector<fs::path>& candidate_sources,
        const std::string& validation_label,
        const int timeout_seconds,
        const std::size_t validation_cap,
        std::vector<Diagnostic>& errors
    ) {
        for (std::size_t i = 0; i < validation_cap; ++i) {
            const fs::path& source = candidate_sources[i];
            auto compile_args = load_compile_command_args_for_source(compile_commands_path, source);
            if (compile_args.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = validation_label + " apply blocked: no compile command found for " + source.string();
                errors.push_back(std::move(diag));
                return false;
            }

            const auto syntax_cmd = build_syntax_check_command(compile_args, source);
            if (!syntax_cmd.has_value()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = validation_label + " apply blocked: failed to construct syntax check command for " +
                    source.string();
                errors.push_back(std::move(diag));
                return false;
            }

            std::string syntax_output;
            const int exit_code = run_command_collect_output(*syntax_cmd, timeout_seconds, syntax_output);
            if (exit_code == 0) {
                continue;
            }

            return append_validation_command_failure(
                validation_label,
                source,
                timeout_seconds,
                exit_code,
                syntax_output,
                errors
            );
        }

        return true;
    }

    std::vector<fs::path> SuggestionManager::collect_generated_forward_headers(
        const bha::Suggestion& suggestion,
        const std::vector<fs::path>& changed_files
    ) {
        const auto is_generated_forward_header = [](const fs::path& path) {
            if (!bha::suggestions::is_header_file_path(path)) {
                return false;
            }
            const std::string stem = path.stem().string();
            return stem.size() > 4 && stem.ends_with("_fwd");
        };

        std::vector<fs::path> headers;
        std::unordered_set<std::string> seen;
        const auto add_header = [&](const fs::path& path) {
            const fs::path normalized = normalize_path_for_match(path);
            if (normalized.empty() || !is_generated_forward_header(normalized)) {
                return;
            }
            if (seen.insert(normalized.generic_string()).second) {
                headers.push_back(normalized);
            }
        };

        for (const auto& file : changed_files) {
            add_header(file);
        }
        for (const auto& edit : suggestion.edits) {
            add_header(edit.file);
        }
        for (const auto& secondary : suggestion.secondary_files) {
            add_header(secondary.path);
        }
        return headers;
    }

    bool SuggestionManager::validate_generated_forward_headers_against_compile_commands(
        const bha::Suggestion& suggestion,
        const std::optional<fs::path>& compile_commands_path,
        const std::vector<fs::path>& changed_files,
        const std::vector<fs::path>& candidate_sources,
        const std::string& validation_label,
        const int timeout_seconds,
        std::vector<Diagnostic>& errors
    ) {
        const auto forward_headers =
            SuggestionManager::collect_generated_forward_headers(suggestion, changed_files);
        if (forward_headers.empty()) {
            return true;
        }
        if (candidate_sources.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = validation_label +
                " apply blocked: no compile-command-backed source is available for generated forward-header validation";
            errors.push_back(std::move(diag));
            return false;
        }

        const fs::path& reference_source = candidate_sources.front();
        auto compile_args = load_compile_command_args_for_source(compile_commands_path, reference_source);
        if (compile_args.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = validation_label + " apply blocked: no compile command found for " +
                reference_source.string();
            errors.push_back(std::move(diag));
            return false;
        }

        for (const auto& forward_header : forward_headers) {
            const auto syntax_cmd = build_header_syntax_check_command(
                compile_args,
                reference_source,
                forward_header
            );
            if (!syntax_cmd.has_value()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = validation_label +
                    " apply blocked: failed to construct forward-header validation command for " +
                    forward_header.string();
                errors.push_back(std::move(diag));
                return false;
            }

            std::string syntax_output;
            const int exit_code = run_command_collect_output(*syntax_cmd, timeout_seconds, syntax_output);
            if (exit_code == 0) {
                continue;
            }

            return append_validation_command_failure(
                validation_label,
                forward_header,
                timeout_seconds,
                exit_code,
                syntax_output,
                errors
            );
        }

        return true;
    }

    bool validate_generated_pch_against_compile_commands(
        const bha::Suggestion& suggestion,
        const std::optional<fs::path>& compile_commands_path,
        const std::vector<fs::path>& changed_files,
        const std::vector<fs::path>& candidate_sources,
        const std::string& validation_label,
        const int timeout_seconds,
        const std::size_t validation_cap,
        std::vector<Diagnostic>& errors
    ) {
        const auto pch_header = resolve_generated_pch_header_path(suggestion, changed_files);
        if (!pch_header.has_value()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = validation_label +
                " apply blocked: could not resolve the generated precompiled header file for validation";
            errors.push_back(std::move(diag));
            return false;
        }

        for (std::size_t i = 0; i < validation_cap; ++i) {
            const fs::path& source = candidate_sources[i];
            auto compile_args = load_compile_command_args_for_source(compile_commands_path, source);
            if (compile_args.empty()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = validation_label + " apply blocked: no compile command found for " + source.string();
                errors.push_back(std::move(diag));
                return false;
            }

            const auto syntax_cmd = build_header_syntax_check_command(compile_args, source, *pch_header);
            if (!syntax_cmd.has_value()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.source = "bha-lsp";
                diag.message = validation_label +
                    " apply blocked: failed to construct precompiled-header validation command for " +
                    pch_header->string();
                errors.push_back(std::move(diag));
                return false;
            }

            std::string syntax_output;
            const int exit_code = run_command_collect_output(*syntax_cmd, timeout_seconds, syntax_output);
            if (exit_code == 0) {
                continue;
            }

            return append_validation_command_failure(
                validation_label,
                *pch_header,
                timeout_seconds,
                exit_code,
                syntax_output,
                errors
            );
        }

        return true;
    }

    bool SuggestionManager::validate_compile_command_backed_suggestion(
        const bha::Suggestion& suggestion,
        const std::vector<fs::path>& changed_files,
        std::vector<Diagnostic>& errors
    ) const {
        const std::string validation_label = validation_label_for_suggestion(suggestion);
        if (!config_.enforce_compile_command_syntax_gate) {
            return true;
        }

        if (last_analysis_id_.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = validation_label +
                " apply blocked: no prior analysis context is available for validation";
            errors.push_back(std::move(diag));
            return false;
        }

        const auto analysis_it = analysis_cache_.find(last_analysis_id_);
        if (analysis_it == analysis_cache_.end()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = validation_label +
                " apply blocked: cached analysis trace is no longer available";
            errors.push_back(std::move(diag));
            return false;
        }

        const auto candidate_sources = collect_compile_command_validation_sources(
            last_compile_commands_path_,
            analysis_it->second,
            last_project_root_,
            suggestion,
            changed_files,
            validation_label,
            errors
        );
        if (!candidate_sources.has_value()) {
            return false;
        }

        const std::size_t validation_cap = config_.max_compile_command_validation_units == 0
            ? candidate_sources->size()
            : std::min(candidate_sources->size(), config_.max_compile_command_validation_units);

        if (suggestion.type == bha::SuggestionType::PCHOptimization) {
            return validate_generated_pch_against_compile_commands(
                suggestion,
                last_compile_commands_path_,
                changed_files,
                *candidate_sources,
                validation_label,
                config_.compile_command_validation_timeout_seconds,
                validation_cap,
                errors
            );
        }

        if (suggestion.type == bha::SuggestionType::HeaderSplit &&
            !validate_generated_forward_headers_against_compile_commands(
                suggestion,
                last_compile_commands_path_,
                changed_files,
                *candidate_sources,
                validation_label,
                config_.compile_command_validation_timeout_seconds,
                errors
            )) {
            return false;
        }

        return validate_source_suggestion_against_compile_commands(
            last_compile_commands_path_,
            *candidate_sources,
            validation_label,
            config_.compile_command_validation_timeout_seconds,
            validation_cap,
            errors
        );
    }

    bool SuggestionManager::can_prepare_compile_command_validation(
        const bha::Suggestion& suggestion,
        std::vector<Diagnostic>& errors
    ) const {
        const std::string validation_label = validation_label_for_suggestion(suggestion);
        if (!config_.enforce_compile_command_syntax_gate) {
            return true;
        }

        if (last_analysis_id_.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = validation_label +
                " apply blocked: no prior analysis context is available for validation";
            errors.push_back(std::move(diag));
            return false;
        }

        const auto analysis_it = analysis_cache_.find(last_analysis_id_);
        if (analysis_it == analysis_cache_.end()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.source = "bha-lsp";
            diag.message = validation_label +
                " apply blocked: cached analysis trace is no longer available";
            errors.push_back(std::move(diag));
            return false;
        }

        const auto predicted_changed_files = collect_backup_files(suggestion);
        return collect_compile_command_validation_sources(
            last_compile_commands_path_,
            analysis_it->second,
            last_project_root_,
            suggestion,
            predicted_changed_files,
            validation_label,
            errors
        ).has_value();
    }

    ApplySuggestionResult SuggestionManager::apply_suggestion(
        const std::string& suggestion_id,
        bool /*skip_validation*/,
        bool skip_rebuild,
        bool create_backup_flag
    ) {
        ApplySuggestionResult result;
        result.success = false;
        const auto apply_start = std::chrono::steady_clock::now();
        const auto record_phase = [&result](
            const std::string_view name,
            const std::chrono::steady_clock::time_point phase_start
        ) {
            result.phase_timings_ms[std::string(name)] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - phase_start
            ).count();
        };

        auto bha_it = bha_suggestions_.find(suggestion_id);
        if (bha_it == bha_suggestions_.end()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Invalid suggestion ID: " + suggestion_id;
            result.errors.push_back(diag);
            return result;
        }

        const auto& bha_sug = bha_it->second;
        if (!validate_suggestion_source_state(bha_sug, result.errors)) {
            return result;
        }
        const auto application_mode = bha::resolve_application_mode(bha_sug);
        const bool enforce_syntax_validation =
            requires_compile_command_syntax_validation(bha_sug) &&
            config_.enforce_compile_command_syntax_gate;

        if (enforce_syntax_validation &&
            !can_prepare_compile_command_validation(bha_sug, result.errors)) {
            return result;
        }

        std::vector<FileBackup> transactional_snapshot;

        std::vector<fs::path> changed_files;
        const bool apply_ok = application_mode == bha::SuggestionApplicationMode::ExternalRefactor
            ? apply_external_refactor_suggestion(
                suggestion_id,
                bha_sug,
                create_backup_flag,
                result,
                changed_files
            )
            : apply_direct_edit_suggestion(
                suggestion_id,
                bha_sug,
                create_backup_flag,
                enforce_syntax_validation,
                result,
                transactional_snapshot,
                changed_files
            );
        if (!apply_ok) {
            return result;
        }
        record_phase("apply_edits", apply_start);

        const auto syntax_validation_start = std::chrono::steady_clock::now();
        if (enforce_syntax_validation &&
            !validate_compile_command_backed_suggestion(bha_sug, changed_files, result.errors)) {
            record_phase("syntax_validation", syntax_validation_start);
            rollback_apply_suggestion(
                result,
                transactional_snapshot,
                "Syntax validation failed and automatic rollback did not complete"
            );
            return result;
        }
        if (enforce_syntax_validation) {
            record_phase("syntax_validation", syntax_validation_start);
        }

        append_changed_files(result, changed_files);

        const auto rebuild_validation_start = std::chrono::steady_clock::now();
        if (!skip_rebuild && !validate_post_apply_rebuild(result)) {
            record_phase("rebuild_validation", rebuild_validation_start);
            if (result.backup_id) {
                rollback_apply_suggestion(
                    result,
                    transactional_snapshot,
                    "Automatic rollback failed after build failure"
                );
            }
            return result;
        }
        if (!skip_rebuild) {
            record_phase("rebuild_validation", rebuild_validation_start);
        }

        result.success = true;
        return result;
    }

    ApplySuggestionResult SuggestionManager::apply_edit_bundle(
        const std::vector<bha::TextEdit>& edits,
        const bool create_backup_flag
    ) {
        ApplySuggestionResult result;
        result.success = false;

        if (edits.empty()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "No edits provided";
            result.errors.push_back(std::move(diag));
            return result;
        }

        const auto files_to_backup = collect_unique_edit_files(edits);
        if (create_backup_flag &&
            !create_backup_for_files(
                files_to_backup,
                "Failed to create durable backup before applying edit bundle",
                result
            )) {
            return result;
        }

        bha::Suggestion synthetic;
        synthetic.title = "Direct text edit bundle";
        synthetic.type = bha::SuggestionType::IncludeRemoval;
        synthetic.is_safe = true;
        synthetic.edits = edits;
        synthetic.application_mode = bha::SuggestionApplicationMode::DirectEdits;

        std::vector<fs::path> changed_files;
        if (!apply_file_changes(synthetic, changed_files)) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.message = "Failed to apply edit bundle";
            result.errors.push_back(std::move(diag));
            if (result.backup_id) {
                rollback_apply_suggestion(
                    result,
                    {},
                    "Automatic rollback failed after edit-bundle apply failure"
                );
            }
            return result;
        }

        append_changed_files(result, changed_files);
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
