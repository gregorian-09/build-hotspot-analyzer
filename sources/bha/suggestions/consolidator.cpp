//
// Created by gregorian-rayne on 01/18/26.
//

#include "bha/suggestions/consolidator.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <ranges>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {
        namespace fs = std::filesystem;

        bool is_build_system_file(const fs::path& path) {
            const auto name = path.filename().string();
            return name == "CMakeLists.txt" || name == "Makefile" ||
                   name == "makefile" || name == "GNUmakefile";
        }

        std::vector<TextEdit> merge_text_edits(std::vector<TextEdit> edits) {
            if (edits.empty()) {
                return edits;
            }

            std::ranges::sort(edits, [](const TextEdit& a, const TextEdit& b) {
                if (a.file != b.file) {
                    return a.file < b.file;
                }
                if (a.start_line != b.start_line) {
                    return a.start_line < b.start_line;
                }
                return a.start_col < b.start_col;
            });

            std::vector<TextEdit> merged;
            merged.reserve(edits.size());

            for (const auto& edit : edits) {
                bool conflict = false;

                for (const auto& existing : merged) {
                    if (existing.file != edit.file) {
                        continue;
                    }

                    const bool overlaps =
                        (edit.start_line < existing.end_line ||
                         (edit.start_line == existing.end_line && edit.start_col < existing.end_col)) &&
                        (edit.end_line > existing.start_line ||
                         (edit.end_line == existing.start_line && edit.end_col > existing.start_col));

                    if (overlaps) {
                        conflict = true;
                        break;
                    }
                }

                if (!conflict) {
                    merged.push_back(edit);
                }
            }

            std::ranges::sort(merged, [](const TextEdit& a, const TextEdit& b) {
                if (a.file != b.file) {
                    return a.file < b.file;
                }
                if (a.start_line != b.start_line) {
                    return b.start_line < a.start_line;
                }
                return b.start_col < a.start_col;
            });

            return merged;
        }

        TextEdit make_replace_file_edit(const fs::path& path, const std::string& content) {
            TextEdit edit;
            edit.file = path;
            edit.start_line = 0;
            edit.start_col = 0;
            edit.end_line = 0;
            edit.end_col = 0;
            edit.new_text = content;

            if (fs::exists(path)) {
                std::ifstream in(path);
                if (in) {
                    const std::string existing((std::istreambuf_iterator<char>(in)),
                                               std::istreambuf_iterator<char>());
                    edit.end_line = end_of_file_insert_line(existing);
                    edit.end_col = 0;
                }
            }

            return edit;
        }

        void append_build_system_targets(
            const std::vector<Suggestion>& suggestions,
            std::vector<FileTarget>& targets
        ) {
            std::unordered_set<std::string> seen;

            auto add_target = [&](const FileTarget& target) {
                if (target.path.empty() || !is_build_system_file(target.path)) {
                    return;
                }
                const std::string key = target.path.string();
                if (seen.insert(key).second) {
                    targets.push_back(target);
                }
            };

            for (const auto& sug : suggestions) {
                add_target(sug.target_file);
                for (const auto& secondary : sug.secondary_files) {
                    add_target(secondary);
                }
            }
        }

        void append_build_system_edits(
            const std::vector<Suggestion>& suggestions,
            std::vector<TextEdit>& edits
        ) {
            std::unordered_set<std::string> seen;
            for (const auto& sug : suggestions) {
                for (const auto& edit : sug.edits) {
                    if (is_build_system_file(edit.file)) {
                        std::ostringstream key;
                        key << edit.file.string() << ":" << edit.start_line << ":" << edit.end_line
                            << ":" << edit.new_text;
                        if (seen.insert(key.str()).second) {
                            edits.push_back(edit);
                        }
                    }
                }
            }
        }

        std::string make_repo_relative_for_root(const fs::path& path, const fs::path& root) {
            fs::path resolved_path = path;
            if (path.is_relative() && path.parent_path().empty()) {
                resolved_path = resolve_source_path(path);
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

            if (!root.empty() && resolved_path.is_absolute()) {
                std::error_code ec;
                auto rel = fs::relative(resolved_path, root, ec);
                if (!ec && !rel.empty()) {
                    return rel.generic_string();
                }
            }
            return resolved_path.generic_string();
        }

        std::vector<Suggestion> group_by_type(
            const std::vector<Suggestion>& suggestions,
            SuggestionType type
        ) {
            std::vector<Suggestion> filtered;
            std::ranges::copy_if(suggestions, std::back_inserter(filtered),
                [type](const Suggestion& s) { return s.type == type; });
            return filtered;
        }

        fs::path common_parent_path(const fs::path& lhs, const fs::path& rhs) {
            fs::path result;
            auto lit = lhs.begin();
            auto rit = rhs.begin();
            for (; lit != lhs.end() && rit != rhs.end() && *lit == *rit; ++lit, ++rit) {
                result /= *lit;
            }
            return result;
        }

        fs::path compute_common_directory(const std::unordered_set<std::string>& files) {
            fs::path common_dir;
            for (const auto& file : files) {
                const fs::path path(file);
                if (!path.is_absolute()) {
                    continue;
                }
                const fs::path dir = path.parent_path();
                if (dir.empty()) {
                    continue;
                }
                if (common_dir.empty()) {
                    common_dir = dir;
                } else {
                    common_dir = common_parent_path(common_dir, dir);
                }
                if (common_dir.empty()) {
                    break;
                }
            }
            return common_dir;
        }

        std::string resolve_unity_include_path(const fs::path& path) {
            const fs::path resolved = resolve_source_path(path);
            std::string rel = make_repo_relative(resolved);
            if (!fs::path(rel).parent_path().empty()) {
                return rel;
            }

            return rel;
        }

    }  // namespace

    std::vector<Suggestion> SuggestionConsolidator::consolidate(
        std::vector<Suggestion> suggestions
    ) const {
        if (!options_.enable_consolidation) {
            return suggestions;
        }

        std::vector<Suggestion> consolidated;

        for (const auto type : {
            SuggestionType::PCHOptimization,
            SuggestionType::HeaderSplit,
            SuggestionType::UnityBuild,
            SuggestionType::IncludeRemoval,
            SuggestionType::ForwardDeclaration,
            SuggestionType::ExplicitTemplate,
            SuggestionType::PIMPLPattern,
            SuggestionType::MoveToCpp
        }) {
            auto group = group_by_type(suggestions, type);
            if (group.empty()) {
                continue;
            }

            std::optional<Suggestion> result;

            switch (type) {
            case SuggestionType::PCHOptimization:
                result = consolidate_pch(group);
                break;
            case SuggestionType::HeaderSplit:
                result = consolidate_header_split(group);
                break;
            case SuggestionType::UnityBuild:
                result = consolidate_unity_build(group);
                break;
            case SuggestionType::IncludeRemoval:
                result = consolidate_include_removal(group);
                break;
            case SuggestionType::ForwardDeclaration:
                result = consolidate_forward_decl(group);
                break;
            case SuggestionType::ExplicitTemplate:
                consolidated.insert(consolidated.end(), group.begin(), group.end());
                continue;
            case SuggestionType::PIMPLPattern:
                result = consolidate_pimpl(group);
                break;
            case SuggestionType::MoveToCpp:
                consolidated.insert(consolidated.end(), group.begin(), group.end());
                break;
            }

            if (result.has_value()) {
                consolidated.push_back(std::move(result.value()));
            }
        }

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_pch(
        const std::vector<Suggestion>& suggestions
    ) {
        if (suggestions.empty()) {
            return std::nullopt;
        }

        Suggestion consolidated;
        consolidated.type = SuggestionType::PCHOptimization;
        consolidated.priority = Priority::High;

        std::unordered_set<std::string> stable_headers;
        std::unordered_set<std::string> internal_project_headers;
        std::unordered_set<std::string> volatile_headers;
        std::unordered_set<std::string> external_headers;
        fs::path repo_root;

        const auto is_probable_header_path = [](const fs::path& path) {
            std::string lower = path.generic_string();
            std::ranges::transform(lower, lower.begin(),
                                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("cmakelists.txt") != std::string::npos) {
                return false;
            }
            if (lower.ends_with("makefile") || lower.ends_with("gnumakefile")) {
                return false;
            }
            const std::string ext = path.extension().string();
            if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
                return true;
            }
            if (!lower.empty() && lower.front() == '<') {
                return true;
            }
            if (lower.find("/include/") != std::string::npos) {
                return true;
            }
            return false;
        };

        const auto is_public_project_header = [](const std::string& repo_relative) {
            return repo_relative == "pch.h" ||
                   repo_relative.rfind("include/", 0) == 0;
        };

        const auto normalize_project_header = [](const std::string& repo_relative) {
            constexpr std::string_view include_prefix = "include/";
            if (repo_relative.rfind(include_prefix, 0) == 0) {
                return repo_relative.substr(include_prefix.size());
            }
            return repo_relative;
        };

        for (const auto& sug : suggestions) {
            if (sug.priority == Priority::Critical) {
                consolidated.priority = Priority::Critical;
            }

            if (!sug.target_file.path.empty()) {
                if (!is_probable_header_path(sug.target_file.path)) {
                    continue;
                }
                if (repo_root.empty()) {
                    repo_root = find_repository_root(sug.target_file.path);
                }

                const std::string header_str = sug.target_file.path.string();
                const std::string repo_relative = make_repo_relative_for_root(sug.target_file.path, repo_root);
                if (header_str.find('<') == 0 ||
                    header_str.find("/usr/") == 0 ||
                    header_str.find("third_party") != std::string::npos) {
                    external_headers.insert(header_str);
                } else if (sug.rationale.find("volatile") != std::string::npos ||
                           sug.rationale.find("frequently modified") != std::string::npos) {
                    volatile_headers.insert(repo_relative);
                } else if (is_public_project_header(repo_relative)) {
                    stable_headers.insert(repo_relative);
                } else {
                    internal_project_headers.insert(repo_relative);
                }
            }

            for (const auto& secondary : sug.secondary_files) {
                if (!is_probable_header_path(secondary.path)) {
                    continue;
                }
                if (repo_root.empty()) {
                    repo_root = find_repository_root(secondary.path);
                }

                const std::string header_str = secondary.path.string();
                const std::string repo_relative = make_repo_relative_for_root(secondary.path, repo_root);
                if (header_str.ends_with("pch.h") || header_str.ends_with("stdafx.h")) {
                    continue;
                }
                if (header_str.find('<') == 0 ||
                    header_str.find("/usr/") == 0 ||
                    header_str.find("third_party") != std::string::npos) {
                    external_headers.insert(header_str);
                } else if (is_public_project_header(repo_relative)) {
                    stable_headers.insert(repo_relative);
                } else {
                    internal_project_headers.insert(repo_relative);
                }
            }
        }

        if (stable_headers.empty() && external_headers.empty()) {
            return std::nullopt;
        }

        std::ostringstream desc;
        desc << "Consolidate frequently-used stable headers into a precompiled header to improve build times.\n\n";
        desc << "**Summary:** " << external_headers.size() << " external/system headers and "
             << stable_headers.size() << " project headers identified for PCH inclusion.\n\n";

        if (!volatile_headers.empty()) {
            desc << "**Warning:** These headers should NOT be added to PCH (frequently modified):\n";
            std::vector sorted_volatile(volatile_headers.begin(), volatile_headers.end());
            std::ranges::sort(sorted_volatile);
            for (const auto& header : sorted_volatile) {
                desc << "  - " << header << "\n";
            }
            desc << "\n";
        }

        if (!internal_project_headers.empty()) {
            desc << "**Excluded from shared pch.h:** These project-internal headers are not auto-added to the shared PCH because they are less portable across targets:\n";
            std::vector sorted_internal(internal_project_headers.begin(), internal_project_headers.end());
            std::ranges::sort(sorted_internal);
            for (const auto& header : sorted_internal) {
                desc << "  - " << header << "\n";
            }
            desc << "\n";
        }

        desc << "See the **Text Edits** section below for the complete pch.h content.";

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);

        std::ostringstream rationale;
        rationale << "These headers are stable (rarely modified) and widely used across the codebase. "
                  << "Precompiling them can reduce compilation time. "
                  << "Total files benefiting: " << consolidated.impact.files_benefiting.size() << ".";
        consolidated.rationale = rationale.str();

        consolidated.implementation_steps = {
            "1. Create or update precompiled header file (e.g., stdafx.h or pch.h)",
            "2. Add stable headers (listed above) to the PCH file",
            "3. Configure build system to use PCH:",
            "   - CMake: target_precompile_headers(target PRIVATE pch.h)",
            R"(   - MSVC: /Yu"pch.h" for source files, /Yc"pch.h" for PCH generation)",
            "   - GCC/Clang: -include pch.h",
            "4. Verify all translation units can access PCH",
            "5. Measure build time improvement"
        };

        consolidated.caveats = {
            "PCH must be included first in all source files",
            "Changes to PCH file will trigger full rebuild",
            "PCH size impacts memory usage during compilation",
            "Not all compilers support PCH equally well"
        };

        consolidated.verification =
            "Measure clean build time before and after PCH. "
            "Verify incremental build times haven't regressed. "
            "Check PCH memory usage doesn't exceed limits. "
            "Ensure all source files compile correctly.";

        consolidated.is_safe = true;
        consolidated.confidence = 0.85;

        Duration total_savings = Duration::zero();
        double total_percent = 0.0;
        for (const auto& sug : suggestions) {
            total_savings += sug.estimated_savings;
            total_percent += sug.estimated_savings_percent;
        }
        consolidated.estimated_savings = total_savings;
        consolidated.estimated_savings_percent = total_percent;

        consolidated.target_file.path = "pch.h";
        consolidated.target_file.action = FileAction::Create;
        consolidated.target_file.note = "Create or update precompiled header";

        const auto normalize_external_header = [](const std::string& header) -> std::string {
            if (header.empty()) {
                return header;
            }
            std::string trimmed = header;
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
                trimmed.erase(trimmed.begin());
            }
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
                trimmed.pop_back();
            }
            if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
                trimmed = trimmed.substr(1, trimmed.size() - 2);
            }
            if (!trimmed.empty() && trimmed.front() == '<' && trimmed.back() == '>') {
                return trimmed;
            }
            if (!trimmed.empty() && trimmed.front() == '/') {
                std::string include_path;
                const std::string marker = "/include/";
                const auto pos = trimmed.rfind(marker);
                if (pos != std::string::npos) {
                    include_path = trimmed.substr(pos + marker.size());
                } else {
                    include_path = fs::path(trimmed).filename().string();
                }
                if (!include_path.empty()) {
                    return "<" + include_path + ">";
                }
            }
            return trimmed;
        };

        // Generate a single consolidated edit that creates pch.h with all headers
        // instead of merging individual "create file" edits
        std::ostringstream pch_content;
        pch_content << "#pragma once\n\n";

        std::size_t header_count = 0;
        if (!external_headers.empty()) {
            pch_content << "// External/System Headers\n";
            std::vector sorted_external(external_headers.begin(), external_headers.end());
            std::ranges::sort(sorted_external);
            for (const auto& header : sorted_external) {
                const std::string include_header = normalize_external_header(header);
                if (include_header.find('<') == 0 || include_header.find('>') != std::string::npos) {
                    pch_content << "#include " << include_header << "\n";
                } else {
                    pch_content << "#include \"" << include_header << "\"\n";
                }
                ++header_count;
            }
            pch_content << "\n";
        }

        if (!stable_headers.empty()) {
            pch_content << "// Project Headers\n";
            std::vector sorted_stable(stable_headers.begin(), stable_headers.end());
            std::ranges::sort(sorted_stable);
            for (const auto& header : sorted_stable) {
                pch_content << "#include \"" << normalize_project_header(header) << "\"\n";
                ++header_count;
            }
        }

        const fs::path pch_path = "pch.h";
        consolidated.edits.push_back(make_replace_file_edit(pch_path, pch_content.str()));

        append_build_system_targets(suggestions, consolidated.secondary_files);
        append_build_system_edits(suggestions, consolidated.edits);
        consolidated.edits = merge_text_edits(std::move(consolidated.edits));

        std::ostringstream title;
        title << "Precompiled Header Optimization ("
              << header_count
              << " headers)";
        consolidated.title = title.str();

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_header_split(
        const std::vector<Suggestion>& suggestions
    ) {
        if (suggestions.empty()) {
            return std::nullopt;
        }

        Suggestion consolidated;
        consolidated.type = SuggestionType::HeaderSplit;
        consolidated.priority = Priority::Medium;

        std::unordered_map<std::string, std::vector<const Suggestion*>> by_file;

        for (const auto& sug : suggestions) {
            if (sug.priority >= Priority::High) {
                consolidated.priority = Priority::High;
            }
            const std::string key = make_repo_relative(sug.target_file.path);
            by_file[key].push_back(&sug);
        }

        std::ostringstream desc;
        desc << "Split large headers to reduce compilation dependencies and improve build times.\n\n";

        for (const auto& [file, sug_list] : by_file) {
            desc << "**" << file << ":**\n";
            desc << "  - Currently included by " << sug_list[0]->impact.total_files_affected << " files\n";
            desc << "  - Suggested split:\n";

            std::unordered_set<std::string> suggestions_seen;
            for (const auto* sug : sug_list) {
                if (!sug->description.empty() && !suggestions_seen.contains(sug->description)) {
                    desc << "    * " << sug->description << "\n";
                    suggestions_seen.insert(sug->description);
                }
            }
            desc << "\n";
        }

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);
        consolidated.title = "Header Splitting Opportunities (" + std::to_string(by_file.size()) + " headers)";

        std::ostringstream rationale;
        rationale << "Large headers with many dependencies force excessive recompilation. "
                  << "Splitting them into focused interfaces reduces build cascades.";
        consolidated.rationale = rationale.str();

        consolidated.implementation_steps = merge_steps(suggestions);
        consolidated.is_safe = false;
        consolidated.confidence = 0.7;

        consolidated.edits = merge_edits(suggestions);

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_unity_build(
        const std::vector<Suggestion>& suggestions
    ) const {
        if (suggestions.empty()) {
            return std::nullopt;
        }

        Suggestion consolidated;
        consolidated.type = SuggestionType::UnityBuild;
        consolidated.priority = Priority::Medium;

        std::unordered_set<std::string> all_files;
        for (const auto& sug : suggestions) {
            for (const auto& sec : sug.secondary_files) {
                if (!sec.path.empty()) {
                    const fs::path resolved = resolve_source_path(sec.path);
                    all_files.insert(resolved.string());
                }
            }
            if (sug.secondary_files.empty() && !sug.target_file.path.empty()) {
                const fs::path resolved = resolve_source_path(sug.target_file.path);
                all_files.insert(resolved.string());
            }
        }

        std::unordered_set<std::string> source_files;
        for (const auto& file : all_files) {
            if (is_source_file_path(fs::path(file))) {
                source_files.insert(file);
            }
        }

        if (source_files.empty()) {
            return std::nullopt;
        }

        const std::size_t files_per_group = std::max<std::size_t>(1, options_.max_items_per_suggestion / 3);
        std::vector sorted_files(source_files.begin(), source_files.end());
        std::ranges::sort(sorted_files);

        fs::path unity_base_dir;
        for (const auto& file : sorted_files) {
            const fs::path path(file);
            if (!path.is_absolute()) {
                continue;
            }
            unity_base_dir = find_repository_root(path);
            if (!unity_base_dir.empty()) {
                if (fs::exists(unity_base_dir / "src")) {
                    unity_base_dir /= "src";
                }
                break;
            }
        }
        if (unity_base_dir.empty()) {
            unity_base_dir = compute_common_directory(source_files);
        }

        // Generate unity build file edits
        bool first_unity = true;
        std::size_t group_num = 1;
        for (std::size_t i = 0; i < sorted_files.size(); i += files_per_group) {
            const fs::path unity_path = unity_base_dir.empty()
                ? fs::path("unity_build_" + std::to_string(group_num) + ".cpp")
                : unity_base_dir / ("unity_build_" + std::to_string(group_num) + ".cpp");

            std::ostringstream unity_content;
            unity_content << "// unity_build_" << group_num << ".cpp\n";
            unity_content << "// Generated unity build file - combines multiple translation units\n\n";
            const fs::path unity_dir = unity_path.parent_path();
            for (std::size_t j = i; j < std::min(i + files_per_group, sorted_files.size()); ++j) {
                const fs::path source_path = resolve_source_path(fs::path(sorted_files[j]));
                std::error_code ec;
                fs::path rel = fs::relative(source_path, unity_dir, ec);
                if (ec || rel.empty()) {
                    rel = resolve_unity_include_path(source_path);
                }
                unity_content << "#include \"" << rel.generic_string() << "\"\n";
            }

            consolidated.edits.push_back(make_replace_file_edit(unity_path, unity_content.str()));

            FileTarget unity_target;
            unity_target.path = unity_path;
            unity_target.action = FileAction::Create;
            unity_target.note = "Create unity build file";
            if (first_unity) {
                consolidated.target_file = unity_target;
                first_unity = false;
            } else {
                consolidated.secondary_files.push_back(unity_target);
            }

            ++group_num;
        }

        std::ostringstream desc;
        desc << "Group compatible source files into unity builds to reduce compilation overhead.\n\n";
        desc << "**Summary:** " << all_files.size() << " source files grouped into "
             << (group_num - 1) << " unity build files.\n\n";
        desc << "**Build system changes needed:**\n";
        desc << "  - Remove original source files from build targets\n";
        desc << "  - Add unity_build_*.cpp files to build targets\n\n";
        desc << "See the **Text Edits** section below for the generated unity build files.";

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);
        consolidated.title = "Unity Build Optimization (" + std::to_string(all_files.size()) + " files)";

        consolidated.rationale = "Unity builds reduce header parsing overhead by combining source files. "
                                 "Best for files with similar dependencies.";

        consolidated.implementation_steps = {
            "1. Create unity build source files (unity_build_*.cpp)",
            "2. Add #include directives for grouped source files",
            "3. Remove original source files from build targets",
            "4. Add unity build files to build targets",
            "5. Verify no symbol conflicts or ODR violations",
            "6. Measure build time improvement"
        };

        consolidated.is_safe = false;
        consolidated.confidence = 0.65;

        Duration total_savings = Duration::zero();
        double total_percent = 0.0;
        for (const auto& sug : suggestions) {
            total_savings += sug.estimated_savings;
            total_percent += sug.estimated_savings_percent;
        }
        consolidated.estimated_savings = total_savings;
        consolidated.estimated_savings_percent = total_percent;

        append_build_system_targets(suggestions, consolidated.secondary_files);
        append_build_system_edits(suggestions, consolidated.edits);
        consolidated.edits = merge_text_edits(std::move(consolidated.edits));

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_include_removal(
        const std::vector<Suggestion>& suggestions
    ) {
        if (suggestions.empty()) {
            return std::nullopt;
        }

        Suggestion consolidated;
        consolidated.type = SuggestionType::IncludeRemoval;
        consolidated.priority = Priority::Low;

        std::ostringstream desc;
        desc << "Remove unused includes to reduce compilation dependencies.\n\n";

        std::unordered_map<std::string, std::vector<std::string>> by_source;
        for (const auto& sug : suggestions) {
            const std::string source = make_repo_relative(sug.target_file.path);
            if (!sug.description.empty()) {
                by_source[source].push_back(sug.description);
            }
        }

        for (const auto& [source, includes] : by_source) {
            desc << "**" << source << ":**\n";
            for (const auto& inc : includes) {
                desc << "  - Remove: " << inc << "\n";
            }
            desc << "\n";
        }

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);
        consolidated.title = "Include Cleanup (" + std::to_string(suggestions.size()) + " includes)";
        consolidated.rationale = "Removing unused includes reduces compilation time and dependencies.";
        const bool all_safe = std::ranges::all_of(
            suggestions,
            [](const Suggestion& s) { return s.is_safe; }
        );
        consolidated.is_safe = all_safe;
        consolidated.confidence = all_safe ? 0.98 : 0.75;
        consolidated.target_file = suggestions.front().target_file;

        consolidated.edits = merge_edits(suggestions);

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_forward_decl(
        const std::vector<Suggestion>& suggestions
    ) {
        if (suggestions.empty()) {
            return std::nullopt;
        }

        Suggestion consolidated;
        consolidated.type = SuggestionType::ForwardDeclaration;
        consolidated.priority = Priority::Medium;

        std::ostringstream desc;
        desc << "Replace includes with forward declarations where possible.\n\n";

        for (const auto& sug : suggestions) {
            if (!sug.target_file.path.empty()) {
                desc << "**" << make_repo_relative(sug.target_file.path) << ":**\n";
                desc << sug.description << "\n\n";
            }
        }

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);
        consolidated.title = "Forward Declaration Opportunities (" + std::to_string(suggestions.size()) + " locations)";
        consolidated.is_safe = false;
        consolidated.confidence = 0.7;

        consolidated.edits = merge_edits(suggestions);

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_template(
        const std::vector<Suggestion>& suggestions
    ) {
        if (suggestions.empty()) {
            return std::nullopt;
        }

        Suggestion consolidated;
        consolidated.type = SuggestionType::ExplicitTemplate;
        consolidated.priority = Priority::Medium;

        // Build template_instantiations.cpp content
        std::ostringstream tmpl_content;
        tmpl_content << "// template_instantiations.cpp\n";
        tmpl_content << "// Explicit template instantiations to reduce compile-time overhead\n\n";

        std::unordered_set<std::string> added_includes;
        std::unordered_set<std::string> instantiations;
        std::unordered_set<std::string> extern_decls;

        const std::regex inst_regex(R"(template\s+class\s+[^;]+;)");
        const std::regex extern_regex(R"(extern\s+template\s+class\s+[^;]+;)");

        auto capture_templates = [&](const std::string& code) {
            for (std::sregex_iterator it(code.begin(), code.end(), inst_regex), end; it != end; ++it) {
                instantiations.insert(it->str());
            }
            for (std::sregex_iterator it(code.begin(), code.end(), extern_regex), end; it != end; ++it) {
                extern_decls.insert(it->str());
            }
        };

        for (const auto& sug : suggestions) {
            for (const auto& edit : sug.edits) {
                if (!edit.new_text.empty()) {
                    capture_templates(edit.new_text);
                }

                if (!edit.file.empty()) {
                    const auto ext = edit.file.extension().string();
                    if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
                        const std::string include_path = make_repo_relative(edit.file);
                        if (!added_includes.contains(include_path)) {
                            tmpl_content << "#include \"" << include_path << "\"\n";
                            added_includes.insert(include_path);
                        }
                    }
                }
            }

            for (const auto& step : sug.implementation_steps) {
                if (step.find("template class ") != std::string::npos ||
                    step.find("extern template class ") != std::string::npos) {
                    capture_templates(step);
                }
            }
        }
        tmpl_content << "\n// Explicit template instantiations\n";

        std::vector<std::string> ordered_instantiations(instantiations.begin(), instantiations.end());
        std::ranges::sort(ordered_instantiations);
        for (const auto& inst : ordered_instantiations) {
            tmpl_content << inst << "\n";
        }

        fs::path inst_path;
        for (const auto& sug : suggestions) {
            if (!sug.target_file.path.empty() &&
                sug.target_file.path.filename() == "template_instantiations.cpp") {
                inst_path = sug.target_file.path;
                break;
            }
        }
        if (inst_path.empty()) {
            inst_path = "template_instantiations.cpp";
        }

        // Generate the template_instantiations.cpp edit
        const TextEdit tmpl_edit = make_replace_file_edit(inst_path, tmpl_content.str());

        std::ostringstream desc;
        desc << "Explicitly instantiate frequently-used templates to reduce instantiation overhead.\n\n";
        desc << "**Summary:** " << suggestions.size() << " templates will be instantiated once instead of multiple times.\n\n";

        desc << "See the **Text Edits** section below for the generated template_instantiations.cpp file.";

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);
        consolidated.title = "Template Instantiation Optimization (" + std::to_string(suggestions.size()) + " templates)";
        consolidated.is_safe = true;
        consolidated.confidence = 0.8;

        std::ostringstream rationale;
        rationale << "These templates are instantiated multiple times across the codebase. "
                  << "Explicit instantiation compiles the template once and reuses it everywhere.";
        consolidated.rationale = rationale.str();

        consolidated.implementation_steps = {
            "1. Create template_instantiations.cpp with explicit instantiations",
            "2. Add extern template declarations in corresponding headers",
            "3. Add template_instantiations.cpp to your build system",
            "4. Rebuild and verify all instantiations are found at link time",
            "5. Measure compilation time improvement"
        };

        Duration total_savings = Duration::zero();
        for (const auto& sug : suggestions) {
            total_savings += sug.estimated_savings;
        }
        consolidated.estimated_savings = total_savings;

        consolidated.edits = merge_edits(suggestions);
        consolidated.edits.erase(
            std::remove_if(consolidated.edits.begin(), consolidated.edits.end(),
                [](const TextEdit& edit) {
                    return edit.file.filename() == "template_instantiations.cpp";
                }),
            consolidated.edits.end()
        );
        consolidated.edits.push_back(tmpl_edit);

        consolidated.target_file.path = inst_path;
        consolidated.target_file.action = FileAction::Create;
        consolidated.target_file.note = "Create explicit instantiation file";

        append_build_system_targets(suggestions, consolidated.secondary_files);
        append_build_system_edits(suggestions, consolidated.edits);
        consolidated.edits = merge_text_edits(std::move(consolidated.edits));

        return consolidated;
    }

    Impact SuggestionConsolidator::merge_impacts(
        const std::vector<Suggestion>& suggestions
    )
    {
        Impact merged;

        for (const auto& sug : suggestions) {
            merged.files_benefiting.insert(
                merged.files_benefiting.end(),
                sug.impact.files_benefiting.begin(),
                sug.impact.files_benefiting.end()
            );
            merged.total_files_affected += sug.impact.total_files_affected;
            merged.cumulative_savings += sug.impact.cumulative_savings;
            merged.rebuild_files_count += sug.impact.rebuild_files_count;
        }

        std::ranges::sort(merged.files_benefiting);
        auto unique_end = std::ranges::unique(merged.files_benefiting);
        merged.files_benefiting.erase(unique_end.begin(), merged.files_benefiting.end());

        return merged;
    }

    std::vector<std::string> SuggestionConsolidator::merge_steps(
        const std::vector<Suggestion>& suggestions
    )
    {
        std::unordered_set<std::string> unique_steps;
        std::vector<std::string> merged;

        for (const auto& sug : suggestions) {
            for (const auto& step : sug.implementation_steps) {
                if (!unique_steps.contains(step)) {
                    unique_steps.insert(step);
                    merged.push_back(step);
                }
            }
        }

        return merged;
    }

    std::vector<TextEdit> SuggestionConsolidator::merge_edits(
        const std::vector<Suggestion>& suggestions
    ) {
        std::vector<TextEdit> all_edits;

        for (const auto& sug : suggestions) {
            all_edits.insert(all_edits.end(), sug.edits.begin(), sug.edits.end());
        }

        if (all_edits.empty()) {
            return all_edits;
        }

        std::ranges::sort(all_edits, [](const TextEdit& a, const TextEdit& b) {
            if (a.file != b.file) {
                return a.file < b.file;
            }
            if (a.start_line != b.start_line) {
                return a.start_line < b.start_line;
            }
            return a.start_col < b.start_col;
        });

        std::vector<TextEdit> merged;
        merged.reserve(all_edits.size());

        for (const auto& edit : all_edits) {
            bool conflict = false;

            for (const auto& existing : merged) {
                if (existing.file != edit.file) {
                    continue;
                }

                const bool overlaps =
                    (edit.start_line < existing.end_line ||
                     (edit.start_line == existing.end_line && edit.start_col < existing.end_col)) &&
                    (edit.end_line > existing.start_line ||
                     (edit.end_line == existing.start_line && edit.end_col > existing.start_col));

                if (overlaps) {
                    conflict = true;
                    break;
                }
            }

            if (!conflict) {
                merged.push_back(edit);
            }
        }

        std::ranges::sort(merged, [](const TextEdit& a, const TextEdit& b) {
            if (a.file != b.file) {
                return a.file < b.file;
            }
            if (a.start_line != b.start_line) {
                return b.start_line < a.start_line;
            }
            return b.start_col < a.start_col;
        });

        return merged;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_pimpl(
        const std::vector<Suggestion>& suggestions
    ) {
        if (suggestions.empty()) {
            return std::nullopt;
        }
        if (suggestions.size() == 1) {
            return suggestions.front();
        }

        Suggestion consolidated;
        consolidated.type = SuggestionType::PIMPLPattern;
        consolidated.priority = Priority::Medium;

        // Group by module/directory for better organization
        std::unordered_map<std::string, std::vector<const Suggestion*>> by_module;

        for (const auto& sug : suggestions) {
            if (sug.priority == Priority::Critical || sug.priority == Priority::High) {
                consolidated.priority = Priority::High;
            }

            std::string module_key = "default";
            if (!sug.target_file.path.empty()) {
                // Use parent directory as module grouping
                if (sug.target_file.path.has_parent_path()) {
                    module_key = sug.target_file.path.parent_path().filename().string();
                }
            }
            by_module[module_key].push_back(&sug);
        }

        std::ostringstream desc;
        desc << "Apply PIMPL (Pointer to Implementation) pattern to reduce compile-time coupling.\n\n";
        desc << "The PIMPL idiom moves private implementation details to a separate compilation unit, "
             << "reducing header dependencies and improving incremental build times.\n\n";

        Duration total_savings = Duration::zero();
        double total_percent = 0.0;

        for (const auto& [module, sug_list] : by_module) {
            desc << "**Module: " << module << "** (" << sug_list.size() << " candidates)\n";

            for (const auto* sug : sug_list) {
                desc << "  - `" << sug->target_file.path.filename().string() << "`";
                if (sug->confidence > 0) {
                    desc << " (confidence: " << static_cast<int>(sug->confidence * 100) << "%)";
                }
                desc << "\n";

                total_savings += sug->estimated_savings;
                total_percent += sug->estimated_savings_percent;
            }
            desc << "\n";
        }

        desc << "**Implementation Pattern:**\n```cpp\n";
        desc << "// header.h\n";
        desc << "class MyClass {\n";
        desc << "public:\n";
        desc << "    MyClass();\n";
        desc << "    ~MyClass();\n";
        desc << "    // ... public interface\n";
        desc << "private:\n";
        desc << "    struct Impl;\n";
        desc << "    std::unique_ptr<Impl> pimpl_;\n";
        desc << "};\n\n";
        desc << "// source.cpp\n";
        desc << "struct MyClass::Impl {\n";
        desc << "    // ... private members and implementation\n";
        desc << "};\n";
        desc << "```\n";

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);
        consolidated.title = "PIMPL Pattern Opportunities (" + std::to_string(suggestions.size()) + " classes)";

        consolidated.rationale = "These classes have significant private implementation details that cause "
                                 "recompilation cascades when modified. Applying PIMPL decouples the "
                                 "interface from implementation, reducing incremental build times.";

        consolidated.implementation_steps = {
            "1. Create a forward-declared Impl struct in the class header",
            "2. Replace private members with std::unique_ptr<Impl>",
            "3. Move implementation details to the .cpp file",
            "4. Implement constructor/destructor in .cpp (after Impl is complete)",
            "5. Update any member functions that access private data",
            "6. Verify ABI compatibility if this is a library interface"
        };

        consolidated.caveats = {
            "PIMPL adds one level of indirection (minor performance cost)",
            "Requires heap allocation for Impl object",
            "Cannot be used with classes that need to be trivially copyable",
            "Move semantics require explicit implementation"
        };

        consolidated.verification =
            "Measure incremental build time after modifying private implementation. "
            "Verify no functionality regression. Check for memory leaks with sanitizers.";

        consolidated.is_safe = false;
        consolidated.confidence = 0.75;
        consolidated.estimated_savings = total_savings;
        consolidated.estimated_savings_percent = total_percent;

        consolidated.edits = merge_edits(suggestions);

        return consolidated;
    }

}  // namespace bha::suggestions
