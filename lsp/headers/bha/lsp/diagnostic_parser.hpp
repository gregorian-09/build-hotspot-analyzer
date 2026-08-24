#pragma once

#include "types.hpp"

#include <string_view>
#include <vector>

namespace bha::lsp {

/**
 * Parse source-located compiler errors and warnings from text build output.
 *
 * The parser deliberately accepts only the documented location forms emitted
 * by Clang/GCC and MSVC. Lines that do not contain a complete source location
 * are left for the caller's generic build-failure diagnostic.
 */
[[nodiscard]] std::vector<Diagnostic> parse_compiler_diagnostics(std::string_view output);

}  // namespace bha::lsp
