#include "bha/suggestions/all_suggesters.hpp"
#include "bha/suggestions/suggester_catalog.hpp"

#include <gtest/gtest.h>

namespace bha::suggestions {

    TEST(SuggesterCatalogTest, ListsRegisteredSuggesters) {
        register_all_suggesters();
        const auto descriptors = list_suggester_descriptors();
        ASSERT_FALSE(descriptors.empty());

        const auto has_pch = std::ranges::any_of(descriptors, [](const SuggesterDescriptor& descriptor) {
            return descriptor.id == "pch";
        });
        const auto has_include = std::ranges::any_of(descriptors, [](const SuggesterDescriptor& descriptor) {
            return descriptor.id == "include";
        });

        EXPECT_TRUE(has_pch);
        EXPECT_TRUE(has_include);
    }

    TEST(SuggesterCatalogTest, FindsByAliasAndSuggestionType) {
        register_all_suggesters();

        const auto by_class = find_suggester_descriptor("PCHSuggester");
        ASSERT_TRUE(by_class.has_value());
        EXPECT_EQ(by_class->id, "pch");

        const auto by_type = find_suggester_descriptor("move-to-cpp");
        ASSERT_TRUE(by_type.has_value());
        EXPECT_EQ(by_type->id, "include");
    }

    TEST(SuggesterCatalogTest, ParsesSuggestionTypeTokens) {
        EXPECT_EQ(parse_suggestion_type_id("explicit-template"), SuggestionType::ExplicitTemplate);
        EXPECT_EQ(parse_suggestion_type_id("unity"), SuggestionType::UnityBuild);
        EXPECT_FALSE(parse_suggestion_type_id("not-a-type").has_value());
    }

}  // namespace bha::suggestions

