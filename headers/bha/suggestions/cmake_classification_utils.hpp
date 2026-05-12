#ifndef BHA_SUGGESTIONS_CMAKE_CLASSIFICATION_UTILS_HPP
#define BHA_SUGGESTIONS_CMAKE_CLASSIFICATION_UTILS_HPP

#include "bha/suggestions/suggester.hpp"

#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>

namespace bha::suggestions {

    enum class CMakeSourceTokenMode {
        Strict,
        AllowLeadingGeneratorToken
    };

    enum class CMakeTargetNameMode {
        Strict,
        AllowGeneratorExpressions
    };

    [[nodiscard]] inline bool is_probable_source_token(
        std::string_view token,
        const CMakeSourceTokenMode mode
    ) {
        if (token.empty()) {
            return false;
        }
        if (mode == CMakeSourceTokenMode::Strict) {
            if (token.find('$') != std::string_view::npos ||
                token.find('<') != std::string_view::npos) {
                return false;
            }
        } else if (token.front() == '$') {
            return false;
        }

        static constexpr std::array<std::string_view, 9> kExts = {
            ".c", ".cc", ".cpp", ".cxx", ".c++", ".mm", ".m", ".ixx", ".cu"
        };
        for (const auto ext : kExts) {
            if (token.size() >= ext.size() &&
                token.substr(token.size() - ext.size()) == ext) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline bool is_probable_cmake_target_name(
        std::string_view name,
        const CMakeTargetNameMode mode
    ) {
        if (name.empty() || name.front() == '-') {
            return false;
        }
        if (mode == CMakeTargetNameMode::Strict &&
            (name.find('$') != std::string_view::npos ||
             name.find('<') != std::string_view::npos ||
             name.find('>') != std::string_view::npos)) {
            return false;
        }

        int brace_depth = 0;
        int angle_depth = 0;
        for (std::size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            const unsigned char ch = static_cast<unsigned char>(c);
            if (std::isalnum(ch) != 0 || c == '_' || c == '-' || c == '.') {
                continue;
            }
            if (mode == CMakeTargetNameMode::AllowGeneratorExpressions) {
                if (c == '$') {
                    continue;
                }
                if (c == '{') {
                    ++brace_depth;
                    continue;
                }
                if (c == '}') {
                    if (brace_depth == 0) {
                        return false;
                    }
                    --brace_depth;
                    continue;
                }
                if (c == '<') {
                    ++angle_depth;
                    continue;
                }
                if (c == '>') {
                    if (angle_depth == 0) {
                        return false;
                    }
                    --angle_depth;
                    continue;
                }
                if (c == ':') {
                    if (i + 1 >= name.size() || name[i + 1] != ':') {
                        return false;
                    }
                    ++i;
                    continue;
                }
            }
            return false;
        }
        return brace_depth == 0 && angle_depth == 0;
    }

    [[nodiscard]] inline bool is_macro_keyword_lower(std::string_view token) {
        static const std::unordered_set<std::string> kKeywords = {
            "name",
            "hdrs",
            "srcs",
            "sources",
            "src",
            "source",
            "copts",
            "defines",
            "linkopts",
            "deps",
            "public",
            "private",
            "interface",
            "textual_hdrs",
            "testonly",
            "disable_install"
        };
        return kKeywords.contains(lowercase_ascii(token));
    }

    [[nodiscard]] inline bool is_excluded_cmake_path(const fs::path& path) {
        const std::string lower = lowercase_ascii(path.generic_string());
        if (lower.find("/.git/") != std::string::npos ||
            lower.find("/cmakefiles/") != std::string::npos ||
            lower.find("/_deps/") != std::string::npos ||
            lower.find("/cmake/") != std::string::npos ||
            lower.find("example") != std::string::npos ||
            lower.find("benchmark") != std::string::npos ||
            lower.find("install_test") != std::string::npos ||
            lower.find(".lsp-optimization-backup") != std::string::npos ||
            lower.find(".bha_traces") != std::string::npos ||
            lower.find("/test") != std::string::npos ||
            lower.find("/tests") != std::string::npos) {
            return true;
        }
        for (const auto& part : path) {
            const std::string c = lowercase_ascii(part.string());
            if (c == "build" || c == "out" || c.rfind("cmake-build", 0) == 0) {
                return true;
            }
        }
        return false;
    }

}  // namespace bha::suggestions

#endif
