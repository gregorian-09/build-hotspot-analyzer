#pragma once

#include "bha/suggestions/suggester.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bha::suggestions {

    struct SuggesterDescriptor {
        std::string id;
        std::string class_name;
        std::string description;
        std::vector<SuggestionType> supported_types;
        std::vector<std::string> input_requirements;
        bool potentially_auto_applicable = false;
        bool supports_explain_mode = true;
        SuggesterLanguageSupport language_support = SuggesterLanguageSupport::CAndCXX;
        SuggesterAbiSensitivity abi_sensitivity = SuggesterAbiSensitivity::Low;
    };

    [[nodiscard]] inline std::string normalize_suggester_token(std::string_view token) {
        std::string normalized;
        normalized.reserve(token.size());
        bool previous_dash = false;
        for (const char c : token) {
            const unsigned char ch = static_cast<unsigned char>(c);
            if (std::isalnum(ch) != 0) {
                normalized.push_back(static_cast<char>(std::tolower(ch)));
                previous_dash = false;
                continue;
            }
            if (!previous_dash && !normalized.empty()) {
                normalized.push_back('-');
                previous_dash = true;
            }
        }
        while (!normalized.empty() && normalized.back() == '-') {
            normalized.pop_back();
        }
        return normalized;
    }

    [[nodiscard]] inline std::string suggestion_type_id(const SuggestionType type) {
        switch (type) {
            case SuggestionType::PCHOptimization:
                return "pch";
            case SuggestionType::ForwardDeclaration:
                return "forward-decl";
            case SuggestionType::HeaderSplit:
                return "header-split";
            case SuggestionType::IncludeRemoval:
                return "include-removal";
            case SuggestionType::MoveToCpp:
                return "move-to-cpp";
            case SuggestionType::ExplicitTemplate:
                return "template-instantiation";
            case SuggestionType::UnityBuild:
                return "unity-build";
            case SuggestionType::PIMPLPattern:
                return "pimpl";
        }
        return "unknown";
    }

    [[nodiscard]] inline std::optional<SuggestionType> parse_suggestion_type_id(std::string_view token) {
        const std::string normalized = normalize_suggester_token(token);
        if (normalized == "pch" || normalized == "pch-optimization") {
            return SuggestionType::PCHOptimization;
        }
        if (normalized == "forward-decl" || normalized == "forward-declaration") {
            return SuggestionType::ForwardDeclaration;
        }
        if (normalized == "header-split") {
            return SuggestionType::HeaderSplit;
        }
        if (normalized == "include-removal" || normalized == "include-cleanup" || normalized == "include") {
            return SuggestionType::IncludeRemoval;
        }
        if (normalized == "move-to-cpp") {
            return SuggestionType::MoveToCpp;
        }
        if (normalized == "template-instantiation" || normalized == "explicit-template" || normalized == "template") {
            return SuggestionType::ExplicitTemplate;
        }
        if (normalized == "unity-build" || normalized == "unity") {
            return SuggestionType::UnityBuild;
        }
        if (normalized == "pimpl" || normalized == "pimpl-pattern") {
            return SuggestionType::PIMPLPattern;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::string canonical_suggester_id(const ISuggester& suggester) {
        switch (suggester.suggestion_type()) {
            case SuggestionType::PCHOptimization:
                return "pch";
            case SuggestionType::ForwardDeclaration:
                return "forward-decl";
            case SuggestionType::HeaderSplit:
                return "header-split";
            case SuggestionType::ExplicitTemplate:
                return "template-instantiation";
            case SuggestionType::UnityBuild:
                return "unity-build";
            case SuggestionType::PIMPLPattern:
                return "pimpl";
            case SuggestionType::IncludeRemoval: {
                const auto supported = suggester.supported_types();
                if (std::ranges::find(supported, SuggestionType::MoveToCpp) != supported.end()) {
                    return "include";
                }
                return "include-removal";
            }
            case SuggestionType::MoveToCpp:
                return "move-to-cpp";
        }
        return normalize_suggester_token(suggester.name());
    }

    [[nodiscard]] inline std::vector<std::string> default_input_requirements(
        const ISuggester& suggester
    ) {
        const SuggesterPolicy policy = suggester.policy();
        std::vector<std::string> requirements{
            "Build traces (.json) from a profiled compile"
        };
        const auto supported = suggester.supported_types();
        if (policy.language_support == SuggesterLanguageSupport::CXXOnly) {
            requirements.emplace_back("C++ translation units in compile_commands.json");
        } else if (policy.language_support == SuggesterLanguageSupport::COnly) {
            requirements.emplace_back("C translation units in compile_commands.json");
        }
        if (std::ranges::find(supported, SuggestionType::IncludeRemoval) != supported.end()) {
            requirements.emplace_back("compile_commands.json for clang-tidy verified include cleanup");
        }
        if (suggester.suggestion_type() == SuggestionType::PIMPLPattern) {
            requirements.emplace_back("compile_commands.json for AST-backed external refactor apply path");
        }
        if (suggester.suggestion_type() == SuggestionType::PCHOptimization ||
            suggester.suggestion_type() == SuggestionType::ExplicitTemplate ||
            suggester.suggestion_type() == SuggestionType::UnityBuild) {
            requirements.emplace_back("Build-system files for auto-edit emission (CMake/Make/Meson/etc.)");
        }
        if (policy.abi_sensitivity == SuggesterAbiSensitivity::HeaderSurface) {
            requirements.emplace_back("Conservative validation for ABI-visible/public headers and extern \"C\" surfaces");
        } else if (policy.abi_sensitivity == SuggesterAbiSensitivity::BuildConfiguration) {
            requirements.emplace_back("Target-level build validation before applying build-configuration edits");
        }
        return requirements;
    }

    [[nodiscard]] inline bool potentially_auto_applicable(const ISuggester& suggester) {
        if (suggester.suggestion_type() == SuggestionType::PIMPLPattern) {
            return false;
        }
        const auto supported = suggester.supported_types();
        return std::ranges::find(supported, SuggestionType::PCHOptimization) != supported.end() ||
               std::ranges::find(supported, SuggestionType::IncludeRemoval) != supported.end() ||
               std::ranges::find(supported, SuggestionType::MoveToCpp) != supported.end() ||
               std::ranges::find(supported, SuggestionType::ExplicitTemplate) != supported.end() ||
               std::ranges::find(supported, SuggestionType::UnityBuild) != supported.end() ||
               std::ranges::find(supported, SuggestionType::ForwardDeclaration) != supported.end();
    }

    [[nodiscard]] inline std::vector<SuggesterDescriptor> list_suggester_descriptors() {
        std::vector<SuggesterDescriptor> descriptors;
        descriptors.reserve(SuggesterRegistry::instance().suggesters().size());
        for (const auto& suggester : SuggesterRegistry::instance().suggesters()) {
            if (!suggester) {
                continue;
            }
            SuggesterDescriptor descriptor;
            descriptor.id = canonical_suggester_id(*suggester);
            descriptor.class_name = std::string(suggester->name());
            descriptor.description = std::string(suggester->description());
            descriptor.supported_types = suggester->supported_types();
            descriptor.input_requirements = default_input_requirements(*suggester);
            descriptor.potentially_auto_applicable = potentially_auto_applicable(*suggester);
            descriptor.language_support = suggester->policy().language_support;
            descriptor.abi_sensitivity = suggester->policy().abi_sensitivity;
            descriptors.push_back(std::move(descriptor));
        }
        std::ranges::sort(descriptors, [](const auto& lhs, const auto& rhs) {
            return lhs.id < rhs.id;
        });
        return descriptors;
    }

    [[nodiscard]] inline std::optional<SuggesterDescriptor> find_suggester_descriptor(std::string_view token) {
        const std::string normalized = normalize_suggester_token(token);
        if (normalized.empty()) {
            return std::nullopt;
        }

        const auto descriptors = list_suggester_descriptors();
        for (const auto& descriptor : descriptors) {
            if (normalized == descriptor.id ||
                normalized == normalize_suggester_token(descriptor.class_name)) {
                return descriptor;
            }

            for (const auto type : descriptor.supported_types) {
                if (normalized == suggestion_type_id(type)) {
                    return descriptor;
                }
            }
        }
        return std::nullopt;
    }

}  // namespace bha::suggestions
