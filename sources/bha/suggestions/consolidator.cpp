//
// Created by gregorian-rayne on 01/18/26.
//

#include "bha/suggestions/consolidator.hpp"

#include <algorithm>
#include <ranges>
#include <sstream>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {

        std::vector<Suggestion> group_by_type(
            const std::vector<Suggestion>& suggestions,
            SuggestionType type
        ) {
            std::vector<Suggestion> filtered;
            std::ranges::copy_if(suggestions, std::back_inserter(filtered),
                [type](const Suggestion& s) { return s.type == type; });
            return filtered;
        }

        [[maybe_unused]] std::string format_duration_estimate(const Duration d) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
            if (ms < 1000) {
                return std::to_string(ms) + "ms";
            }
            const auto sec = ms / 1000;
            if (sec < 60) {
                return std::to_string(sec) + "s";
            }
            const auto min = sec / 60;
            return std::to_string(min) + "m " + std::to_string(sec % 60) + "s";
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
            if (group.empty()) continue;

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
                result = consolidate_template(group);
                break;
            case SuggestionType::PIMPLPattern:
                result = consolidate_pimpl(group);
                break;
            case SuggestionType::MoveToCpp:
                result = consolidate_move_to_cpp(group);
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
        std::unordered_set<std::string> volatile_headers;
        std::unordered_set<std::string> external_headers;

        for (const auto& sug : suggestions) {
            if (sug.priority == Priority::Critical) {
                consolidated.priority = Priority::Critical;
            }

            if (!sug.target_file.path.empty()) {
                if (std::string header_str = sug.target_file.path.string(); header_str.find('<') == 0 ||
                    header_str.find("/usr/") == 0 ||
                    header_str.find("third_party") != std::string::npos) {
                    external_headers.insert(header_str);
                } else if (sug.rationale.find("volatile") != std::string::npos ||
                           sug.rationale.find("frequently modified") != std::string::npos) {
                    volatile_headers.insert(header_str);
                } else {
                    stable_headers.insert(header_str);
                }
            }

            for (const auto& secondary : sug.secondary_files) {
                std::string header_str = secondary.path.string();
                if (header_str.ends_with("pch.h") || header_str.ends_with("stdafx.h")) {
                    continue;
                }
                if (header_str.find('<') == 0 ||
                    header_str.find("/usr/") == 0 ||
                    header_str.find("third_party") != std::string::npos) {
                    external_headers.insert(header_str);
                } else {
                    stable_headers.insert(header_str);
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
                if (header.find('<') == 0 || header.find('>') != std::string::npos) {
                    pch_content << "#include " << header << "\n";
                } else {
                    pch_content << "#include \"" << header << "\"\n";
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
                pch_content << "#include \"" << header << "\"\n";
                ++header_count;
            }
        }

        TextEdit create_pch;
        create_pch.file = "pch.h";
        create_pch.start_line = 0;
        create_pch.start_col = 0;
        create_pch.end_line = 0;
        create_pch.end_col = 0;
        create_pch.new_text = pch_content.str();
        consolidated.edits.push_back(create_pch);

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
            std::string key = sug.target_file.path.string();
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
            if (!sug.target_file.path.empty()) {
                all_files.insert(sug.target_file.path.string());
            }
            for (const auto& sec : sug.secondary_files) {
                if (!sec.path.empty()) {
                    all_files.insert(sec.path.string());
                }
            }
        }

        const std::size_t files_per_group = options_.max_items_per_suggestion / 3;
        std::vector sorted_files(all_files.begin(), all_files.end());
        std::ranges::sort(sorted_files);

        // Generate unity build file edits
        std::size_t group_num = 1;
        for (std::size_t i = 0; i < sorted_files.size(); i += files_per_group) {
            std::ostringstream unity_content;
            unity_content << "// unity_build_" << group_num << ".cpp\n";
            unity_content << "// Generated unity build file - combines multiple translation units\n\n";
            for (std::size_t j = i; j < std::min(i + files_per_group, sorted_files.size()); ++j) {
                unity_content << "#include \"" << sorted_files[j] << "\"\n";
            }

            TextEdit unity_edit;
            unity_edit.file = "unity_build_" + std::to_string(group_num) + ".cpp";
            unity_edit.start_line = 0;
            unity_edit.start_col = 0;
            unity_edit.end_line = 0;
            unity_edit.end_col = 0;
            unity_edit.new_text = unity_content.str();
            consolidated.edits.push_back(unity_edit);

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
            std::string source = sug.target_file.path.string();
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
        consolidated.is_safe = false;
        consolidated.confidence = 0.75;

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
                desc << "**" << sug.target_file.path.string() << ":**\n";
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
        for (const auto& sug : suggestions) {
            if (!sug.target_file.path.empty()) {
                std::string include_path = sug.target_file.path.string();
                if (include_path.find(".cpp") != std::string::npos) {
                    include_path.replace(include_path.find(".cpp"), 4, ".h");
                } else if (include_path == "template_instantiations.cpp") {
                    continue;
                }

                if (!added_includes.contains(include_path)) {
                    tmpl_content << "#include \"" << include_path << "\"\n";
                    added_includes.insert(include_path);
                }
            }
        }
        tmpl_content << "\n// Explicit template instantiations\n";

        std::vector<std::string> instantiations;
        std::vector<std::string> extern_decls;

        for (const auto& sug : suggestions) {
            std::string code = sug.after_code.code;
            if (size_t start = code.find("template class "); start != std::string::npos) {
                if (size_t end = code.find(';', start); end != std::string::npos) {
                    instantiations.push_back(code.substr(start, end - start + 1));
                }
            }
            if (size_t start = code.find("extern template class "); start != std::string::npos) {
                if (size_t end = code.find(';', start); end != std::string::npos) {
                    extern_decls.push_back(code.substr(start, end - start + 1));
                }
            }
        }

        for (const auto& inst : instantiations) {
            tmpl_content << inst << "\n";
        }

        // Generate the template_instantiations.cpp edit
        TextEdit tmpl_edit;
        tmpl_edit.file = "template_instantiations.cpp";
        tmpl_edit.start_line = 0;
        tmpl_edit.start_col = 0;
        tmpl_edit.end_line = 0;
        tmpl_edit.end_col = 0;
        tmpl_edit.new_text = tmpl_content.str();
        consolidated.edits.push_back(tmpl_edit);

        std::ostringstream desc;
        desc << "Explicitly instantiate frequently-used templates to reduce instantiation overhead.\n\n";
        desc << "**Summary:** " << suggestions.size() << " templates will be instantiated once instead of multiple times.\n\n";

        if (!extern_decls.empty()) {
            desc << "**Note:** Add these extern declarations to corresponding headers to prevent implicit instantiation:\n";
            for (const auto& decl : extern_decls) {
                desc << "  - `" << decl << "`\n";
            }
            desc << "\n";
        }

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

                bool overlaps =
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

    std::optional<Suggestion> SuggestionConsolidator::consolidate_move_to_cpp(
        const std::vector<Suggestion>& suggestions
    ) {
        if (suggestions.empty()) {
            return std::nullopt;
        }

        Suggestion consolidated;
        consolidated.type = SuggestionType::MoveToCpp;
        consolidated.priority = Priority::Low;

        std::unordered_map<std::string, std::vector<std::string>> by_header;

        for (const auto& sug : suggestions) {
            if (sug.priority >= Priority::High) {
                consolidated.priority = Priority::Medium;
            }

            std::string header = sug.target_file.path.string();
            if (!sug.description.empty()) {
                by_header[header].push_back(sug.description);
            }
        }

        std::ostringstream desc;
        desc << "Move function implementations from headers to source files to reduce compilation overhead.\n\n";

        for (const auto& [header, items] : by_header) {
            desc << "**" << header << ":**\n";
            for (const auto& item : items) {
                desc << "  - " << item << "\n";
            }
            desc << "\n";
        }

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);
        consolidated.title = "Move to Source File (" + std::to_string(suggestions.size()) + " items)";
        consolidated.rationale = "Function definitions in headers are re-parsed and re-compiled "
                                 "in every translation unit that includes them.";

        consolidated.implementation_steps = {
            "1. Identify function definitions that don't need to be inline",
            "2. Move implementation to corresponding .cpp file",
            "3. Keep only declaration in header",
            "4. Verify linkage is correct (no ODR violations)"
        };

        consolidated.is_safe = false;
        consolidated.confidence = 0.7;

        Duration total_savings = Duration::zero();
        for (const auto& sug : suggestions) {
            total_savings += sug.estimated_savings;
        }
        consolidated.estimated_savings = total_savings;

        consolidated.edits = merge_edits(suggestions);

        return consolidated;
    }

}  // namespace bha::suggestions
