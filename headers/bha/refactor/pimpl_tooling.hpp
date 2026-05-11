#ifndef BHA_REFACTOR_PIMPL_TOOLING_HPP
#define BHA_REFACTOR_PIMPL_TOOLING_HPP

/**
 * @file pimpl_tooling.hpp
 * @brief Clang-tooling execution entrypoints for external PIMPL refactor.
 */

#include "bha/refactor/types.hpp"

namespace bha::refactor {

    /**
     * @brief Check whether clang-tooling backend is available in current environment.
     */
    [[nodiscard]] bool clang_tooling_available() noexcept;

    /**
     * @brief Run PIMPL refactor via clang-tooling backend.
     *
     * @param request Refactor request descriptor.
     * @return Full refactor result including replacements and diagnostics.
     */
    [[nodiscard]] Result run_pimpl_refactor_with_clang_tooling(const PimplRequest& request);

}  // namespace bha::refactor

#endif  // BHA_REFACTOR_PIMPL_TOOLING_HPP
