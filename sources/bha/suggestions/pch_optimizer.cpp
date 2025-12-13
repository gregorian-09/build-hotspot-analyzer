//
// Created by gregorian on 20/10/2025.
//

#include "bha/suggestions/pch_optimizer.h"
#include "bha/analysis/pch_analyzer.h"
#include "bha/utils/string_utils.h"
#include <algorithm>
#include <sstream>
#include "bha/utils/hash_utils.h"

namespace bha::suggestions {

    core::Result<PCHOptimizationResult> PCHOptimizer::optimize_pch(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const std::vector<std::string>& current_pch_headers
    ) {
        PCHOptimizationResult result;

        if (auto add_result = suggest_headers_to_add(trace, graph, 10, 0.5); add_result.is_success()) {
            result.headers_to_add = add_result.value();
        }

        if (auto remove_result = suggest_headers_to_remove(trace, graph, current_pch_headers); remove_result.is_success()) {
            result.headers_to_remove = remove_result.value();
        }

        std::vector<std::string> optimized_headers = current_pch_headers;

        for (const auto& header : result.headers_to_remove) {
            if (auto it = std::ranges::find(optimized_headers, header); it != optimized_headers.end()) {
                optimized_headers.erase(it);
            }
        }

        for (const auto& header : result.headers_to_add) {
            if (std::ranges::find(optimized_headers, header) ==
                optimized_headers.end()) {
                optimized_headers.push_back(header);
            }
        }

        if (auto pch_content_result = generate_pch_header_file(optimized_headers); pch_content_result.is_success()) {
            result.suggested_pch_content = pch_content_result.value();
        }

        result.estimated_time_savings_ms = estimate_pch_optimization_benefit(
            result.headers_to_add,
            result.headers_to_remove,
            trace,
            graph
        );

        result.confidence = 0.75;

        return core::Result<PCHOptimizationResult>::success(std::move(result));
    }

    core::Result<std::vector<std::string>> PCHOptimizer::suggest_headers_to_add(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const int top_n,
        const double min_inclusion_ratio
    ) {
        auto candidates_result = analysis::PCHAnalyzer::identify_pch_candidates(
            trace,
            graph,
            top_n,
            min_inclusion_ratio
        );

        if (!candidates_result.is_success()) {
            return core::Result<std::vector<std::string>>::failure(candidates_result.error());
        }

        std::vector<std::string> headers;
        for (const auto& candidate : candidates_result.value()) {
            headers.push_back(candidate.header);
        }

        return core::Result<std::vector<std::string>>::success(std::move(headers));
    }

    core::Result<std::vector<std::string>> PCHOptimizer::suggest_headers_to_remove(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const std::vector<std::string>& current_pch_headers
    ) {
        std::vector<std::string> removals;
        const int total_files = static_cast<int>(trace.compilation_units.size());

        for (const auto& header : current_pch_headers) {
            auto dependents = graph.get_reverse_dependencies(header);
            const double inclusion_ratio = static_cast<double>(dependents.size()) / total_files;

            double compile_time = 0.0;
            for (const auto& unit : trace.compilation_units) {
                if (unit.file_path == header) {
                    compile_time = unit.preprocessing_time_ms;
                    break;
                }
            }

            if (!should_remain_in_pch(inclusion_ratio, compile_time)) {
                removals.push_back(header);
            }
        }

        return core::Result<std::vector<std::string>>::success(std::move(removals));
    }

