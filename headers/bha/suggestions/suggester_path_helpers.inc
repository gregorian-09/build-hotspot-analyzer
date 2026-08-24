
    [[nodiscard]] inline std::optional<fs::path> find_file_in_repo(
        const fs::path& repo_root,
        const fs::path& filename
    );

    [[nodiscard]] inline std::optional<fs::path> resolve_trace_repo_root(const fs::path& path);

    [[nodiscard]] inline fs::path resolve_source_path(const fs::path& path);

    /**
     * Restrict source-transforming semantic passes to files owned by the
     * analyzed project. Dependency and system headers are evidence inputs,
     * not edit targets, and replaying them for every translation unit is both
     * unsafe and needlessly expensive.
     */
    [[nodiscard]] inline bool is_project_owned_path(
        const SuggestionContext& context,
        const fs::path& path
    ) {
        if (!context.project_index || path.empty()) {
            return false;
        }
        const fs::path root = context.project_root.empty()
            ? context.project_index->project_root()
            : context.project_root;
        if (root.empty()) {
            return false;
        }
        return bha::utils::is_under(
            context.project_index->resolve(path),
            context.project_index->resolve(root)
        );
    }

    [[nodiscard]] inline std::string repo_relative_path_string(
        const fs::path& path,
        const fs::path& project_root
    ) {
        fs::path resolved_path = path;
        if (resolved_path.is_relative() && resolved_path.parent_path().empty()) {
            resolved_path = resolve_source_path(resolved_path);
        }

        if (auto repo_root = resolve_trace_repo_root(resolved_path)) {
            if (auto found = find_file_in_repo(*repo_root, resolved_path.filename())) {
                std::error_code ec;
                auto rel = fs::relative(*found, *repo_root, ec);
                if (!ec && !rel.empty()) {
                    return rel.generic_string();
                }
            }
        }

        if (!project_root.empty() && resolved_path.is_absolute()) {
            std::error_code ec;
            auto rel = fs::relative(resolved_path, project_root, ec);
            if (!ec && !rel.empty()) {
                return rel.generic_string();
            }
        }
        return resolved_path.generic_string();
    }

    [[nodiscard]] inline bool is_public_api_header_path(std::string_view repo_relative) {
        return repo_relative.rfind("include/", 0) == 0 ||
               repo_relative.rfind("public/", 0) == 0 ||
               repo_relative.rfind("api/", 0) == 0;
    }

    [[nodiscard]] inline bool file_contains_extern_c_linkage(const fs::path& path) {
        static std::mutex cache_mutex;
        static std::unordered_map<std::string, bool> cache;

        const fs::path resolved = resolve_source_path(path).lexically_normal();
        const std::string key = resolved.generic_string();
        {
            const std::scoped_lock lock(cache_mutex);
            if (const auto it = cache.find(key); it != cache.end()) {
                return it->second;
            }
        }

        std::ifstream in(resolved);
        bool contains_extern_c = false;
        if (in.is_open()) {
            const std::string content(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>()
            );
            contains_extern_c = content.find("extern \"C\"") != std::string::npos;
        }

        const std::scoped_lock lock(cache_mutex);
        cache[key] = contains_extern_c;
        return contains_extern_c;
    }

    [[nodiscard]] inline bool is_abi_sensitive_header_path(
        const fs::path& path,
        const fs::path& project_root
    ) {
        if (!is_header_file_path(path)) {
            return false;
        }
        const std::string repo_relative = repo_relative_path_string(path, project_root);
        return is_public_api_header_path(repo_relative) || file_contains_extern_c_linkage(path);
    }

    [[nodiscard]] inline ProjectLanguageProfile summarize_file_language_profile(
        const fs::path& path,
        const SuggestionContext& context
    ) {
        ProjectLanguageProfile profile;
        const fs::path target = resolve_source_path(path).lexically_normal();

        for (const auto& unit : context.trace.units) {
            const SourceLanguageMode mode = detect_source_language_mode(unit);
            const auto count_mode = [&](const SourceLanguageMode detected) {
                switch (detected) {
                    case SourceLanguageMode::C:
                        ++profile.c_units;
                        break;
                    case SourceLanguageMode::CXX:
                        ++profile.cxx_units;
                        break;
                    case SourceLanguageMode::ObjectiveC:
                        ++profile.objc_units;
                        break;
                    case SourceLanguageMode::ObjectiveCXX:
                        ++profile.objcxx_units;
                        break;
                    case SourceLanguageMode::Unknown:
                        ++profile.unknown_units;
                        break;
                }
            };

            if (resolve_source_path(unit.source_file).lexically_normal() == target) {
                count_mode(mode);
                continue;
            }

            for (const auto& include : unit.includes) {
                if (resolve_source_path(include.header).lexically_normal() == target) {
                    count_mode(mode);
                    break;
                }
            }
        }

        return profile;
    }

    [[nodiscard]] inline bool is_c_like_profile(const ProjectLanguageProfile& profile) noexcept {
        return profile.has_c_family() && !profile.has_cxx_family();
    }

    [[nodiscard]] inline bool is_mixed_c_and_cxx_profile(const ProjectLanguageProfile& profile) noexcept {
        return profile.has_c_family() && profile.has_cxx_family();
    }

    inline void downgrade_suggestion_to_manual_review(
        Suggestion& suggestion,
        const std::string& reason,
        const std::string& guidance
    ) {
        suggestion.application_mode = SuggestionApplicationMode::Advisory;
        suggestion.application_summary = "Manual review only";
        suggestion.application_guidance = guidance;
        suggestion.auto_apply_blocked_reason = reason;
        suggestion.edits.clear();
        suggestion.refactor_class_name.reset();
        suggestion.refactor_compile_commands_path.reset();
    }

    [[nodiscard]] inline std::vector<fs::path> suggestion_touched_files(const Suggestion& suggestion) {
        std::vector<fs::path> files;
        auto append_unique = [&](const fs::path& candidate) {
            if (candidate.empty()) {
                return;
            }
            const fs::path normalized = resolve_source_path(candidate).lexically_normal();
            if (std::ranges::find(files, normalized) == files.end()) {
                files.push_back(normalized);
            }
        };

        append_unique(suggestion.target_file.path);
        for (const auto& secondary : suggestion.secondary_files) {
            append_unique(secondary.path);
        }
        for (const auto& edit : suggestion.edits) {
            append_unique(edit.file);
        }
        return files;
    }

    [[nodiscard]] inline bool suggestion_touches_abi_sensitive_header(
        const Suggestion& suggestion,
        const fs::path& project_root
    ) {
        for (const auto& path : suggestion_touched_files(suggestion)) {
            if (is_abi_sensitive_header_path(path, project_root)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline bool has_build_system_marker(const fs::path& dir) {
        if (fs::exists(dir / "CMakeLists.txt") ||
            fs::exists(dir / "meson.build") ||
            fs::exists(dir / "build.ninja") ||
            fs::exists(dir / "Makefile") ||
            fs::exists(dir / "makefile") ||
            fs::exists(dir / "GNUmakefile") ||
            fs::exists(dir / "WORKSPACE") ||
            fs::exists(dir / "WORKSPACE.bazel") ||
            fs::exists(dir / "MODULE.bazel") ||
            fs::exists(dir / "BUILD") ||
            fs::exists(dir / "BUILD.bazel") ||
            fs::exists(dir / "BUCK") ||
            fs::exists(dir / "BUCK.v2") ||
            fs::exists(dir / ".buckconfig") ||
            fs::exists(dir / "SConstruct") ||
            fs::exists(dir / "SConscript")) {
            return true;
        }

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto ext = entry.path().extension().string();
            if (ext == ".sln" || ext == ".vcxproj" || ext == ".xcodeproj" || ext == ".xcworkspace") {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] inline bool has_build_system_marker_fast(const fs::path& dir) {
        return fs::exists(dir / "CMakeLists.txt") ||
               fs::exists(dir / "meson.build") ||
               fs::exists(dir / "build.ninja") ||
               fs::exists(dir / "Makefile") ||
               fs::exists(dir / "makefile") ||
               fs::exists(dir / "GNUmakefile") ||
               fs::exists(dir / "WORKSPACE") ||
               fs::exists(dir / "WORKSPACE.bazel") ||
               fs::exists(dir / "MODULE.bazel") ||
               fs::exists(dir / "BUILD") ||
               fs::exists(dir / "BUILD.bazel") ||
               fs::exists(dir / "BUCK") ||
               fs::exists(dir / "BUCK.v2") ||
               fs::exists(dir / ".buckconfig") ||
               fs::exists(dir / "SConstruct") ||
               fs::exists(dir / "SConscript");
    }

    [[nodiscard]] inline fs::path find_repository_root(const fs::path& path) {
        fs::path current = path;
        if (fs::exists(current)) {
            if (fs::is_regular_file(current)) {
                current = current.parent_path();
            }
        } else if (path.has_parent_path()) {
            current = path.parent_path();
        }

        std::optional<fs::path> git_root;
        while (!current.empty() && current.has_parent_path() && current != current.parent_path()) {
            if (has_build_system_marker(current)) {
                return current;
            }
            if (!git_root.has_value() && fs::exists(current / ".git")) {
                git_root = current;
            }
            current = current.parent_path();
        }

        if (git_root.has_value()) {
            return *git_root;
        }

        return {};
    }

    [[nodiscard]] inline std::optional<fs::path> find_file_in_repo(
        const fs::path& repo_root,
        const fs::path& filename
    ) {
        static std::unordered_map<std::string, fs::path> cache;
        if (repo_root.empty() || filename.empty()) {
            return std::nullopt;
        }

        const std::string key = repo_root.string() + "|" + filename.string();
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second.empty() ? std::nullopt : std::optional<fs::path>(it->second);
        }

        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(repo_root, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().filename() == filename) {
                cache.emplace(key, entry.path());
                return entry.path();
            }
        }

        cache.emplace(key, fs::path());
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<fs::path> resolve_trace_repo_root(const fs::path& path) {
        if (path.empty()) {
            return std::nullopt;
        }

        fs::path normalized = path;
        if (normalized.is_relative()) {
            normalized = fs::current_path() / normalized;
        }
        normalized = normalized.lexically_normal();

        // Look for ".../traces/..." with a sibling "repo" directory.
        fs::path prefix;
        std::vector<fs::path> parts;
        for (const auto& part : normalized) {
            parts.push_back(part);
        }
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (parts[i] != "traces") {
                continue;
            }

            prefix.clear();
            for (std::size_t k = 0; k < i; ++k) {
                prefix /= parts[k];
            }

            const fs::path sibling_repo = prefix / "repo";
            if (fs::exists(sibling_repo)) {
                return sibling_repo;
            }

        }

        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<fs::path> resolve_trace_repo_file(const fs::path& path) {
        if (auto repo_root = resolve_trace_repo_root(path)) {
            if (auto found = find_file_in_repo(*repo_root, path.filename())) {
                return *found;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline fs::path resolve_source_path(const fs::path& path) {
        if (auto resolved = resolve_trace_repo_file(path)) {
            return *resolved;
        }
        if (path.is_relative() && path.parent_path().empty()) {
            const fs::path root = find_repository_root(fs::current_path());
            if (!root.empty()) {
                if (auto found = find_file_in_repo(root, path.filename())) {
                    return *found;
                }
            }
        }
        return path;
    }

    [[nodiscard]] inline std::optional<fs::path> find_cmake_source_root_from_cache(
        const fs::path& build_dir
    ) {
        if (build_dir.empty()) {
            return std::nullopt;
        }
        const fs::path cache_path = build_dir / "CMakeCache.txt";
        std::error_code ec;
        if (!fs::exists(cache_path, ec) || ec) {
            return std::nullopt;
        }
        auto content = bha::utils::read_file(cache_path);
        if (content.is_err()) {
            return std::nullopt;
        }
        std::istringstream input(content.value());
        std::string line;
        while (std::getline(input, line)) {
            if (line.rfind("CMAKE_HOME_DIRECTORY:", 0) != 0 &&
                line.rfind("CMAKE_SOURCE_DIR:", 0) != 0) {
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            std::string root_text = line.substr(eq + 1);
            while (!root_text.empty() &&
                   std::isspace(static_cast<unsigned char>(root_text.back()))) {
                root_text.pop_back();
            }
            fs::path root = root_text;
            if (!root.empty() && fs::exists(root / "CMakeLists.txt")) {
                return root;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline fs::path find_project_root_from_trace_path(const fs::path& trace_path) {
        if (trace_path.empty()) {
            return {};
        }
        fs::path dir = trace_path;
        if (fs::is_regular_file(dir)) {
            dir = dir.parent_path();
        }
        // 1) Prefer CMake source root from a nearby build dir cache.
        for (fs::path current = dir; !current.empty();) {
            if (auto cmake_root = find_cmake_source_root_from_cache(current)) {
                return *cmake_root;
            }
            const fs::path parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
        // 2) Scan direct child directories of ancestors for CMakeCache.txt.
        for (fs::path current = dir; !current.empty();) {
            std::error_code ec;
            std::vector<std::pair<fs::path, int>> queue;
            queue.reserve(256);
            std::size_t checked = 0;
            for (const auto& entry : fs::directory_iterator(current, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_directory()) {
                    continue;
                }
                if (++checked > 200) {
                    break;
                }
                queue.emplace_back(entry.path(), 0);
            }

            std::size_t idx = 0;
            while (idx < queue.size() && checked <= 500) {
                const auto [path, depth] = queue[idx++];
                if (auto cmake_root = find_cmake_source_root_from_cache(path)) {
                    return *cmake_root;
                }
                if (depth >= 1) {
                    continue;
                }
                std::error_code child_ec;
                for (const auto& child : fs::directory_iterator(path, child_ec)) {
                    if (child_ec) {
                        break;
                    }
                    if (!child.is_directory()) {
                        continue;
                    }
                    if (++checked > 500) {
                        break;
                    }
                    queue.emplace_back(child.path(), depth + 1);
                }
            }
            const fs::path parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
        // 3) Match a trace filename against nearby build-system roots.
        auto resolve_trace_sample = [&]() -> fs::path {
            fs::path sample = trace_path;
            if (fs::is_regular_file(sample)) {
                return sample;
            }
            if (!fs::is_directory(sample)) {
                return {};
            }
            std::error_code ec;
            std::size_t checked = 0;
            for (const auto& entry : fs::recursive_directory_iterator(sample, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (++checked > 200) {
                    break;
                }
                if (entry.path().extension() == ".json") {
                    return entry.path();
                }
            }
            return {};
        };

        const fs::path sample_trace = resolve_trace_sample();
        std::string sample_name = sample_trace.filename().string();
        if (sample_name.size() > 5 && sample_name.ends_with(".json")) {
            sample_name = sample_name.substr(0, sample_name.size() - 5);
        } else {
            sample_name.clear();
        }

        if (!sample_name.empty()) {
            std::size_t checked_dirs = 0;
            for (fs::path current = dir; !current.empty();) {
                std::error_code ec;
                for (const auto& entry : fs::directory_iterator(current, ec)) {
                    if (ec) {
                        break;
                    }
                    if (!entry.is_directory()) {
                        continue;
                    }
                    if (++checked_dirs > 300) {
                        break;
                    }
                    const fs::path candidate = entry.path();
                    if (!has_build_system_marker_fast(candidate)) {
                        continue;
                    }
                    if (fs::exists(candidate / sample_name)) {
                        return candidate;
                    }
                    if (auto found = find_file_in_repo(candidate, fs::path(sample_name))) {
                        return candidate;
                    }
                }
                if (checked_dirs > 300) {
                    break;
                }
                const fs::path parent = current.parent_path();
                if (parent == current) {
                    break;
                }
                current = parent;
            }
        }
        // 2) Fall back to nearest directory with build-system markers.
        for (fs::path current = dir; !current.empty();) {
            if (has_build_system_marker_fast(current)) {
                return current;
            }
            const fs::path parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
        return {};
    }

    [[nodiscard]] inline std::string make_repo_relative(const fs::path& path) {
        if (auto repo_root = resolve_trace_repo_root(path)) {
            if (auto found = find_file_in_repo(*repo_root, path.filename())) {
                std::error_code ec;
                auto rel = fs::relative(*found, *repo_root, ec);
                if (!ec && !rel.empty()) {
                    return rel.generic_string();
                }
            }
        }

        if (path.is_relative() && path.parent_path().empty()) {
            const fs::path root = find_repository_root(fs::current_path());
            if (!root.empty()) {
                if (auto found = find_file_in_repo(root, path.filename())) {
                    std::error_code ec;
                    auto rel = fs::relative(*found, root, ec);
                    if (!ec && !rel.empty()) {
                        return rel.generic_string();
                    }
                }
            }
        }

        const fs::path root = find_repository_root(path);
        if (!root.empty() && path.is_absolute()) {
            std::error_code ec;
            auto rel = fs::relative(path, root, ec);
            if (!ec && !rel.empty()) {
                return rel.generic_string();
            }
        }

        return path.generic_string();
    }

    /**
     * Generates a unique suggestion ID from a prefix and file path.
     *
     * Uses hash of the full path to avoid collisions when multiple files
     * have the same filename in different directories.
     *
     * @param prefix Suggestion type prefix (e.g., "pch", "fwd", "unused")
     * @param path File path to include in the ID
     * @param suffix Optional additional suffix for disambiguation
     * @return Unique suggestion ID string
     */
    [[nodiscard]] inline std::string generate_suggestion_id(
        const std::string_view prefix,
        const fs::path& path,
        const std::string_view suffix = ""
    ) {
        std::ostringstream oss;
        oss << prefix << "-";

        const std::uint64_t path_hash = std::hash<std::string>{}(path.string());
        oss << std::hex << std::setw(16) << std::setfill('0') << path_hash;

        // Readable filename for human identification
        oss << "-" << path.filename().string();

        if (!suffix.empty()) {
            oss << "-" << suffix;
        }

        return oss.str();
    }

    /**
     * Generates a unique suggestion ID from a prefix and counter.
     *
     * Use this variant when there's no natural file path (e.g., unity builds).
     *
     * @param prefix Suggestion type prefix
     * @param counter Unique counter value
     * @param name Optional descriptive name
     * @return Unique suggestion ID string
     */
    [[nodiscard]] inline std::string generate_suggestion_id(
        const std::string_view prefix,
        const std::size_t counter,
        const std::string_view name = ""
    ) {
        std::ostringstream oss;
        oss << prefix << "-" << counter;
        if (!name.empty()) {
            oss << "-" << name;
        }
        return oss.str();
    }
