#ifndef BHA_REFACTOR_PIMPL_ELIGIBILITY_HPP
#define BHA_REFACTOR_PIMPL_ELIGIBILITY_HPP

/**
 * @file pimpl_eligibility.hpp
 * @brief Eligibility rules for external PIMPL refactor automation.
 */

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bha::refactor {

    /**
     * @brief Canonical blocker categories for external PIMPL refactor.
     */
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

    /**
     * @brief Extracted semantic state used by PIMPL eligibility checks.
     */
    struct PimplEligibilityState {
        /// Whether compile-command/semantic context is available.
        bool has_compile_context = false;
        /// Class declaration appears macro-generated.
        bool has_macro_generated_class = false;
        /// Class is templated.
        bool has_template_declaration = false;
        /// Class has base classes/inheritance.
        bool has_inheritance = false;
        /// Class declares virtual members.
        bool has_virtual_members = false;
        /// Class contains private methods.
        bool has_private_methods = false;
        /// Class has inline private method bodies.
        bool has_private_inline_method_bodies = false;
        /// Private declarations include macro expansions.
        bool has_macro_generated_private_declarations = false;
        /// Class declares copy constructor.
        bool has_copy_constructor = false;
        /// Class defines explicit copy operations that require manual handling.
        bool has_explicit_copy_definition = false;
        /// Preprocessor directives appear inside class body.
        bool has_preprocessor_in_class = false;
        /// Count of non-static private data members.
        std::size_t private_data_members = 0;
    };

    /**
     * @brief Return the first hard blocker for external PIMPL refactor.
     *
     * @param state Eligibility state snapshot.
     * @return First blocking category when present, otherwise `std::nullopt`.
     */
    [[nodiscard]] std::optional<PimplEligibilityBlocker> first_pimpl_eligibility_blocker(
        const PimplEligibilityState& state
    ) noexcept;

    /**
     * @brief Determine whether external PIMPL refactor is supported.
     *
     * @param state Eligibility state snapshot.
     * @return `true` when no hard blockers are present.
     */
    [[nodiscard]] bool supports_pimpl_external_refactor(
        const PimplEligibilityState& state
    ) noexcept;

    /**
     * @brief Map blocker enum to user-facing explanation text.
     */
    [[nodiscard]] std::string_view pimpl_blocker_message(
        PimplEligibilityBlocker blocker
    ) noexcept;

    /**
     * @brief Produce advisory (non-blocking) notes for a partially supported class.
     *
     * @param state Eligibility state snapshot.
     * @return Ordered list of advisory conditions.
     */
    [[nodiscard]] std::vector<std::string> describe_pimpl_advisory_conditions(
        const PimplEligibilityState& state
    );

}  // namespace bha::refactor

#endif  // BHA_REFACTOR_PIMPL_ELIGIBILITY_HPP
