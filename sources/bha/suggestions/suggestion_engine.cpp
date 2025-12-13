//
// Created by gregorian on 20/10/2025.
//

#include "bha/suggestions/suggestion_engine.h"
#include "bha/suggestions/forward_decl_suggester.h"
#include "bha/suggestions/pimpl_suggester.h"
#include "bha/analysis/pch_analyzer.h"
#include "bha/utils/hash_utils.h"
#include <algorithm>
#include <ranges>
#include <string>

namespace bha::suggestions {

    SuggestionEngine::SuggestionEngine()
        : header_splitter_(nullptr) {}

    core::Result<std::vector<core::Suggestion>> SuggestionEngine::generate_all_suggestions(
        const core::BuildTrace& trace,
        const Options& options
    ) {
        std::vector<core::Suggestion> all_suggestions;

        if (options.enable_forward_declarations) {
            if (auto fwd_result = suggest_forward_declarations(trace); fwd_result.is_success()) {
                all_suggestions.insert(all_suggestions.end(),
                                     fwd_result.value().begin(),
                                     fwd_result.value().end());
            }
        }

        if (options.enable_header_splits) {
            if (auto split_result = suggest_header_splits(trace.dependency_graph, options); split_result.is_success()) {
                all_suggestions.insert(all_suggestions.end(),
                                     split_result.value().begin(),
                                     split_result.value().end());
            }
        }

        if (options.enable_pch_suggestions) {
            if (auto pch_result = suggest_pch_optimization(trace, trace.dependency_graph); pch_result.is_success()) {
                all_suggestions.insert(all_suggestions.end(),
                                     pch_result.value().begin(),
                                     pch_result.value().end());
            }
        }

        if (options.enable_pimpl) {
            if (auto pimpl_result = suggest_pimpl_patterns(trace); pimpl_result.is_success()) {
                all_suggestions.insert(all_suggestions.end(),
                                     pimpl_result.value().begin(),
                                     pimpl_result.value().end());
            }
        }

        std::ranges::sort(all_suggestions,
                          [](const core::Suggestion& a, const core::Suggestion& b) {
                              const double score_a = a.confidence * a.estimated_time_savings_ms;
                              const double score_b = b.confidence * b.estimated_time_savings_ms;
                              return score_a > score_b;
                          });

        std::vector<core::Suggestion> filtered;
        for (const auto& suggestion : all_suggestions) {
            if (should_include_suggestion(suggestion, options.min_confidence, options.min_time_savings_ms)) {
                filtered.push_back(suggestion);
                if (static_cast<int>(filtered.size()) >= options.max_suggestions) {
                    break;
                }
            }
        }

        return core::Result<std::vector<core::Suggestion>>::success(std::move(filtered));
    }

    core::Result<std::vector<core::Suggestion>> SuggestionEngine::suggest_forward_declarations(
        const core::BuildTrace& trace
    ) {
        std::vector<core::Suggestion> suggestions;

        for (const auto& unit : trace.compilation_units) {
            if (auto result = ForwardDeclSuggester::suggest_forward_declarations(unit.file_path, trace); result.is_success()) {
                suggestions.insert(suggestions.end(),
                                 result.value().begin(),
                                 result.value().end());
            }
        }

        return core::Result<std::vector<core::Suggestion>>::success(std::move(suggestions));
    }

    core::Result<std::vector<core::Suggestion>> SuggestionEngine::suggest_header_splits(
        const core::DependencyGraph& graph,
        const Options& options
    ) {
        std::vector<core::Suggestion> suggestions;

        if (!header_splitter_) {
            header_splitter_ = std::make_unique<HeaderSplitter>(graph);
        }

        for (const auto nodes = graph.get_all_nodes(); const auto& node : nodes) {
            auto dependents = graph.get_reverse_dependencies(node);

            if (static_cast<int>(dependents.size()) < options.header_split_fanout_threshold) {
                continue;
            }

            auto split_result = HeaderSplitter::suggest_split(
                node,
                dependents,
                options.header_split_min_symbols
            );

            if (split_result.is_success()) {
                auto suggestion = header_split_to_suggestion(split_result.value());
                suggestions.push_back(suggestion);
            }
        }

        return core::Result<std::vector<core::Suggestion>>::success(std::move(suggestions));
    }

