#include "bha/refactor/pimpl_eligibility.hpp"

namespace bha::refactor {

    std::optional<PimplEligibilityBlocker> first_pimpl_eligibility_blocker(
        const PimplEligibilityState& state
    ) noexcept {
        if (state.has_macro_generated_class) {
            return PimplEligibilityBlocker::MacroGeneratedClass;
        }
        if (state.has_template_declaration) {
            return PimplEligibilityBlocker::TemplateDeclaration;
        }
        if (state.has_inheritance) {
            return PimplEligibilityBlocker::Inheritance;
        }
        if (state.private_data_members == 0) {
            return PimplEligibilityBlocker::NoPrivateDataMembers;
        }
        if (state.has_virtual_members) {
            return PimplEligibilityBlocker::VirtualMembers;
        }
        if (state.has_private_inline_method_bodies) {
            return PimplEligibilityBlocker::PrivateInlineMethodBodies;
        }
        if (state.has_macro_generated_private_declarations) {
            return PimplEligibilityBlocker::MacroGeneratedPrivateDeclarations;
        }
        if (state.has_explicit_copy_definition) {
            return PimplEligibilityBlocker::ExplicitCopyDefinitions;
        }
        return std::nullopt;
    }

    bool supports_pimpl_external_refactor(const PimplEligibilityState& state) noexcept {
        return state.has_compile_context && !first_pimpl_eligibility_blocker(state).has_value();
    }

    std::string_view pimpl_blocker_message(const PimplEligibilityBlocker blocker) noexcept {
        switch (blocker) {
            case PimplEligibilityBlocker::MacroGeneratedClass:
                return "The target class is declared through a macro expansion; automatic PIMPL refactoring is disabled for macro-generated class layouts";
            case PimplEligibilityBlocker::TemplateDeclaration:
                return "Template classes are not supported by the automatic PIMPL refactor; use manual refactoring for this class shape";
            case PimplEligibilityBlocker::Inheritance:
                return "Classes with inheritance are not supported by the automatic PIMPL refactor; use manual refactoring for this class shape";
            case PimplEligibilityBlocker::NoPrivateDataMembers:
                return "No private data members were found to move behind Impl; automatic PIMPL refactoring is not applicable";
            case PimplEligibilityBlocker::VirtualMembers:
                return "Classes with virtual member functions are not supported by the automatic PIMPL refactor; use manual refactoring for this class shape";
            case PimplEligibilityBlocker::PrivateInlineMethodBodies:
                return "Private inline method bodies in the class definition are not supported by the automatic PIMPL refactor; use manual refactoring for this class shape";
            case PimplEligibilityBlocker::MacroGeneratedPrivateDeclarations:
                return "Private declarations produced through macro expansions are not supported by the automatic PIMPL refactor";
            case PimplEligibilityBlocker::ExplicitCopyDefinitions:
                return "User-defined copy constructor or copy assignment bodies were detected; automatic PIMPL refactoring is disabled to preserve copy semantics safely";
        }
        return "Automatic PIMPL refactoring is not supported for this class shape";
    }

}  // namespace bha::refactor