    core::Result<std::vector<core::Suggestion>> PCHOptimizer::generate_pch_suggestions(
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph,
        const std::vector<std::string>& current_pch_headers
    ) {
        std::vector<core::Suggestion> suggestions;

        auto optimize_result = optimize_pch(trace, graph, current_pch_headers);
        if (!optimize_result.is_success()) {
            return core::Result<std::vector<core::Suggestion>>::failure(optimize_result.error());
        }

        const auto& result = optimize_result.value();

        for (const auto& header : result.headers_to_add) {
            core::Suggestion suggestion;
            suggestion.id = utils::generate_uuid();
            suggestion.type = core::SuggestionType::PCH_ADDITION;
            suggestion.priority = core::Priority::MEDIUM;
            suggestion.confidence = 0.75;
            suggestion.file_path = header;
            suggestion.title = "Add to PCH: " + header;
            suggestion.description = "This header is frequently included and would benefit from precompilation.";
            suggestion.estimated_time_savings_ms = 50.0;
            suggestion.is_safe = true;
            suggestion.affected_files.push_back(header);

            suggestions.push_back(suggestion);
        }

        for (const auto& header : result.headers_to_remove) {
            core::Suggestion suggestion;
            suggestion.id = utils::generate_uuid();
            suggestion.type = core::SuggestionType::PCH_REMOVAL;
            suggestion.priority = core::Priority::LOW;
            suggestion.confidence = 0.8;
            suggestion.file_path = header;
            suggestion.title = "Remove from PCH: " + header;
            suggestion.description = "This header is rarely used and could be removed to reduce PCH build time.";
            suggestion.estimated_time_savings_ms = 25.0;
            suggestion.is_safe = true;
            suggestion.affected_files.push_back(header);

            suggestions.push_back(suggestion);
        }

        return core::Result<std::vector<core::Suggestion>>::success(std::move(suggestions));
    }

    core::Result<std::string> PCHOptimizer::generate_pch_header_file(
        const std::vector<std::string>& headers
    ) {
        std::ostringstream ss;

        ss << "#ifndef BHA_GENERATED_PCH_H\n";
        ss << "#define BHA_GENERATED_PCH_H\n\n";
        ss << "// Auto-generated precompiled header\n";
        ss << "// Generated by Build Hotspot Analyzer\n\n";

        for (const auto& header : headers) {
            ss << "#include \"" << header << "\"\n";
        }

        ss << "\n#endif // BHA_GENERATED_PCH_H\n";

        return core::Result<std::string>::success(ss.str());
    }

    double PCHOptimizer::estimate_pch_optimization_benefit(
        const std::vector<std::string>& headers_to_add,
        const std::vector<std::string>& headers_to_remove,
        const core::BuildTrace& trace,
        const core::DependencyGraph& graph
    ) {
        double total_benefit = 0.0;

        for (const auto& header : headers_to_add) {
            for (const auto& unit : trace.compilation_units) {
                if (unit.file_path == header) {
                    auto dependents = graph.get_reverse_dependencies(header);
                    total_benefit += unit.preprocessing_time_ms * static_cast<double>(dependents.size()) * 0.8;
                    break;
                }
            }
        }

        for (const auto& header : headers_to_remove) {
            for (const auto& unit : trace.compilation_units) {
                if (unit.file_path == header) {
                    total_benefit -= unit.preprocessing_time_ms * 0.1;
                    break;
                }
            }
        }

        return total_benefit;
    }

    double PCHOptimizer::calculate_header_importance(
        const int inclusion_count,
        const double compile_time_ms,
        const int total_files
    ) {
        const double usage_score = static_cast<double>(inclusion_count) / total_files;
        const double time_score = compile_time_ms / 1000.0;

        return usage_score * time_score;
    }

    bool PCHOptimizer::is_system_header(const std::string& header) {
        return utils::starts_with(header, "/usr/") ||
               utils::starts_with(header, "/opt/") ||
               utils::starts_with(header, "C:\\Program Files") ||
               utils::contains(header, "/include/c++/");
    }

    bool PCHOptimizer::should_remain_in_pch(
        const double inclusion_ratio,
        const double compile_time_ms
    ) {
        if (inclusion_ratio < 0.15) {
            return false;
        }

        if (compile_time_ms < 10.0) {
            return inclusion_ratio > 0.5;
        }

        return true;
    }

} // namespace bha::suggestions