    core::Result<std::vector<core::Suggestion>> SuggestionEngine::suggest_pch_optimization(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph
    ) {
        std::vector<core::Suggestion> suggestions;

        auto candidates_result = analysis::PCHAnalyzer::identify_pch_candidates(trace, graph, 5, 0.5);
        if (!candidates_result.is_success()) {
            return core::Result<std::vector<core::Suggestion>>::success(std::move(suggestions));
        }

        for (const auto& candidate : candidates_result.value()) {
            core::Suggestion suggestion;
            suggestion.id = utils::generate_uuid();
            suggestion.type = core::SuggestionType::PCH_ADDITION;
            suggestion.priority = core::Priority::HIGH;
            suggestion.confidence = 0.8;
            suggestion.file_path = candidate.header;
            suggestion.title = "Add " + candidate.header + " to PCH";
            suggestion.description = "This header is included by " +
                                    std::to_string(candidate.inclusion_count) +
                                    " files and could benefit from precompilation.";
            suggestion.estimated_time_savings_ms = candidate.potential_savings_ms;
            suggestion.is_safe = true;

            suggestions.push_back(suggestion);
        }

        return core::Result<std::vector<core::Suggestion>>::success(std::move(suggestions));
    }

    core::Result<std::vector<core::Suggestion>> SuggestionEngine::suggest_pimpl_patterns(
        const core::BuildTrace& trace
    ) {
        std::vector<core::Suggestion> suggestions;

        for (const auto& unit : trace.compilation_units) {
            if (auto result = PIMPLSuggester::suggest_pimpl_patterns(unit.file_path); result.is_success()) {
                suggestions.insert(suggestions.end(),
                                 result.value().begin(),
                                 result.value().end());
            }
        }

        return core::Result<std::vector<core::Suggestion>>::success(std::move(suggestions));
    }

    core::Result<core::Suggestion> SuggestionEngine::filter_and_rank(
        std::vector<core::Suggestion>& suggestions,
        const double min_confidence,
        const double min_savings,
        const int max_count
    )
    {
        std::ranges::sort(suggestions,
                          [](const core::Suggestion& a, const core::Suggestion& b) {
                              return a.confidence * a.estimated_time_savings_ms >
                                     b.confidence * b.estimated_time_savings_ms;
                          });

        std::vector<core::Suggestion> filtered;
        for (const auto& suggestion : suggestions) {
            if (should_include_suggestion(suggestion, min_confidence, min_savings)) {
                filtered.push_back(suggestion);
                if (static_cast<int>(filtered.size()) >= max_count) {
                    break;
                }
            }
        }

        suggestions = std::move(filtered);

        if (!suggestions.empty()) {
            return core::Result<core::Suggestion>::success(suggestions.front());
        }

        return core::Result<core::Suggestion>::failure(core::Error{
            core::ErrorCode::ANALYSIS_ERROR,
            "No suggestion passed filters"
        });
    }


    bool SuggestionEngine::should_include_suggestion(
        const core::Suggestion& suggestion,
        const double min_confidence,
        const double min_savings
    )
    {
        return suggestion.confidence >= min_confidence &&
               suggestion.estimated_time_savings_ms >= min_savings;
    }

    core::Suggestion SuggestionEngine::header_split_to_suggestion(
        const HeaderSplitSuggestion& split_suggestion
    ) {
        core::Suggestion suggestion;
        suggestion.id = utils::generate_uuid();
        suggestion.type = core::SuggestionType::HEADER_SPLIT;
        suggestion.priority = core::Priority::MEDIUM;
        suggestion.confidence = split_suggestion.confidence;
        suggestion.file_path = split_suggestion.original_file;
        suggestion.title = "Split header: " + split_suggestion.original_file;

        std::string parts_desc;
        for (size_t i = 0; i < split_suggestion.suggested_splits.size(); ++i) {
            if (i > 0) parts_desc += ", ";
            parts_desc += split_suggestion.suggested_splits[i].first;
        }

        suggestion.description = split_suggestion.rationale +
                                "\n\nSuggested splits:\n" + parts_desc;
        suggestion.estimated_time_savings_ms = split_suggestion.estimated_benefit_ms;
        suggestion.is_safe = true;

        for (const auto& filename : split_suggestion.suggested_splits | std::views::keys) {
            suggestion.related_files.push_back(filename);
        }

        return suggestion;
    }
}
