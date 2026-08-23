//
// Created by gregorian-rayne on 01/18/26.
//

#include "bha/suggestions/consolidator.hpp"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <sstream>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {
        namespace fs = std::filesystem;

        std::vector<Suggestion> group_by_type(
            const std::vector<Suggestion>& suggestions,
            SuggestionType type
        ) {
            std::vector<Suggestion> filtered;
            std::ranges::copy_if(suggestions, std::back_inserter(filtered),
                [type](const Suggestion& s) { return s.type == type; });
            return filtered;
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
            SuggestionType::PIMPLPattern
        }) {
            auto group = group_by_type(suggestions, type);
            if (group.empty()) {
                continue;
            }

            std::optional<Suggestion> result;

            switch (type) {
            case SuggestionType::PCHOptimization:
                // PCH feasibility is compiler/build-system specific. Preserve
                // each producer-backed advisory instead of synthesizing a
                // build-file edit from header names or prose.
                consolidated.insert(consolidated.end(), group.begin(), group.end());
                continue;
            case SuggestionType::HeaderSplit:
                result = consolidate_header_split(group);
                break;
            case SuggestionType::UnityBuild:
                // Unity suggestions already contain an exact target-scoped edit;
                // never replace it with the retired generated-file workflow.
                consolidated.insert(consolidated.end(), group.begin(), group.end());
                continue;
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
            }

            if (result.has_value()) {
                consolidated.push_back(std::move(result.value()));
            }
        }

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
