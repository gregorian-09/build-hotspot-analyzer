//
// Created by gregorian on 20/10/2025.
//

#include "bha/suggestions/confidence_calculator.h"
#include <algorithm>
#include <cmath>

namespace bha::suggestions {

    double ConfidenceCalculator::calculate_forward_declaration_confidence(
        const bool used_by_pointer,
        const bool used_by_reference,
        const bool used_by_value,
        const int usage_count
    ) {
        double score = 0.0;
        int usage_methods = 0;

        if (used_by_pointer) {
            score += POINTER_WEIGHT;
            usage_methods++;
        }

        if (used_by_reference) {
            score += REFERENCE_WEIGHT;
            usage_methods++;
        }

        if (used_by_value) {
            score += VALUE_WEIGHT;
            usage_methods++;
        }

        if (usage_methods == 0) {
            return 0.0;
        }

        score /= usage_methods;

        if (usage_count > 5) {
            score *= HIGH_USAGE_MULTIPLIER;
        }

        return normalize_confidence(score);
    }

    double ConfidenceCalculator::calculate_header_split_confidence(
        const int num_dependents,
        const double average_include_depth
    ) {
        if (num_dependents < 10) {
            return 0.2;
        }

        const double fanout_score = std::min(static_cast<double>(num_dependents) / 50.0, 1.0);
        const double depth_penalty = std::min(average_include_depth / 20.0, 0.3);

        return normalize_confidence(fanout_score - depth_penalty);
    }

    double ConfidenceCalculator::calculate_pch_confidence(
        const int inclusion_count,
        const int total_files,
        const double compile_time_ms,
        const double average_file_time_ms
    )
    {
        if (total_files == 0) {
            return 0.0;
        }

        const double inclusion_ratio = static_cast<double>(inclusion_count) / total_files;
        const double time_importance = std::min(compile_time_ms / average_file_time_ms, 2.0) / 2.0;

        if (inclusion_ratio < 0.3) {
            return 0.3 + time_importance * 0.3;
        }
        if (inclusion_ratio < 0.6) {
            return 0.6 + time_importance * 0.2;
        }
        return 0.8 + time_importance * 0.2;
    }

    double ConfidenceCalculator::calculate_include_removal_confidence(
        const bool is_transitive,
        const int direct_usage_count
    ) {
        if (direct_usage_count > 0) {
            return 0.3;
        }

        return is_transitive ? 0.85 : 0.95;
    }

    double ConfidenceCalculator::calculate_pimpl_confidence(
        const int private_member_count,
        const int included_headers_in_private
    ) {
        if (private_member_count < 5) {
            return 0.2;
        }

        const double member_score = std::min(static_cast<double>(private_member_count) / 20.0, 1.0);
        const double header_score = std::min(static_cast<double>(included_headers_in_private) / 10.0, 0.5);

        return normalize_confidence(member_score * 0.7 + header_score * 0.3);
    }

    double ConfidenceCalculator::calculate_move_to_cpp_confidence(
        const bool is_template,
        const bool is_inline,
        const int usage_count
    ) {
        if (is_template) {
            return 0.3;
        }

        if (is_inline) {
            return 0.4;
        }

        if (usage_count > 10) {
            return 0.7;
        }
        if (usage_count > 3) {
            return 0.6;
        }

        return 0.5;
    }

    double ConfidenceCalculator::normalize_confidence(const double raw_score) {
        return std::max(CONFIDENCE_MIN, std::min(CONFIDENCE_MAX, raw_score));
    }

} // namespace bha::suggestions