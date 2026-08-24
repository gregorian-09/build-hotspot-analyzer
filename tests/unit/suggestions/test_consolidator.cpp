//
// Created by gregorian-rayne on 01/18/26.
//

#include "bha/suggestions/consolidator.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace bha::suggestions
{
    class ConsolidatorTest : public ::testing::Test {
    protected:
        void SetUp() override {
            consolidator_ = std::make_unique<SuggestionConsolidator>();
        }

        static TextEdit make_file_edit(const std::string& path, const std::string& content) {
            TextEdit edit;
            edit.file = path;
            edit.start_line = 0;
            edit.start_col = 0;
            edit.end_line = 0;
            edit.end_col = 0;
            edit.new_text = content;
            return edit;
        }

        static Suggestion create_pch_suggestion(const std::string& header_name, const bool is_stable = true) {
            Suggestion s;
            s.type = SuggestionType::PCHOptimization;
            s.target_file.path = header_name;
            s.priority = Priority::Medium;
            s.estimated_savings = std::chrono::milliseconds(100);
            s.confidence = 0.8;

            if (header_name.find("volatile") != std::string::npos) {
                s.rationale = "This header is volatile and frequently modified.";
            } else if (is_stable) {
                s.rationale = "This header is stable.";
            }

            return s;
        }

        std::unique_ptr<SuggestionConsolidator> consolidator_;
    };

    TEST_F(ConsolidatorTest, EmptyInput) {
        const std::vector<Suggestion> empty;
        const auto result = consolidator_->consolidate(empty);
        EXPECT_TRUE(result.empty());
    }

    TEST_F(ConsolidatorTest, PreservesPCHAdvisoriesWithoutSynthesizedEdits) {
        std::vector<Suggestion> suggestions;
        auto first = create_pch_suggestion("header1.h");
        first.application_mode = SuggestionApplicationMode::Advisory;
        auto second = create_pch_suggestion("header2.h");
        second.application_mode = SuggestionApplicationMode::Advisory;
        suggestions.push_back(std::move(first));
        suggestions.push_back(std::move(second));

        const auto result = consolidator_->consolidate(suggestions);

        ASSERT_EQ(result.size(), 2u);
        EXPECT_EQ(result[0].target_file.path, "header1.h");
        EXPECT_EQ(result[1].target_file.path, "header2.h");
        EXPECT_TRUE(result[0].edits.empty());
        EXPECT_TRUE(result[1].edits.empty());
        EXPECT_EQ(result[0].application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_EQ(result[1].application_mode, SuggestionApplicationMode::Advisory);
    }

    TEST_F(ConsolidatorTest, PreservesDifferentSuggestionTypes) {
        std::vector<Suggestion> suggestions;

        Suggestion pch;
        pch.type = SuggestionType::PCHOptimization;
        pch.target_file.path = "header.h";
        suggestions.push_back(pch);

        Suggestion split;
        split.type = SuggestionType::HeaderSplit;
        split.target_file.path = "large_header.h";
        suggestions.push_back(split);

        Suggestion unity;
        unity.type = SuggestionType::UnityBuild;
        unity.target_file.path = "source1.cpp";
        suggestions.push_back(unity);

        const auto result = consolidator_->consolidate(suggestions);

        EXPECT_GE(result.size(), 1u);
        EXPECT_LE(result.size(), 3u);
    }

    TEST_F(ConsolidatorTest, ConsolidationCanBeDisabled) {
        ConsolidationOptions opts;
        opts.enable_consolidation = false;

        const SuggestionConsolidator disabled_consolidator(opts);

        std::vector<Suggestion> suggestions;
        suggestions.push_back(create_pch_suggestion("h1.h"));
        suggestions.push_back(create_pch_suggestion("h2.h"));
        suggestions.push_back(create_pch_suggestion("h3.h"));

        const auto result = disabled_consolidator.consolidate(suggestions);

        EXPECT_EQ(result.size(), suggestions.size());
    }

    TEST_F(ConsolidatorTest, LeavesExplicitTemplateSuggestionsUnconsolidated) {
        Suggestion first;
        first.type = SuggestionType::ExplicitTemplate;
        first.title = "Instantiate Foo<int>";
        first.target_file.path = "foo.cpp";
        first.edits.push_back(make_file_edit("foo.cpp", "template class Foo<int>;\n"));

        Suggestion second;
        second.type = SuggestionType::ExplicitTemplate;
        second.title = "Instantiate Bar<int>";
        second.target_file.path = "bar.cpp";
        second.edits.push_back(make_file_edit("bar.cpp", "template class Bar<int>;\n"));

        const auto result = consolidator_->consolidate({first, second});

        ASSERT_EQ(result.size(), 2u);
        EXPECT_EQ(result[0].type, SuggestionType::ExplicitTemplate);
        EXPECT_EQ(result[1].type, SuggestionType::ExplicitTemplate);
        EXPECT_EQ(result[0].title, "Instantiate Foo<int>");
        EXPECT_EQ(result[1].title, "Instantiate Bar<int>");
        EXPECT_EQ(result[0].target_file.path, "foo.cpp");
        EXPECT_EQ(result[1].target_file.path, "bar.cpp");
    }

    TEST_F(ConsolidatorTest, PreservesTheLeastConfidentChildEvidence) {
        Suggestion first;
        first.type = SuggestionType::IncludeRemoval;
        first.target_file.path = "first.cpp";
        first.confidence = 0.95;
        first.is_safe = true;

        Suggestion second;
        second.type = SuggestionType::IncludeRemoval;
        second.target_file.path = "second.cpp";
        second.confidence = 0.6;
        second.is_safe = true;

        const auto result = consolidator_->consolidate({first, second});

        ASSERT_EQ(result.size(), 1u);
        EXPECT_DOUBLE_EQ(result.front().confidence, 0.6);
        EXPECT_TRUE(result.front().is_safe);
    }

    TEST_F(ConsolidatorTest, DeduplicatesIdenticalEdits) {
        Suggestion first;
        first.type = SuggestionType::IncludeRemoval;
        first.target_file.path = "source.cpp";
        first.confidence = 0.9;
        first.edits.push_back(make_file_edit("source.cpp", "replacement"));

        Suggestion duplicate = first;
        const auto result = consolidator_->consolidate({first, duplicate});

        ASSERT_EQ(result.size(), 1u);
        ASSERT_EQ(result.front().edits.size(), 1u);
        EXPECT_EQ(result.front().edits.front().new_text, "replacement");
    }

    TEST_F(ConsolidatorTest, RejectsConflictingEdits) {
        Suggestion first;
        first.type = SuggestionType::IncludeRemoval;
        first.target_file.path = "source.cpp";
        first.confidence = 0.9;
        first.edits.push_back(TextEdit{
            "source.cpp", 0, 0, 0, 5, "first"
        });

        Suggestion second = first;
        second.edits.front().end_col = 7;
        second.edits.front().new_text = "second";

        const auto result = consolidator_->consolidate({first, second});

        EXPECT_TRUE(result.empty());
    }

    TEST_F(ConsolidatorTest, RejectsImpactOverflow) {
        Suggestion first;
        first.type = SuggestionType::IncludeRemoval;
        first.target_file.path = "first.cpp";
        first.confidence = 0.9;
        first.impact.total_files_affected = std::numeric_limits<std::size_t>::max();

        Suggestion second = first;
        second.target_file.path = "second.cpp";
        second.impact.total_files_affected = 1;

        const auto result = consolidator_->consolidate({first, second});

        EXPECT_TRUE(result.empty());
    }

    TEST_F(ConsolidatorTest, DoesNotInventPimplSavings) {
        Suggestion first;
        first.type = SuggestionType::PIMPLPattern;
        first.target_file.path = "first.hpp";
        first.confidence = 0.9;
        first.estimated_savings = std::chrono::milliseconds(100);
        first.estimated_savings_percent = 10.0;
        first.estimated_savings_evidence = EvidenceKind::Observed;

        Suggestion second = first;
        second.target_file.path = "second.hpp";
        second.estimated_savings = std::chrono::milliseconds(200);
        second.estimated_savings_percent = 20.0;

        const auto result = consolidator_->consolidate({first, second});

        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result.front().estimated_savings, Duration::zero());
        EXPECT_DOUBLE_EQ(result.front().estimated_savings_percent, 0.0);
        EXPECT_EQ(result.front().estimated_savings_evidence, EvidenceKind::Unavailable);
    }

}
