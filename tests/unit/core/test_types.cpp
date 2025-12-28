//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/types.hpp"

#include <gtest/gtest.h>

namespace bha
{
    TEST(SourceLocationTest, DefaultConstruction) {
        const SourceLocation loc;

        EXPECT_TRUE(loc.file.empty());
        EXPECT_EQ(loc.line, 0u);
        EXPECT_EQ(loc.column, 0u);
        EXPECT_FALSE(loc.has_location());
    }

    TEST(SourceLocationTest, HasLocation) {
        SourceLocation loc;
        loc.file = "/path/to/file.cpp";
        loc.line = 42;
        loc.column = 10;

        EXPECT_TRUE(loc.has_location());
    }

    TEST(TimeBreakdownTest, Total) {
        TimeBreakdown breakdown;
        breakdown.preprocessing = std::chrono::milliseconds(100);
        breakdown.parsing = std::chrono::milliseconds(200);
        breakdown.semantic_analysis = std::chrono::milliseconds(300);
        breakdown.template_instantiation = std::chrono::milliseconds(150);
        breakdown.code_generation = std::chrono::milliseconds(50);
        breakdown.optimization = std::chrono::milliseconds(200);

        const auto total = breakdown.total();
        EXPECT_TRUE(total == std::chrono::milliseconds(1000));
    }

    TEST(CompilerTypeTest, ToString) {
        EXPECT_STREQ(to_string(CompilerType::Unknown), "Unknown");
        EXPECT_STREQ(to_string(CompilerType::Clang), "Clang");
        EXPECT_STREQ(to_string(CompilerType::GCC), "GCC");
        EXPECT_STREQ(to_string(CompilerType::MSVC), "MSVC");
        EXPECT_STREQ(to_string(CompilerType::IntelClassic), "Intel ICC");
        EXPECT_STREQ(to_string(CompilerType::IntelOneAPI), "Intel ICX");
        EXPECT_STREQ(to_string(CompilerType::NVCC), "NVCC");
        EXPECT_STREQ(to_string(CompilerType::ArmClang), "ARM Clang");
        EXPECT_STREQ(to_string(CompilerType::AppleClang), "Apple Clang");
    }

    TEST(BuildSystemTypeTest, ToString) {
        EXPECT_STREQ(to_string(BuildSystemType::Unknown), "Unknown");
        EXPECT_STREQ(to_string(BuildSystemType::CMake), "CMake");
        EXPECT_STREQ(to_string(BuildSystemType::Ninja), "Ninja");
        EXPECT_STREQ(to_string(BuildSystemType::Make), "Make");
        EXPECT_STREQ(to_string(BuildSystemType::MSBuild), "MSBuild");
        EXPECT_STREQ(to_string(BuildSystemType::Bazel), "Bazel");
        EXPECT_STREQ(to_string(BuildSystemType::Buck2), "Buck2");
        EXPECT_STREQ(to_string(BuildSystemType::Meson), "Meson");
        EXPECT_STREQ(to_string(BuildSystemType::SCons), "SCons");
        EXPECT_STREQ(to_string(BuildSystemType::XCode), "XCode");
    }

    TEST(SuggestionTypeTest, ToString) {
        EXPECT_STREQ(to_string(SuggestionType::ForwardDeclaration), "Forward Declaration");
        EXPECT_STREQ(to_string(SuggestionType::HeaderSplit), "Header Split");
        EXPECT_STREQ(to_string(SuggestionType::PCHOptimization), "PCH Optimization");
        EXPECT_STREQ(to_string(SuggestionType::PIMPLPattern), "PIMPL Pattern");
        EXPECT_STREQ(to_string(SuggestionType::IncludeRemoval), "Include Removal");
        EXPECT_STREQ(to_string(SuggestionType::MoveToCpp), "Move to CPP");
        EXPECT_STREQ(to_string(SuggestionType::ExplicitTemplate), "Explicit Template");
        EXPECT_STREQ(to_string(SuggestionType::UnityBuild), "Unity Build");
        EXPECT_STREQ(to_string(SuggestionType::ModuleMigration), "Module Migration");
    }

    TEST(PriorityTest, ToString) {
        EXPECT_STREQ(to_string(Priority::Critical), "Critical");
        EXPECT_STREQ(to_string(Priority::High), "High");
        EXPECT_STREQ(to_string(Priority::Medium), "Medium");
        EXPECT_STREQ(to_string(Priority::Low), "Low");
    }

    TEST(FileActionTest, ToString) {
        EXPECT_STREQ(to_string(FileAction::Modify), "MODIFY");
        EXPECT_STREQ(to_string(FileAction::AddInclude), "ADD_INCLUDE");
        EXPECT_STREQ(to_string(FileAction::Remove), "REMOVE");
        EXPECT_STREQ(to_string(FileAction::Create), "CREATE");
    }

    TEST(FileTargetTest, HasLineRange) {
        FileTarget target;
        target.path = "/path/to/file.h";

        EXPECT_FALSE(target.has_line_range());

        target.line_start = 10;
        target.line_end = 20;

        EXPECT_TRUE(target.has_line_range());
    }

    TEST(BuildTraceTest, FileCount) {
        BuildTrace trace;
        EXPECT_EQ(trace.file_count(), 0u);

        trace.units.push_back(CompilationUnit{});
        trace.units.push_back(CompilationUnit{});

        EXPECT_EQ(trace.file_count(), 2u);
    }

    TEST(BuildTraceTest, DefaultValues) {
        const BuildTrace trace;

        EXPECT_TRUE(trace.id.empty());
        EXPECT_TRUE(trace.total_time == Duration::zero());
        EXPECT_EQ(trace.compiler, CompilerType::Unknown);
        EXPECT_TRUE(trace.compiler_version.empty());
        EXPECT_EQ(trace.build_system, BuildSystemType::Unknown);
        EXPECT_FALSE(trace.git_info.has_value());
    }

    TEST(SuggestionTest, DefaultValues) {
        const Suggestion suggestion;

        EXPECT_TRUE(suggestion.id.empty());
        EXPECT_EQ(suggestion.type, SuggestionType::ForwardDeclaration);
        EXPECT_EQ(suggestion.priority, Priority::Medium);
        EXPECT_DOUBLE_EQ(suggestion.confidence, 0.0);
        EXPECT_FALSE(suggestion.is_safe);
    }

    TEST(AnalysisOptionsTest, DefaultValues) {
        constexpr AnalysisOptions options;

        EXPECT_EQ(options.max_threads, 0u);
        EXPECT_TRUE(options.min_duration_threshold == std::chrono::milliseconds(10));
        EXPECT_TRUE(options.analyze_templates);
        EXPECT_TRUE(options.analyze_includes);
        EXPECT_TRUE(options.analyze_symbols);
        EXPECT_FALSE(options.verbose);
    }

    TEST(SuggesterOptionsTest, DefaultValues) {
        const SuggesterOptions options;

        EXPECT_EQ(options.max_suggestions, 100u);
        EXPECT_EQ(options.min_priority, Priority::Low);
        EXPECT_DOUBLE_EQ(options.min_confidence, 0.5);
        EXPECT_FALSE(options.include_unsafe);
        EXPECT_TRUE(options.enabled_types.empty());
    }

}  // namespace bha