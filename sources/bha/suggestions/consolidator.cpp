//
// Created by gregorian-rayne on 01/18/26.
//

#include "bha/suggestions/consolidator.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <sstream>
#include <tuple>
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

        std::optional<double> conservative_confidence(
            const std::vector<Suggestion>& suggestions
        ) {
            if (suggestions.empty()) {
                return std::nullopt;
            }

            double result = suggestions.front().confidence;
            if (!std::isfinite(result) || result < 0.0 || result > 1.0) {
                return std::nullopt;
            }

            for (const auto& suggestion : suggestions) {
                if (!std::isfinite(suggestion.confidence) ||
                    suggestion.confidence < 0.0 || suggestion.confidence > 1.0) {
                    return std::nullopt;
                }
                result = std::min(result, suggestion.confidence);
            }
            return result;
        }

        bool position_less(
            const std::size_t line_a,
            const std::size_t col_a,
            const std::size_t line_b,
            const std::size_t col_b
        ) {
            return std::tie(line_a, col_a) < std::tie(line_b, col_b);
        }

        bool position_equal(
            const std::size_t line_a,
            const std::size_t col_a,
            const std::size_t line_b,
            const std::size_t col_b
        ) {
            return line_a == line_b && col_a == col_b;
        }

        bool edit_ranges_conflict(const TextEdit& left, const TextEdit& right) {
            if (left.file != right.file) {
                return false;
            }

            if (left.has_byte_range() && right.has_byte_range()) {
                const bool identical =
                    *left.byte_offset == *right.byte_offset &&
                    *left.byte_length == *right.byte_length &&
                    left.new_text == right.new_text;
                if (identical) {
                    return false;
                }

                const bool ranges_overlap = *left.byte_offset <= *right.byte_offset
                    ? *right.byte_offset - *left.byte_offset < *left.byte_length
                    : *left.byte_offset - *right.byte_offset < *right.byte_length;
                if (ranges_overlap) {
                    return true;
                }

                const auto point_inclusive = [](const TextEdit& edit, const TextEdit& insertion) {
                    const std::size_t point = *insertion.byte_offset;
                    if (point < *edit.byte_offset) {
                        return false;
                    }
                    return point - *edit.byte_offset <= *edit.byte_length;
                };
                if (*left.byte_length == 0 && point_inclusive(right, left)) {
                    return true;
                }
                if (*right.byte_length == 0 && point_inclusive(left, right)) {
                    return true;
                }
                return false;
            }

            const bool identical =
                position_equal(left.start_line, left.start_col, right.start_line, right.start_col) &&
                position_equal(left.end_line, left.end_col, right.end_line, right.end_col) &&
                left.new_text == right.new_text;
            if (identical) {
                return false;
            }

            const bool ranges_overlap =
                position_less(left.start_line, left.start_col, right.end_line, right.end_col) &&
                position_less(right.start_line, right.start_col, left.end_line, left.end_col);
            if (ranges_overlap) {
                return true;
            }

            const auto point_inclusive = [](const TextEdit& edit, const TextEdit& insertion) {
                const bool after_start = !position_less(
                    insertion.start_line, insertion.start_col,
                    edit.start_line, edit.start_col
                );
                const bool before_end = !position_less(
                    edit.end_line, edit.end_col,
                    insertion.start_line, insertion.start_col
                );
                return after_start && before_end;
            };

            if (left.is_insertion() && point_inclusive(right, left)) {
                return true;
            }
            if (right.is_insertion() && point_inclusive(left, right)) {
                return true;
            }
            return false;
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
        consolidated.title = "Header Splitting Opportunities (" + std::to_string(by_file.size()) + " headers)";

        std::ostringstream rationale;
        rationale << "Large headers with many dependencies force excessive recompilation. "
                  << "Splitting them into focused interfaces reduces build cascades.";
        consolidated.rationale = rationale.str();

        consolidated.implementation_steps = merge_steps(suggestions);
        consolidated.is_safe = false;
        const auto confidence = conservative_confidence(suggestions);
        const auto impact = merge_impacts(suggestions);
        const auto edits = merge_edits(suggestions);
        if (!confidence || !impact || !edits) {
            return std::nullopt;
        }
        consolidated.confidence = *confidence;
        consolidated.impact = *impact;

        consolidated.edits = *edits;
        consolidated.caveats.emplace_back(
            "Savings remain unavailable until a post-edit trace is captured"
        );

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
        const auto impact = merge_impacts(suggestions);
        const auto confidence = conservative_confidence(suggestions);
        const auto edits = merge_edits(suggestions);
        if (!impact || !confidence || !edits) {
            return std::nullopt;
        }
        consolidated.impact = *impact;
        consolidated.title = "Include Cleanup (" + std::to_string(suggestions.size()) + " includes)";
        consolidated.rationale = "Removing unused includes reduces compilation time and dependencies.";
        const bool all_safe = std::ranges::all_of(
            suggestions,
            [](const Suggestion& s) { return s.is_safe; }
        );
        consolidated.is_safe = all_safe;
        consolidated.confidence = *confidence;
        consolidated.target_file = suggestions.front().target_file;

        consolidated.edits = *edits;
        consolidated.caveats.emplace_back(
            "Savings remain unavailable until a post-edit trace is captured"
        );

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
        const auto impact = merge_impacts(suggestions);
        const auto confidence = conservative_confidence(suggestions);
        const auto edits = merge_edits(suggestions);
        if (!impact || !confidence || !edits) {
            return std::nullopt;
        }
        consolidated.impact = *impact;
        consolidated.title = "Forward Declaration Opportunities (" + std::to_string(suggestions.size()) + " locations)";
        consolidated.is_safe = false;
        consolidated.confidence = *confidence;

        consolidated.edits = *edits;
        consolidated.caveats.emplace_back(
            "Savings remain unavailable until a post-edit trace is captured"
        );

        return consolidated;
    }


    std::optional<Impact> SuggestionConsolidator::merge_impacts(
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
            const auto total_files = utils::checked_add(
                merged.total_files_affected,
                sug.impact.total_files_affected
            );
            const auto cumulative_savings = utils::checked_add_duration(
                merged.cumulative_savings,
                sug.impact.cumulative_savings
            );
            const auto rebuild_files = utils::checked_add(
                merged.rebuild_files_count,
                sug.impact.rebuild_files_count
            );
            if (!total_files || !cumulative_savings || !rebuild_files) {
                return std::nullopt;
            }
            merged.total_files_affected = *total_files;
            merged.cumulative_savings = *cumulative_savings;
            merged.rebuild_files_count = *rebuild_files;
        }

        std::ranges::sort(merged.files_benefiting);
        const auto unique_end = std::unique(
            merged.files_benefiting.begin(), merged.files_benefiting.end());
        merged.files_benefiting.erase(unique_end, merged.files_benefiting.end());

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

    std::optional<std::vector<TextEdit>> SuggestionConsolidator::merge_edits(
        const std::vector<Suggestion>& suggestions
    ) {
        std::vector<TextEdit> all_edits;

        for (const auto& sug : suggestions) {
            all_edits.insert(all_edits.end(), sug.edits.begin(), sug.edits.end());
        }

        if (all_edits.empty()) {
            return all_edits;
        }

        if (std::ranges::any_of(
                all_edits,
                [](const TextEdit& edit) { return edit.has_partial_byte_range(); }
            )) {
            return std::nullopt;
        }

        for (std::size_t i = 0; i < all_edits.size(); ++i) {
            for (std::size_t j = i + 1; j < all_edits.size(); ++j) {
                if (all_edits[i].file == all_edits[j].file &&
                    all_edits[i].has_byte_range() != all_edits[j].has_byte_range()) {
                    return std::nullopt;
                }
            }
        }

        std::ranges::sort(all_edits, [](const TextEdit& a, const TextEdit& b) {
            if (a.file != b.file) {
                return a.file < b.file;
            }
            if (a.has_byte_range() && b.has_byte_range()) {
                if (*a.byte_offset != *b.byte_offset) {
                    return *a.byte_offset < *b.byte_offset;
                }
                return *a.byte_length < *b.byte_length;
            }
            if (a.start_line != b.start_line) {
                return a.start_line < b.start_line;
            }
            return a.start_col < b.start_col;
        });

        std::vector<TextEdit> merged;
        merged.reserve(all_edits.size());

        for (const auto& edit : all_edits) {
            for (const auto& existing : merged) {
                if (edit_ranges_conflict(existing, edit)) {
                    return std::nullopt;
                }
            }

            const bool duplicate = std::ranges::any_of(
                merged,
                [&edit](const TextEdit& existing) {
                    return existing.file == edit.file &&
                        existing.has_byte_range() == edit.has_byte_range() &&
                        (!existing.has_byte_range() ||
                         (*existing.byte_offset == *edit.byte_offset &&
                          *existing.byte_length == *edit.byte_length)) &&
                        existing.start_line == edit.start_line &&
                        existing.start_col == edit.start_col &&
                        existing.end_line == edit.end_line &&
                        existing.end_col == edit.end_col &&
                        existing.new_text == edit.new_text;
                }
            );
            if (!duplicate) {
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

        for (const auto& [module, sug_list] : by_module) {
            desc << "**Module: " << module << "** (" << sug_list.size() << " candidates)\n";

            for (const auto* sug : sug_list) {
                desc << "  - `" << sug->target_file.path.filename().string() << "`";
                if (sug->confidence > 0) {
                    desc << " (confidence: " << static_cast<int>(sug->confidence * 100) << "%)";
                }
                desc << "\n";

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
        const auto impact = merge_impacts(suggestions);
        const auto confidence = conservative_confidence(suggestions);
        const auto edits = merge_edits(suggestions);
        if (!impact || !confidence || !edits) {
            return std::nullopt;
        }
        consolidated.impact = *impact;
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
        consolidated.confidence = *confidence;
        consolidated.estimated_savings_evidence = EvidenceKind::Unavailable;
        consolidated.caveats.emplace_back(
            "Savings remain unavailable until a post-edit trace is captured"
        );

        consolidated.edits = *edits;

        return consolidated;
    }

}  // namespace bha::suggestions
