#ifndef BHA_REFACTOR_PIMPL_ELIGIBILITY_HPP
#define BHA_REFACTOR_PIMPL_ELIGIBILITY_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bha::refactor {

    enum class PimplEligibilityBlocker {
        MacroGeneratedClass,
        TemplateDeclaration,
        Inheritance,
        NoPrivateDataMembers,
        VirtualMembers,
        PrivateInlineMethodBodies,
        MacroGeneratedPrivateDeclarations,
        PreprocessorInClass,
        ExplicitCopyDefinitions,
    };

    struct PimplEligibilityState {
        bool has_compile_context = false;
        bool has_macro_generated_class = false;
        bool has_template_declaration = false;
        bool has_inheritance = false;
        bool has_virtual_members = false;
        bool has_private_methods = false;
        bool has_private_inline_method_bodies = false;
        bool has_macro_generated_private_declarations = false;
        bool has_copy_constructor = false;
        bool has_explicit_copy_definition = false;
        bool has_preprocessor_in_class = false;
        std::size_t private_data_members = 0;
    };

    [[nodiscard]] std::optional<PimplEligibilityBlocker> first_pimpl_eligibility_blocker(
        const PimplEligibilityState& state
    ) noexcept;

    [[nodiscard]] bool supports_pimpl_external_refactor(
        const PimplEligibilityState& state
    ) noexcept;

    [[nodiscard]] std::string_view pimpl_blocker_message(
        PimplEligibilityBlocker blocker
    ) noexcept;

    [[nodiscard]] std::vector<std::string> describe_pimpl_advisory_conditions(
        const PimplEligibilityState& state
    );

}  // namespace bha::refactor

#endif  // BHA_REFACTOR_PIMPL_ELIGIBILITY_HPP
