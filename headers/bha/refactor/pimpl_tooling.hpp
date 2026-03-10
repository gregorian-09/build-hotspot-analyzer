#ifndef BHA_REFACTOR_PIMPL_TOOLING_HPP
#define BHA_REFACTOR_PIMPL_TOOLING_HPP

#include "bha/refactor/types.hpp"

namespace bha::refactor {

    [[nodiscard]] bool clang_tooling_available() noexcept;

    [[nodiscard]] Result run_pimpl_refactor_with_clang_tooling(const PimplRequest& request);

}  // namespace bha::refactor

#endif  // BHA_REFACTOR_PIMPL_TOOLING_HPP
