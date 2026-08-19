#ifndef BHA_SUGGESTER_POLICY_HPP
#define BHA_SUGGESTER_POLICY_HPP

#include "bha/types.hpp"
#include "bha/utils/string_utils.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace bha::suggestions {
    namespace fs = std::filesystem;
    using bha::utils::lowercase_ascii;

    struct SuggesterPolicy {
        SuggesterLanguageSupport language_support = SuggesterLanguageSupport::CAndCXX;
        SuggesterAbiSensitivity abi_sensitivity = SuggesterAbiSensitivity::Low;
    };

    [[nodiscard]] inline SuggesterPolicy default_suggester_policy(
        const SuggestionType primary_type
    ) noexcept {
        switch (primary_type) {
            case SuggestionType::IncludeRemoval:
                return {
                    .language_support = SuggesterLanguageSupport::CAndCXX,
                    .abi_sensitivity = SuggesterAbiSensitivity::HeaderSurface
                };
            case SuggestionType::ForwardDeclaration:
                return {
                    .language_support = SuggesterLanguageSupport::CAndCXX,
                    .abi_sensitivity = SuggesterAbiSensitivity::HeaderSurface
                };
            case SuggestionType::ExplicitTemplate:
            case SuggestionType::HeaderSplit:
            case SuggestionType::PIMPLPattern:
                return {
                    .language_support = SuggesterLanguageSupport::CXXOnly,
                    .abi_sensitivity = SuggesterAbiSensitivity::HeaderSurface
                };
            case SuggestionType::PCHOptimization:
            case SuggestionType::UnityBuild:
                return {
                    .language_support = SuggesterLanguageSupport::BuildSystemLevel,
                    .abi_sensitivity = SuggesterAbiSensitivity::BuildConfiguration
                };
        }
        return {};
    }

    [[nodiscard]] inline SourceLanguageMode classify_source_language_extension(const fs::path& path) {
        const std::string extension = lowercase_ascii(path.extension().string());
        if (extension == ".c") {
            return SourceLanguageMode::C;
        }
        if (extension == ".cc" || extension == ".cpp" || extension == ".cxx" || extension == ".c++" ||
            extension == ".cp" || extension == ".ixx" || extension == ".cppm") {
            return SourceLanguageMode::CXX;
        }
        if (extension == ".m") {
            return SourceLanguageMode::ObjectiveC;
        }
        if (extension == ".mm" || extension == ".mxx") {
            return SourceLanguageMode::ObjectiveCXX;
        }
        return SourceLanguageMode::Unknown;
    }

    [[nodiscard]] inline SourceLanguageMode parse_explicit_language_mode(std::string_view token) {
        const std::string mode = lowercase_ascii(token);
        if (mode == "c" || mode == "c-header") {
            return SourceLanguageMode::C;
        }
        if (mode == "c++" || mode == "c++-header" || mode == "cxx" || mode == "cxx-header") {
            return SourceLanguageMode::CXX;
        }
        if (mode == "objective-c" || mode == "objc" || mode == "objective-c-header") {
            return SourceLanguageMode::ObjectiveC;
        }
        if (mode == "objective-c++" || mode == "objc++" || mode == "objective-c++-header") {
            return SourceLanguageMode::ObjectiveCXX;
        }
        return SourceLanguageMode::Unknown;
    }

    [[nodiscard]] inline SourceLanguageMode detect_source_language_mode(const CompilationUnit& unit) {
        for (std::size_t i = 0; i < unit.command_line.size(); ++i) {
            const std::string_view arg = unit.command_line[i];
            if (arg == "-x" && i + 1 < unit.command_line.size()) {
                if (const auto explicit_mode = parse_explicit_language_mode(unit.command_line[i + 1]);
                    explicit_mode != SourceLanguageMode::Unknown) {
                    return explicit_mode;
                }
                ++i;
                continue;
            }
            if (arg == "-ObjC") {
                return SourceLanguageMode::ObjectiveC;
            }
            if (arg == "-ObjC++") {
                return SourceLanguageMode::ObjectiveCXX;
            }
            if (arg == "/TC") {
                return SourceLanguageMode::C;
            }
            if (arg == "/TP") {
                return SourceLanguageMode::CXX;
            }
        }

        if (const auto by_extension = classify_source_language_extension(unit.source_file);
            by_extension != SourceLanguageMode::Unknown) {
            return by_extension;
        }

        if (!unit.command_line.empty()) {
            const std::string argv0 = lowercase_ascii(fs::path(unit.command_line.front()).filename().string());
            if (argv0.find("clang++") != std::string::npos || argv0.find("g++") != std::string::npos ||
                argv0 == "c++" || argv0 == "cxx" || argv0.find("icpx") != std::string::npos) {
                return SourceLanguageMode::CXX;
            }
            if (argv0.find("clang") != std::string::npos || argv0.find("gcc") != std::string::npos ||
                argv0 == "cc" || argv0.find("icx") != std::string::npos || argv0 == "cl" ||
                argv0 == "cl.exe") {
                return SourceLanguageMode::C;
            }
        }

        return SourceLanguageMode::Unknown;
    }

    [[nodiscard]] inline ProjectLanguageProfile summarize_project_language_profile(const BuildTrace& trace) {
        ProjectLanguageProfile profile;
        for (const auto& unit : trace.units) {
            switch (detect_source_language_mode(unit)) {
                case SourceLanguageMode::C:
                    ++profile.c_units;
                    break;
                case SourceLanguageMode::CXX:
                    ++profile.cxx_units;
                    break;
                case SourceLanguageMode::ObjectiveC:
                    ++profile.objc_units;
                    break;
                case SourceLanguageMode::ObjectiveCXX:
                    ++profile.objcxx_units;
                    break;
                case SourceLanguageMode::Unknown:
                    ++profile.unknown_units;
                    break;
            }
        }
        return profile;
    }

    [[nodiscard]] inline bool language_support_matches(
        const SuggesterLanguageSupport support,
        const ProjectLanguageProfile& profile
    ) noexcept {
        if (profile.empty()) {
            return true;
        }

        switch (support) {
            case SuggesterLanguageSupport::COnly:
                return profile.has_c_family();
            case SuggesterLanguageSupport::CXXOnly:
                return profile.has_cxx_family();
            case SuggesterLanguageSupport::CAndCXX:
            case SuggesterLanguageSupport::BuildSystemLevel:
                return true;
        }
        return true;
    }

}  // namespace bha::suggestions

#endif  // BHA_SUGGESTER_POLICY_HPP
