//
// Created by gregorian-rayne on 01/18/26.
//

#include "bha/suggestions/consolidator.hpp"

#include <gtest/gtest.h>

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

    TEST_F(ConsolidatorTest, ConsolidatesPCHSuggestions) {
        std::vector<Suggestion> suggestions;
        suggestions.push_back(create_pch_suggestion("header1.h"));
        suggestions.push_back(create_pch_suggestion("header2.h"));
        suggestions.push_back(create_pch_suggestion("header3.h"));

        const auto result = consolidator_->consolidate(suggestions);

        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0].type, SuggestionType::PCHOptimization);
        EXPECT_FALSE(result[0].description.empty());
    }

    TEST_F(ConsolidatorTest, SeparatesStableFromVolatile) {
        std::vector<Suggestion> suggestions;
        suggestions.push_back(create_pch_suggestion("stable_header.h", true));
        suggestions.push_back(create_pch_suggestion("volatile_header.h", false));

        const auto result = consolidator_->consolidate(suggestions);

        ASSERT_EQ(result.size(), 1u);

        const auto& desc = result[0].description;
        EXPECT_TRUE(desc.find("stable") != std::string::npos ||
                    desc.find("external") != std::string::npos ||
                    desc.find("Add to precompiled header") != std::string::npos);
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

    TEST_F(ConsolidatorTest, MergesImpactMetrics) {
        std::vector<Suggestion> suggestions;

        Suggestion s1 = create_pch_suggestion("h1.h");
        s1.impact.files_benefiting = {fs::path("a.cpp"), fs::path("b.cpp")};
        s1.impact.total_files_affected = 10;
        s1.impact.cumulative_savings = std::chrono::milliseconds(200);
        suggestions.push_back(s1);

        Suggestion s2 = create_pch_suggestion("h2.h");
        s2.impact.files_benefiting = {fs::path("b.cpp"), fs::path("c.cpp"), fs::path("d.cpp")};
        s2.impact.total_files_affected = 15;
        s2.impact.cumulative_savings = std::chrono::milliseconds(300);
        suggestions.push_back(s2);

        const auto result = consolidator_->consolidate(suggestions);

        ASSERT_EQ(result.size(), 1u);
        EXPECT_GE(result[0].impact.files_benefiting.size(), 3u);
        EXPECT_GE(result[0].impact.total_files_affected, 10u);
        EXPECT_GT(result[0].impact.cumulative_savings.count(), 0);
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

    TEST_F(ConsolidatorTest, LeavesMoveToCppSuggestionsUnconsolidated) {
        Suggestion first;
        first.type = SuggestionType::MoveToCpp;
        first.title = "Move include A";
        first.target_file.path = "a.hpp";

        Suggestion second;
        second.type = SuggestionType::MoveToCpp;
        second.title = "Move include B";
        second.target_file.path = "b.hpp";

        const auto result = consolidator_->consolidate({first, second});

        ASSERT_EQ(result.size(), 2u);
        EXPECT_EQ(result[0].type, SuggestionType::MoveToCpp);
        EXPECT_EQ(result[1].type, SuggestionType::MoveToCpp);
        EXPECT_EQ(result[0].title, "Move include A");
        EXPECT_EQ(result[1].title, "Move include B");
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

    TEST_F(ConsolidatorTest, ConsolidatedPchUsesProjectNativeStyleAndSkipsInternalHeaders) {
        Suggestion external = create_pch_suggestion("<vector>");
        Suggestion public_header = create_pch_suggestion("include/public.hpp");
        Suggestion internal_header = create_pch_suggestion("db/internal.hpp");

        const auto result = consolidator_->consolidate({external, public_header, internal_header});

        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result.front().type, SuggestionType::PCHOptimization);

        const auto pch_edit = std::find_if(
            result.front().edits.begin(),
            result.front().edits.end(),
            [](const TextEdit& edit) {
                return edit.file == "pch.h";
            }
        );
        ASSERT_NE(pch_edit, result.front().edits.end());
        EXPECT_NE(pch_edit->new_text.find("#include <vector>"), std::string::npos);
        EXPECT_NE(pch_edit->new_text.find("#include \"public.hpp\""), std::string::npos);
        EXPECT_EQ(pch_edit->new_text.find("db/internal.hpp"), std::string::npos);
        EXPECT_NE(
            result.front().description.find("Excluded from shared pch.h"),
            std::string::npos
        );
    }
}
