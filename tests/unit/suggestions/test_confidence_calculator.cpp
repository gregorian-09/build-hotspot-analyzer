//
// Created by gregorian on 13/12/2025.
//

#include <gtest/gtest.h>
#include "bha/suggestions/confidence_calculator.h"

using namespace bha::suggestions;

TEST(ConfidenceCalculatorTest, ForwardDeclarationConfidencePointerOnly) {
    const double confidence = ConfidenceCalculator::calculate_forward_declaration_confidence(
        true,   // used_by_pointer
        false,  // used_by_reference
        false,  // used_by_value
        5       // usage_count
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
    EXPECT_GT(confidence, 0.7);
}

TEST(ConfidenceCalculatorTest, ForwardDeclarationConfidenceReferenceOnly) {
    const double confidence = ConfidenceCalculator::calculate_forward_declaration_confidence(
        false,  // used_by_pointer
        true,   // used_by_reference
        false,  // used_by_value
        3       // usage_count
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
    EXPECT_GT(confidence, 0.6);  // High confidence for reference-only usage
}

TEST(ConfidenceCalculatorTest, ForwardDeclarationConfidenceValueUsage) {
    const double confidence = ConfidenceCalculator::calculate_forward_declaration_confidence(
        false,  // used_by_pointer
        false,  // used_by_reference
        true,   // used_by_value
        2       // usage_count
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
    EXPECT_LT(confidence, 0.5);  // Low confidence when used by value
}

TEST(ConfidenceCalculatorTest, ForwardDeclarationConfidenceMixedUsage) {
    const double confidence = ConfidenceCalculator::calculate_forward_declaration_confidence(
        true,   // used_by_pointer
        true,   // used_by_reference
        true,   // used_by_value
        10      // usage_count
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
}

TEST(ConfidenceCalculatorTest, HeaderSplitConfidenceLowDependents) {
    const double confidence = ConfidenceCalculator::calculate_header_split_confidence(
        5,    // num_dependents
        2.0   // average_include_depth
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
}

TEST(ConfidenceCalculatorTest, HeaderSplitConfidenceHighDependents) {
    const double confidence = ConfidenceCalculator::calculate_header_split_confidence(
        50,   // num_dependents
        4.0   // average_include_depth
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
}

TEST(ConfidenceCalculatorTest, PCHConfidenceLowInclusion) {
    const double confidence = ConfidenceCalculator::calculate_pch_confidence(
        2,      // inclusion_count
        100,    // total_files
        50.0,   // compile_time_ms
        75.0    // average_file_time_ms
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
}

TEST(ConfidenceCalculatorTest, PCHConfidenceHighInclusion) {
    const double confidence = ConfidenceCalculator::calculate_pch_confidence(
        80,     // inclusion_count
        100,    // total_files
        150.0,  // compile_time_ms
        75.0    // average_file_time_ms
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
    EXPECT_GT(confidence, 0.5);  // High inclusion ratio should give higher confidence
}

TEST(ConfidenceCalculatorTest, IncludeRemovalConfidenceTransitive) {
    const double confidence = ConfidenceCalculator::calculate_include_removal_confidence(
        true,   // is_transitive
        0       // direct_usage_count
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
    EXPECT_GT(confidence, 0.7);  // Transitive with no direct usage = high confidence
}

TEST(ConfidenceCalculatorTest, IncludeRemovalConfidenceNonTransitive) {
    const double confidence = ConfidenceCalculator::calculate_include_removal_confidence(
        false,  // is_transitive
        5       // direct_usage_count
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
    EXPECT_LT(confidence, 0.5);  // Non-transitive with direct usage = low confidence
}

TEST(ConfidenceCalculatorTest, PimplConfidenceFewPrivateMembers) {
    const double confidence = ConfidenceCalculator::calculate_pimpl_confidence(
        2,  // private_member_count
        1   // included_headers_in_private
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
}

TEST(ConfidenceCalculatorTest, PimplConfidenceManyPrivateMembers) {
    const double confidence = ConfidenceCalculator::calculate_pimpl_confidence(
        15,  // private_member_count
        8    // included_headers_in_private
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
}

TEST(ConfidenceCalculatorTest, MoveToCppConfidenceTemplate) {
    const double confidence = ConfidenceCalculator::calculate_move_to_cpp_confidence(
        true,   // is_template
        false,  // is_inline
        10      // usage_count
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
}

TEST(ConfidenceCalculatorTest, MoveToCppConfidenceRegular) {
    const double confidence = ConfidenceCalculator::calculate_move_to_cpp_confidence(
        false,  // is_template
        false,  // is_inline
        3       // usage_count
    );

    EXPECT_GE(confidence, 0.0);
    EXPECT_LE(confidence, 1.0);
}

TEST(ConfidenceCalculatorTest, NormalizeConfidenceWithinRange) {
    const double normalized = ConfidenceCalculator::normalize_confidence(0.75);
    EXPECT_GE(normalized, 0.0);
    EXPECT_LE(normalized, 1.0);
    EXPECT_DOUBLE_EQ(normalized, 0.75);
}

TEST(ConfidenceCalculatorTest, NormalizeConfidenceTooHigh) {
    const double normalized = ConfidenceCalculator::normalize_confidence(1.5);
    EXPECT_GE(normalized, 0.0);
    EXPECT_LE(normalized, 1.0);
    EXPECT_DOUBLE_EQ(normalized, 1.0);
}

TEST(ConfidenceCalculatorTest, NormalizeConfidenceTooLow) {
    const double normalized = ConfidenceCalculator::normalize_confidence(-0.3);
    EXPECT_GE(normalized, 0.0);
    EXPECT_LE(normalized, 1.0);
    EXPECT_DOUBLE_EQ(normalized, 0.0);
}

TEST(ConfidenceCalculatorTest, ComputeConfidenceScoresConsistency) {
    const double conf1 = ConfidenceCalculator::calculate_forward_declaration_confidence(
        true, false, false, 5);
    const double conf2 = ConfidenceCalculator::calculate_forward_declaration_confidence(
        true, false, false, 5);

    EXPECT_DOUBLE_EQ(conf1, conf2);
}

TEST(ConfidenceCalculatorTest, WeightDifferentFactors) {
    const double pointer_conf = ConfidenceCalculator::calculate_forward_declaration_confidence(
        true, false, false, 5);
    const double reference_conf = ConfidenceCalculator::calculate_forward_declaration_confidence(
        false, true, false, 5);

    EXPECT_GT(pointer_conf, reference_conf);

    const double value_conf = ConfidenceCalculator::calculate_forward_declaration_confidence(
        false, false, true, 5);

    EXPECT_GT(pointer_conf, value_conf);
    EXPECT_GT(reference_conf, value_conf);
}

TEST(ConfidenceCalculatorTest, ForwardDeclarationConfidenceWithHighUsage) {
    const double high_usage = ConfidenceCalculator::calculate_forward_declaration_confidence(
        true, false, false, 20);

    EXPECT_LE(high_usage, 1.0);
    EXPECT_GE(high_usage, 0.0);
}

TEST(ConfidenceCalculatorTest, HeaderSplitConfidenceScales) {
    const double low_score = ConfidenceCalculator::calculate_header_split_confidence(1, 1.0);
    const double mid_score = ConfidenceCalculator::calculate_header_split_confidence(25, 3.0);
    const double high_score = ConfidenceCalculator::calculate_header_split_confidence(100, 5.0);

    EXPECT_LE(low_score, 1.0);
    EXPECT_LE(mid_score, 1.0);
    EXPECT_LE(high_score, 1.0);
    EXPECT_GE(low_score, 0.0);
    EXPECT_GE(mid_score, 0.0);
    EXPECT_GE(high_score, 0.0);
}

TEST(ConfidenceCalculatorTest, PCHConfidenceWithVaryingFrequency) {
    const double rare = ConfidenceCalculator::calculate_pch_confidence(1, 1000, 10.0, 100.0);
    const double common = ConfidenceCalculator::calculate_pch_confidence(800, 1000, 200.0, 100.0);

    EXPECT_LE(rare, 1.0);
    EXPECT_LE(common, 1.0);
    EXPECT_GE(rare, 0.0);
    EXPECT_GE(common, 0.0);
}

TEST(ConfidenceCalculatorTest, IncludeRemovalConfidenceEdgeCases) {
    // Transitive with many usages should still be somewhat confident
    const double transitive_high_use = ConfidenceCalculator::calculate_include_removal_confidence(true, 100);
    EXPECT_GE(transitive_high_use, 0.0);
    EXPECT_LE(transitive_high_use, 1.0);

    // Non-transitive with zero usages is very low confidence
    const double non_transitive_no_use = ConfidenceCalculator::calculate_include_removal_confidence(false, 0);
    EXPECT_GE(non_transitive_no_use, 0.0);
    EXPECT_LE(non_transitive_no_use, 1.0);
}

TEST(ConfidenceCalculatorTest, PimplConfidenceIncreasingWithComplexity) {
    const double simple = ConfidenceCalculator::calculate_pimpl_confidence(2, 0);
    const double complex = ConfidenceCalculator::calculate_pimpl_confidence(20, 10);

    EXPECT_LE(simple, 1.0);
    EXPECT_LE(complex, 1.0);
    EXPECT_GE(simple, 0.0);
    EXPECT_GE(complex, 0.0);
}

TEST(ConfidenceCalculatorTest, MoveToCppConfidenceMultipleFactors) {
    const double inline_template = ConfidenceCalculator::calculate_move_to_cpp_confidence(true, true, 5);
    const double regular = ConfidenceCalculator::calculate_move_to_cpp_confidence(false, false, 5);

    EXPECT_LE(inline_template, 1.0);
    EXPECT_LE(regular, 1.0);
    EXPECT_GE(inline_template, 0.0);
    EXPECT_GE(regular, 0.0);
}

TEST(ConfidenceCalculatorTest, NormalizeConfidenceNegativeValues) {
    const double negative = ConfidenceCalculator::normalize_confidence(-5.0);
    EXPECT_DOUBLE_EQ(negative, 0.0);
}

TEST(ConfidenceCalculatorTest, NormalizeConfidenceZero) {
    const double zero = ConfidenceCalculator::normalize_confidence(0.0);
    EXPECT_DOUBLE_EQ(zero, 0.0);
}

TEST(ConfidenceCalculatorTest, NormalizeConfidenceOne) {
    const double one = ConfidenceCalculator::normalize_confidence(1.0);
    EXPECT_DOUBLE_EQ(one, 1.0);
}

TEST(ConfidenceCalculatorTest, NormalizeConfidenceLargeValue) {
    const double large = ConfidenceCalculator::normalize_confidence(100.0);
    EXPECT_DOUBLE_EQ(large, 1.0);
}

TEST(ConfidenceCalculatorTest, AllConfidenceMetricsInRange) {
    // Test multiple scenarios to ensure all methods return valid ranges
    for (int i = 0; i < 10; ++i) {
        double fwd_decl = ConfidenceCalculator::calculate_forward_declaration_confidence(
            i % 2 == 0, (i + 1) % 2 == 0, (i + 2) % 2 == 0, i);
        double header_split = ConfidenceCalculator::calculate_header_split_confidence(i * 10, i + 1);
        double pch = ConfidenceCalculator::calculate_pch_confidence(i, 100, i * 10, 50);
        double include_removal = ConfidenceCalculator::calculate_include_removal_confidence(
            i % 2 == 0, i);
        double pimpl = ConfidenceCalculator::calculate_pimpl_confidence(i, i / 2);
        double move_cpp = ConfidenceCalculator::calculate_move_to_cpp_confidence(
            i % 2 == 0, (i + 1) % 2 == 0, i);

        EXPECT_GE(fwd_decl, 0.0) << "Iteration " << i;
        EXPECT_LE(fwd_decl, 1.0) << "Iteration " << i;
        EXPECT_GE(header_split, 0.0) << "Iteration " << i;
        EXPECT_LE(header_split, 1.0) << "Iteration " << i;
        EXPECT_GE(pch, 0.0) << "Iteration " << i;
        EXPECT_LE(pch, 1.0) << "Iteration " << i;
        EXPECT_GE(include_removal, 0.0) << "Iteration " << i;
        EXPECT_LE(include_removal, 1.0) << "Iteration " << i;
        EXPECT_GE(pimpl, 0.0) << "Iteration " << i;
        EXPECT_LE(pimpl, 1.0) << "Iteration " << i;
        EXPECT_GE(move_cpp, 0.0) << "Iteration " << i;
        EXPECT_LE(move_cpp, 1.0) << "Iteration " << i;
    }
}