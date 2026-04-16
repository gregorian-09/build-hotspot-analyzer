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

    TEST(SuggesterCatalogTest, ExposesLanguageAndAbiPolicyMetadata) {
        register_all_suggesters();

        const auto template_descriptor = find_suggester_descriptor("template-instantiation");
        ASSERT_TRUE(template_descriptor.has_value());
        EXPECT_EQ(template_descriptor->language_support, SuggesterLanguageSupport::CXXOnly);
        EXPECT_EQ(template_descriptor->abi_sensitivity, SuggesterAbiSensitivity::HeaderSurface);

        const auto pch_descriptor = find_suggester_descriptor("pch");
        ASSERT_TRUE(pch_descriptor.has_value());
        EXPECT_EQ(pch_descriptor->language_support, SuggesterLanguageSupport::BuildSystemLevel);
        EXPECT_EQ(pch_descriptor->abi_sensitivity, SuggesterAbiSensitivity::BuildConfiguration);
    }

    TEST(SuggesterCatalogTest, DetectsSourceLanguageFromCompileCommands) {
        CompilationUnit c_unit;
        c_unit.source_file = "src/example.c";
        c_unit.command_line = {"clang", "-x", "c", "-c", "src/example.c"};
        EXPECT_EQ(detect_source_language_mode(c_unit), SourceLanguageMode::C);

        CompilationUnit cxx_unit;
        cxx_unit.source_file = "src/example.cpp";
        cxx_unit.command_line = {"clang++", "-c", "src/example.cpp"};
        EXPECT_EQ(detect_source_language_mode(cxx_unit), SourceLanguageMode::CXX);

        CompilationUnit msvc_cxx_unit;
        msvc_cxx_unit.source_file = "src/example.c";
        msvc_cxx_unit.command_line = {"cl.exe", "/TP", "/c", "src/example.c"};
        EXPECT_EQ(detect_source_language_mode(msvc_cxx_unit), SourceLanguageMode::CXX);
    }

    TEST(SuggesterCatalogTest, AppliesLanguageSupportRulesToProjectProfiles) {
        ProjectLanguageProfile c_only_profile;
        c_only_profile.c_units = 4;

        ProjectLanguageProfile mixed_profile;
        mixed_profile.c_units = 2;
        mixed_profile.cxx_units = 3;

        EXPECT_FALSE(language_support_matches(SuggesterLanguageSupport::CXXOnly, c_only_profile));
        EXPECT_TRUE(language_support_matches(SuggesterLanguageSupport::CAndCXX, c_only_profile));
        EXPECT_TRUE(language_support_matches(SuggesterLanguageSupport::BuildSystemLevel, c_only_profile));
        EXPECT_TRUE(language_support_matches(SuggesterLanguageSupport::CXXOnly, mixed_profile));
    }

}  // namespace bha::suggestions
