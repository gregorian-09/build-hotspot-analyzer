    bool SuggestionManager::revert_changes(const std::string& backup_id, const bool preserve_backup) {
        const auto it = backups_.find(backup_id);
        if (it == backups_.end()) {
            if (config_.use_disk_backups && !config_.workspace_root.empty()) {
                if (restore_disk_backup(backup_id)) {
                    if (!preserve_backup && !config_.keep_backups) {
                        cleanup_disk_backup(backup_id);
                    }
                    return true;
                }
            }
            return false;
        }

        const bool is_disk_backup = config_.use_disk_backups && !config_.workspace_root.empty() &&
                              fs::exists(get_backup_path(backup_id));

        if (is_disk_backup) {
            if (!restore_disk_backup(backup_id)) {
                return false;
            }
            if (!preserve_backup && !config_.keep_backups) {
                cleanup_disk_backup(backup_id);
            }
        } else {
            for (const auto& backup = it->second; const auto& file : backup.files) {
                try {
                    if (!file.existed_before) {
                        std::error_code remove_ec;
                        fs::remove(file.path, remove_ec);
                        if (remove_ec) {
                            return false;
                        }
                    } else {
                        std::ofstream out(file.path, std::ios::binary);
                        if (!out) {
                            return false;
                        }
                        out << file.content;
                    }
                } catch (...) {
                    return false;
                }
            }
        }

        if (!preserve_backup) {
            backups_.erase(it);
            backup_lru_.remove(backup_id);
        }
        return true;
    }

    std::string SuggestionManager::create_backup(const std::vector<fs::path>& files) {
        if (config_.use_disk_backups && !config_.workspace_root.empty()) {
            return create_disk_backup(files);
        }
        return create_memory_backup(files);
    }

    std::string SuggestionManager::create_memory_backup(const std::vector<fs::path>& files) {
        Backup backup;
        backup.id = generate_backup_id();
        backup.timestamp = std::chrono::system_clock::now();

        for (const auto& file : files) {
            FileBackup file_backup;
            file_backup.path = file;
            file_backup.existed_before = fs::exists(file);

            if (file_backup.existed_before) {
                if (std::ifstream in(file, std::ios::binary); in) {
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    file_backup.content = ss.str();
                }
            }
            backup.files.push_back(std::move(file_backup));
        }

        const std::string backup_id = backup.id;
        backups_[backup_id] = std::move(backup);
        backup_lru_.push_back(backup_id);

        evict_old_backups();

        return backup_id;
    }

    fs::path SuggestionManager::get_backup_path(const std::string& backup_id) const {
        const fs::path backup_dir = config_.workspace_root / config_.backup_directory;
        return backup_dir / backup_id;
    }

    std::string SuggestionManager::create_disk_backup(const std::vector<fs::path>& files) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        std::ostringstream timestamp_ss;
        timestamp_ss << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");

        std::string backup_id = timestamp_ss.str() + "-" + std::to_string(++backup_counter_);
        fs::path backup_path = get_backup_path(backup_id);

        std::error_code ec;
        fs::create_directories(backup_path, ec);
        if (ec) {
            return {};
        }

        Backup backup;
        backup.id = backup_id;
        backup.timestamp = now;

        for (const auto& file : files) {
            FileBackup file_backup;
            file_backup.path = file;
            file_backup.existed_before = fs::exists(file);

            fs::path relative = fs::relative(file, config_.workspace_root, ec);
            if (ec) {
                fs::remove_all(backup_path, ec);
                return {};
            }
            if (file_backup.existed_before) {
                const fs::path dest = backup_path / "files" / relative;
                if (const fs::path parent = dest.parent_path(); !parent.empty()) {
                    fs::create_directories(parent, ec);
                    if (ec) {
                        fs::remove_all(backup_path, ec);
                        return {};
                    }
                }

                if (!copy_file_with_sync(file, dest)) {
                    fs::remove_all(backup_path, ec);
                    return {};
                }
            }
            backup.files.push_back(std::move(file_backup));
        }

        if (!write_backup_metadata(backup_path, backup)) {
            fs::remove_all(backup_path, ec);
            return {};
        }

        if (!sync_directory_to_disk(backup_path)) {
            fs::remove_all(backup_path, ec);
            return {};
        }
        const fs::path root_backup_dir = (config_.workspace_root / config_.backup_directory).lexically_normal();
        if (!root_backup_dir.empty() && !sync_directory_to_disk(root_backup_dir)) {
            fs::remove_all(backup_path, ec);
            return {};
        }

        backups_[backup_id] = std::move(backup);
        backup_lru_.push_back(backup_id);
        evict_old_backups();

        return backup_id;
    }

    bool SuggestionManager::write_backup_metadata(const fs::path& backup_dir, const Backup& backup) {
        const fs::path meta_path = backup_dir / "metadata.txt";
        const fs::path tmp_meta_path = backup_dir / "metadata.txt.tmp";
        std::ofstream out(tmp_meta_path, std::ios::trunc);
        if (!out) return false;

        const auto time_t = std::chrono::system_clock::to_time_t(backup.timestamp);
        out << "id=" << backup.id << "\n";
        out << "timestamp=" << time_t << "\n";
        out << "file_count=" << backup.files.size() << "\n";

        for (const auto& file : backup.files) {
            out << "file=" << file.path.string() << "\n";
            out << "existed_before=" << (file.existed_before ? 1 : 0) << "\n";
        }
        out.flush();
        out.close();
        if (!out.good()) {
            return false;
        }

        if (!sync_file_to_disk(tmp_meta_path)) {
            return false;
        }

        std::error_code ec;
        fs::rename(tmp_meta_path, meta_path, ec);
        if (ec) {
            fs::remove(meta_path, ec);
            ec.clear();
            fs::rename(tmp_meta_path, meta_path, ec);
            if (ec) {
                return false;
            }
        }

        if (!sync_file_to_disk(meta_path)) {
            return false;
        }
        if (!sync_directory_to_disk(backup_dir)) {
            return false;
        }
        return true;
    }

    std::optional<Backup> SuggestionManager::read_backup_metadata(const fs::path& backup_dir) {
        fs::path meta_path = backup_dir / "metadata.txt";
        std::ifstream in(meta_path);
        if (!in) return std::nullopt;

        Backup backup;
        std::string line;
        while (std::getline(in, line)) {
            auto eq_pos = line.find('=');
            if (eq_pos == std::string::npos) continue;

            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);

            if (key == "id") {
                backup.id = value;
            } else if (key == "timestamp") {
                auto time_t = std::stoll(value);
                backup.timestamp = std::chrono::system_clock::from_time_t(time_t);
            } else if (key == "file") {
                FileBackup fb;
                fb.path = value;
                backup.files.push_back(std::move(fb));
            } else if (key == "existed_before" && !backup.files.empty()) {
                backup.files.back().existed_before = (value == "1" || value == "true");
            }
        }

        return backup.id.empty() ? std::nullopt : std::make_optional(std::move(backup));
    }

    bool SuggestionManager::restore_disk_backup(const std::string& backup_id) const
    {
        const fs::path backup_path = get_backup_path(backup_id);
        if (!fs::exists(backup_path)) return false;

        auto metadata = read_backup_metadata(backup_path);
        if (!metadata) return false;

        for (const auto& file : metadata->files) {
            const fs::path path = file.path;
            if (!file.existed_before) {
                std::error_code remove_ec;
                fs::remove(path, remove_ec);
                if (remove_ec) {
                    return false;
                }
                continue;
            }

            const fs::path relative = fs::relative(path, config_.workspace_root);
            const fs::path src = backup_path / "files" / relative;

            if (!fs::exists(src)) {
                return false;
            }

            try {
                if (const fs::path parent = path.parent_path(); !parent.empty()) {
                    fs::create_directories(parent);
                }
                fs::copy_file(src, path, fs::copy_options::overwrite_existing);
            } catch (const std::exception&) {
                return false;
            }
        }

        return true;
    }

    void SuggestionManager::cleanup_disk_backup(const std::string& backup_id) const
    {
        if (const fs::path backup_path = get_backup_path(backup_id); fs::exists(backup_path)) {
            std::error_code ec;
            fs::remove_all(backup_path, ec);
        }
    }

    bool SuggestionManager::validate_files_exist(const std::vector<fs::path>& files) {
        return std::ranges::all_of(files, [](const auto& file) {
            return fs::exists(file);
        });
    }

    namespace {
        /**
         * Converts a 0-based line and column position to a byte offset in the content.
         * Handles both Unix (LF) and Windows (CRLF) line endings.
         */
        std::optional<std::size_t> line_col_to_offset(
            const std::string& content,
            const std::size_t line,
            const std::size_t col
        ) {
            std::size_t current_line = 0;
            std::size_t line_start = 0;

            for (std::size_t i = 0; i < content.size(); ++i) {
                if (current_line == line) {
                    std::size_t pos = line_start;
                    std::size_t units = 0;
                    while (pos < content.size() && content[pos] != '\n' && content[pos] != '\r') {
                        if (units >= col) {
                            return pos;
                        }

                        const unsigned char lead = static_cast<unsigned char>(content[pos]);
                        std::size_t advance = 1;
                        uint32_t codepoint = 0;
                        if ((lead & 0x80u) == 0) {
                            codepoint = lead;
                            advance = 1;
                        } else if ((lead & 0xE0u) == 0xC0u && pos + 1 < content.size()) {
                            codepoint = ((lead & 0x1Fu) << 6) |
                                        (static_cast<unsigned char>(content[pos + 1]) & 0x3Fu);
                            advance = 2;
                        } else if ((lead & 0xF0u) == 0xE0u && pos + 2 < content.size()) {
                            codepoint = ((lead & 0x0Fu) << 12) |
                                        ((static_cast<unsigned char>(content[pos + 1]) & 0x3Fu) << 6) |
                                        (static_cast<unsigned char>(content[pos + 2]) & 0x3Fu);
                            advance = 3;
                        } else if ((lead & 0xF8u) == 0xF0u && pos + 3 < content.size()) {
                            codepoint = ((lead & 0x07u) << 18) |
                                        ((static_cast<unsigned char>(content[pos + 1]) & 0x3Fu) << 12) |
                                        ((static_cast<unsigned char>(content[pos + 2]) & 0x3Fu) << 6) |
                                        (static_cast<unsigned char>(content[pos + 3]) & 0x3Fu);
                            advance = 4;
                        } else {
                            codepoint = lead;
                            advance = 1;
                        }

                        units += (codepoint > 0xFFFFu) ? 2u : 1u;
                        pos += advance;
                    }
                    return pos;
                }
                if (content[i] == '\n') {
                    ++current_line;
                    line_start = i + 1;
                }
            }

            // Handle last line (no trailing newline)
            if (current_line == line) {
                std::size_t pos = line_start;
                std::size_t units = 0;
                while (pos < content.size() && content[pos] != '\n' && content[pos] != '\r') {
                    if (units >= col) {
                        return pos;
                    }

                    const unsigned char lead = static_cast<unsigned char>(content[pos]);
                    std::size_t advance = 1;
                    uint32_t codepoint = 0;
                    if ((lead & 0x80u) == 0) {
                        codepoint = lead;
                        advance = 1;
                    } else if ((lead & 0xE0u) == 0xC0u && pos + 1 < content.size()) {
                        codepoint = ((lead & 0x1Fu) << 6) |
                                    (static_cast<unsigned char>(content[pos + 1]) & 0x3Fu);
                        advance = 2;
                    } else if ((lead & 0xF0u) == 0xE0u && pos + 2 < content.size()) {
                        codepoint = ((lead & 0x0Fu) << 12) |
                                    ((static_cast<unsigned char>(content[pos + 1]) & 0x3Fu) << 6) |
                                    (static_cast<unsigned char>(content[pos + 2]) & 0x3Fu);
                        advance = 3;
                    } else if ((lead & 0xF8u) == 0xF0u && pos + 3 < content.size()) {
                        codepoint = ((lead & 0x07u) << 18) |
                                    ((static_cast<unsigned char>(content[pos + 1]) & 0x3Fu) << 12) |
                                    ((static_cast<unsigned char>(content[pos + 2]) & 0x3Fu) << 6) |
                                    (static_cast<unsigned char>(content[pos + 3]) & 0x3Fu);
                        advance = 4;
                    } else {
                        codepoint = lead;
                        advance = 1;
                    }

                    units += (codepoint > 0xFFFFu) ? 2u : 1u;
                    pos += advance;
                }
                return pos;
            }

            if (line > current_line) {
                return content.size();
            }

            return std::nullopt;
        }

        /**
         * Applies a single TextEdit to the content string.
         * Compiler-derived edits use their original byte range. Other edits
         * use the LSP-compatible line and UTF-16 column coordinates.
         */
        bool apply_single_edit(std::string& content, const bha::TextEdit& edit) {
            if (!edit.is_valid()) {
                return false;
            }

            if (edit.has_byte_range()) {
                const std::size_t start = *edit.byte_offset;
                const std::size_t length = *edit.byte_length;
                if (start > content.size() || length > content.size() - start) {
                    return false;
                }
                content.replace(start, length, edit.new_text);
                return true;
            }

            auto start_offset = line_col_to_offset(content, edit.start_line, edit.start_col);
            auto end_offset = line_col_to_offset(content, edit.end_line, edit.end_col);

            if (!start_offset || !end_offset) {
                return false;
            }

            // Ensure start <= end
            if (*start_offset > *end_offset) {
                std::swap(start_offset, end_offset);
            }

            // Clamp to content bounds
            *start_offset = std::min(*start_offset, content.size());
            *end_offset = std::min(*end_offset, content.size());

            // Replace the range with new text
            content.replace(*start_offset, *end_offset - *start_offset, edit.new_text);
            return true;
        }

        /**
         * Applies multiple TextEdits to a file.
         * Edits are sorted in reverse order (by position) to avoid offset shifts.
         */
        bool apply_edits_to_file(const fs::path& file_path, std::vector<bha::TextEdit> edits) {
            if (edits.empty()) {
                return true;
            }

            std::ifstream in(file_path);
            if (!in) {
                return false;
            }
            std::string content(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>()
            );
            in.close();

            const bool has_byte_ranges = std::ranges::any_of(
                edits,
                [](const bha::TextEdit& edit) { return edit.has_byte_range(); }
            );
            const bool all_have_byte_ranges = std::ranges::all_of(
                edits,
                [](const bha::TextEdit& edit) { return edit.has_byte_range(); }
            );
            if (has_byte_ranges != all_have_byte_ranges ||
                std::ranges::any_of(
                    edits,
                    [](const bha::TextEdit& edit) { return edit.has_partial_byte_range(); }
                )) {
                return false;
            }

            // Sort edits in reverse order (later positions first)
            // This ensures applying one edit doesn't shift positions of subsequent edits
            std::ranges::sort(edits, [has_byte_ranges](const bha::TextEdit& a, const bha::TextEdit& b) {
                if (has_byte_ranges) {
                    if (*a.byte_offset != *b.byte_offset) {
                        return *a.byte_offset > *b.byte_offset;
                    }
                    return *a.byte_length > *b.byte_length;
                }
                if (a.start_line != b.start_line) {
                    return a.start_line > b.start_line;  // Later lines first
                }
                return a.start_col > b.start_col;  // Later columns first
            });

            for (const auto& edit : edits) {
                if (!apply_single_edit(content, edit)) {
                    return false;
                }
            }

            std::ofstream out(file_path);
            if (!out) {
                return false;
            }
            out << content;
            return out.good();
        }

    }  // namespace

    bool apply_replacements_to_file(
        const fs::path& file_path,
        std::vector<ExternalReplacement> replacements
    ) {
        if (replacements.empty()) {
            return true;
        }

        std::ifstream in(file_path, std::ios::binary);
        if (!in) {
            return false;
        }
        std::string content(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        in.close();

        std::ranges::sort(replacements, [](const ExternalReplacement& lhs, const ExternalReplacement& rhs) {
            if (lhs.offset != rhs.offset) {
                return lhs.offset > rhs.offset;
            }
            return lhs.length > rhs.length;
        });

        for (const auto& replacement : replacements) {
            if (replacement.offset > content.size()) {
                return false;
            }
            const auto clamped_length = std::min(replacement.length, content.size() - replacement.offset);
            content.replace(replacement.offset, clamped_length, replacement.replacement_text);
        }

        std::ofstream out(file_path, std::ios::binary);
        if (!out) {
            return false;
        }
        out << content;
        return out.good();
    }
