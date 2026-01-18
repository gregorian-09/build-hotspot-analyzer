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

        [[maybe_unused]] std::string format_duration_estimate(Duration d) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
            if (ms < 1000) {
                return std::to_string(ms) + "ms";
            }
            auto sec = ms / 1000;
            if (sec < 60) {
                return std::to_string(sec) + "s";
            }
            auto min = sec / 60;
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

        for (auto type : {
            SuggestionType::PCHOptimization,
            SuggestionType::HeaderSplit,
            SuggestionType::UnityBuild,
            SuggestionType::IncludeRemoval,
            SuggestionType::ForwardDeclaration,
            SuggestionType::ExplicitTemplate,
            SuggestionType::DependencyInversion
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
            default:
                continue;
            }

            if (result.has_value()) {
                consolidated.push_back(std::move(result.value()));
            }
        }

        for (const auto& s : suggestions) {
            if (s.type != SuggestionType::PCHOptimization &&
                s.type != SuggestionType::HeaderSplit &&
                s.type != SuggestionType::UnityBuild &&
                s.type != SuggestionType::IncludeRemoval &&
                s.type != SuggestionType::ForwardDeclaration &&
                s.type != SuggestionType::ExplicitTemplate) {
                consolidated.push_back(s);
            }
        }

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_pch(
        const std::vector<Suggestion>& suggestions
    ) const {
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
                std::string header_str = sug.target_file.path.string();

                if (header_str.find("<") == 0 ||
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
                if (header_str.find("<") == 0 ||
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

        if (!external_headers.empty()) {
            desc << "**Add to precompiled header (external/stable libraries):**\n";
            std::vector<std::string> sorted_external(external_headers.begin(), external_headers.end());
            std::ranges::sort(sorted_external);
            for (const auto& header : sorted_external) {
                desc << "  - " << header << "\n";
            }
            desc << "\n";
        }

        if (!stable_headers.empty()) {
            desc << "**Add to precompiled header (project stable headers):**\n";
            std::vector<std::string> sorted_stable(stable_headers.begin(), stable_headers.end());
            std::ranges::sort(sorted_stable);
            for (const auto& header : sorted_stable) {
                desc << "  - " << header << "\n";
            }
            desc << "\n";
        }

        if (!volatile_headers.empty()) {
            desc << "**Exclude from PCH (volatile headers):**\n";
            std::vector<std::string> sorted_volatile(volatile_headers.begin(), volatile_headers.end());
            std::ranges::sort(sorted_volatile);
            for (const auto& header : sorted_volatile) {
                desc << "  - " << header << " (frequently modified)\n";
            }
            desc << "\n";
        }

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);

        std::ostringstream title;
        title << "Precompiled Header Optimization ("
              << (stable_headers.size() + external_headers.size())
              << " headers)";
        consolidated.title = title.str();

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
            "   - MSVC: /Yu\"pch.h\" for source files, /Yc\"pch.h\" for PCH generation",
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

        std::ostringstream before_code;
        before_code << "// Current: These headers are included separately in each source file\n";
        before_code << "// causing redundant parsing across translation units\n\n";

        std::vector<std::string> example_headers;
        for (const auto& header : stable_headers) {
            example_headers.push_back(header);
            if (example_headers.size() >= 3) break;
        }
        for (const auto& header : external_headers) {
            example_headers.push_back(header);
            if (example_headers.size() >= 5) break;
        }

        if (!example_headers.empty()) {
            before_code << "// Example: file1.cpp\n";
            for (const auto& header : example_headers) {
                if (header.find('<') == 0 || header.find('>') != std::string::npos) {
                    before_code << "#include " << header << "\n";
                } else {
                    before_code << "#include \"" << header << "\"\n";
                }
            }
            before_code << "// ... other includes\n\n";
            before_code << "// Example: file2.cpp\n";
            for (const auto& header : example_headers) {
                if (header.find('<') == 0 || header.find('>') != std::string::npos) {
                    before_code << "#include " << header << "  // Parsed again!\n";
                } else {
                    before_code << "#include \"" << header << "\"  // Parsed again!\n";
                }
            }
            before_code << "\n// This pattern repeats across "
                       << consolidated.impact.total_files_affected << "+ files";
        }

        consolidated.before_code.file = "Current state";
        consolidated.before_code.code = before_code.str();

        std::ostringstream after_code;
        after_code << "// pch.h - Precompiled Header\n";
        after_code << "#pragma once\n\n";

        if (!external_headers.empty()) {
            after_code << "// External/System Headers (inherently stable)\n";
            std::vector<std::string> sorted_ext(external_headers.begin(), external_headers.end());
            std::ranges::sort(sorted_ext);
            for (const auto& header : sorted_ext) {
                if (header.find('<') == 0 || header.find('>') != std::string::npos) {
                    after_code << "#include " << header << "\n";
                } else {
                    after_code << "#include \"" << header << "\"\n";
                }
            }
            after_code << "\n";
        }

        if (!stable_headers.empty()) {
            after_code << "// Project Stable Headers (rarely modified)\n";
            std::vector<std::string> sorted_stable(stable_headers.begin(), stable_headers.end());
            std::ranges::sort(sorted_stable);
            for (const auto& header : sorted_stable) {
                after_code << "#include \"" << header << "\"\n";
            }
            after_code << "\n";
        }

        after_code << "// Then in your source files:\n";
        after_code << "// file1.cpp, file2.cpp, etc.\n";
        after_code << "#include \"pch.h\"  // Parsed once, reused everywhere";

        consolidated.after_code.file = "pch.h (proposed)";
        consolidated.after_code.code = after_code.str();

        consolidated.target_file.path = "pch.h";
        consolidated.target_file.action = FileAction::Create;
        consolidated.target_file.note = "Create or update precompiled header";

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_header_split(
        const std::vector<Suggestion>& suggestions
    ) const {
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
            all_files.insert(sug.target_file.path.string());
            for (const auto& sec : sug.secondary_files) {
                all_files.insert(sec.path.string());
            }
        }

        std::ostringstream desc;
        desc << "Group compatible source files into unity builds to reduce compilation overhead.\n\n";
        desc << "**Suggested grouping (" << all_files.size() << " files):**\n\n";

        std::size_t group_num = 1;
        std::size_t files_per_group = options_.max_items_per_suggestion / 3;

        std::vector<std::string> sorted_files(all_files.begin(), all_files.end());
        std::ranges::sort(sorted_files);

        for (std::size_t i = 0; i < sorted_files.size(); i += files_per_group) {
            desc << "**unity_build_" << group_num << ".cpp:**\n";
            for (std::size_t j = i; j < std::min(i + files_per_group, sorted_files.size()); ++j) {
                desc << "  - " << sorted_files[j] << "\n";
            }
            desc << "\n";
            ++group_num;
        }

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

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_include_removal(
        const std::vector<Suggestion>& suggestions
    ) const {
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

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_forward_decl(
        const std::vector<Suggestion>& suggestions
    ) const {
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

        return consolidated;
    }

    std::optional<Suggestion> SuggestionConsolidator::consolidate_template(
        const std::vector<Suggestion>& suggestions
    ) const {
        if (suggestions.empty()) {
            return std::nullopt;
        }

        Suggestion consolidated;
        consolidated.type = SuggestionType::ExplicitTemplate;
        consolidated.priority = Priority::Medium;

        std::ostringstream desc;
        desc << "Explicitly instantiate frequently-used templates to reduce instantiation overhead.\n\n";

        for (const auto& sug : suggestions) {
            if (!sug.description.empty()) {
                desc << "- " << sug.description << "\n";
            }
        }

        consolidated.description = desc.str();
        consolidated.impact = merge_impacts(suggestions);
        consolidated.title = "Template Instantiation Optimization (" + std::to_string(suggestions.size()) + " templates)";
        consolidated.is_safe = true;
        consolidated.confidence = 0.8;

        return consolidated;
    }

    Impact SuggestionConsolidator::merge_impacts(
        const std::vector<Suggestion>& suggestions
    ) const {
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
    ) const {
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

}  // namespace bha::suggestions
