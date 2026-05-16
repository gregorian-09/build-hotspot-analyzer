#pragma once

#include <filesystem>
#include <string>

namespace bha::utils {

    namespace fs = std::filesystem;

    /**
     * Returns true when the path points to a compiler/system header location.
     *
     * This is intentionally conservative and cross-toolchain oriented: it treats
     * Clang builtin headers and common libc/libstdc++ include roots as system
     * headers that should not be edited by suggesters.
     */
    [[nodiscard]] inline bool is_likely_system_header_path(const fs::path& path) {
        const std::string value = path.generic_string();
        return value.starts_with("/usr/") ||
               value.starts_with("/opt/") ||
               value.find("/include/c++/") != std::string::npos ||
               value.find("/lib/clang/") != std::string::npos ||
               value.find("Program Files") != std::string::npos ||
                value.starts_with("<built-in>");
    }

}  // namespace bha::utils
