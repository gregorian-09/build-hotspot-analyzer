#include "bha/refactor/pimpl_eligibility.hpp"

#include <gtest/gtest.h>

namespace bha::refactor {

    TEST(PimplEligibilityTest, ExternalRefactorRequiresCompileContextAndSupportedShape) {
        PimplEligibilityState state;
        state.has_compile_context = true;
        state.private_data_members = 3;

        EXPECT_TRUE(supports_pimpl_external_refactor(state));
        EXPECT_FALSE(first_pimpl_eligibility_blocker(state).has_value());
    }

    TEST(PimplEligibilityTest, TemplateClassIsTheFirstSharedBlocker) {
        PimplEligibilityState state;
        state.has_compile_context = true;
        state.has_template_declaration = true;
        state.private_data_members = 2;

        const auto blocker = first_pimpl_eligibility_blocker(state);

        ASSERT_TRUE(blocker.has_value());
        EXPECT_EQ(*blocker, PimplEligibilityBlocker::TemplateDeclaration);
        EXPECT_FALSE(supports_pimpl_external_refactor(state));
        EXPECT_NE(
            pimpl_blocker_message(*blocker).find("Template classes"),
            std::string_view::npos
        );
    }

    TEST(PimplEligibilityTest, MissingPrivateDataMembersBlocksAutomation) {
        PimplEligibilityState state;
        state.has_compile_context = true;

        const auto blocker = first_pimpl_eligibility_blocker(state);

        ASSERT_TRUE(blocker.has_value());
        EXPECT_EQ(*blocker, PimplEligibilityBlocker::NoPrivateDataMembers);
        EXPECT_FALSE(supports_pimpl_external_refactor(state));
    }

    TEST(PimplEligibilityTest, ExplicitCopyDefinitionBlocksAutomation) {
        PimplEligibilityState state;
        state.has_compile_context = true;
        state.private_data_members = 4;
        state.has_explicit_copy_definition = true;

        const auto blocker = first_pimpl_eligibility_blocker(state);

        ASSERT_TRUE(blocker.has_value());
        EXPECT_EQ(*blocker, PimplEligibilityBlocker::ExplicitCopyDefinitions);
        EXPECT_FALSE(supports_pimpl_external_refactor(state));
    }

    TEST(PimplEligibilityTest, AdvisoryDescriptionsReflectSharedState) {
        PimplEligibilityState state;
        state.has_private_methods = true;
        state.has_private_inline_method_bodies = true;
        state.has_macro_generated_private_declarations = true;
        state.has_copy_constructor = true;
        state.has_explicit_copy_definition = true;

        const auto notes = describe_pimpl_advisory_conditions(state);

        EXPECT_EQ(notes.size(), 6u);
        EXPECT_NE(notes[0].find("No compile_commands entry"), std::string::npos);
        EXPECT_NE(notes[1].find("Private methods were detected"), std::string::npos);
        EXPECT_NE(notes[2].find("Private inline method bodies"), std::string::npos);
        EXPECT_NE(notes[3].find("Macro-generated private declarations"), std::string::npos);
        EXPECT_NE(notes[4].find("Copy semantics are present"), std::string::npos);
        EXPECT_NE(notes[5].find("User-defined copy constructor/assignment"), std::string::npos);
    }

}  // namespace bha::refactor
