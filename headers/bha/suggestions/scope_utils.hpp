#ifndef BHA_SCOPE_UTILS_HPP
#define BHA_SCOPE_UTILS_HPP

#include "bha/suggestions/suggester.hpp"

#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace bha::suggestions {

    enum class ScopeFrameKind {
        Namespace,
        MacroWrapper
    };

    struct MacroWrapperScope {
        std::string open_name;
        std::string open_text;
        std::string close_name;
        std::string close_text;
    };

    struct ScopeFrame {
        ScopeFrameKind kind = ScopeFrameKind::Namespace;
        std::string name;
        std::size_t open_depth = 0;
        MacroWrapperScope macro;
    };

    [[nodiscard]] inline std::vector<std::string> split_namespace_path(const std::string& ns_path) {
        std::vector<std::string> result;
        std::size_t offset = 0;
        while (offset < ns_path.size()) {
            const auto pos = ns_path.find("::", offset);
            if (pos == std::string::npos) {
                result.push_back(ns_path.substr(offset));
                break;
            }
            result.push_back(ns_path.substr(offset, pos - offset));
            offset = pos + 2;
        }
        for (auto& part : result) {
            part = trim_whitespace_copy(std::move(part));
        }
        result.erase(
            std::remove_if(result.begin(), result.end(), [](const std::string& part) { return part.empty(); }),
            result.end()
        );
        return result;
    }

    [[nodiscard]] inline std::vector<std::string> collect_active_namespaces(
        const std::vector<ScopeFrame>& scope_stack
    ) {
        std::vector<std::string> result;
        for (const auto& scope : scope_stack) {
            if (scope.kind == ScopeFrameKind::Namespace) {
                result.push_back(scope.name);
            }
        }
        return result;
    }

    [[nodiscard]] inline std::optional<std::string> derive_matching_close_macro(std::string name) {
        if (name.ends_with("_BEGIN")) {
            name.replace(name.size() - 6, 6, "_END");
            return name;
        }
        if (name.ends_with("_OPEN")) {
            name.replace(name.size() - 5, 5, "_CLOSE");
            return name;
        }
        if (name.ends_with("_PUSH")) {
            name.replace(name.size() - 5, 5, "_POP");
            return name;
        }
        if (name.starts_with("BEGIN_")) {
            return "END_" + name.substr(6);
        }
        if (name.starts_with("OPEN_")) {
            return "CLOSE_" + name.substr(5);
        }
        if (name.starts_with("PUSH_")) {
            return "POP_" + name.substr(5);
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::string> parse_scope_macro_name(const std::string& line) {
        static const std::regex macro_regex(
            R"(^\s*([A-Z][A-Z0-9_]*|(?:BEGIN|OPEN|PUSH)_[A-Z0-9_]+)\s*(\([^;{}]*\))?\s*$)"
        );

        std::smatch match;
        if (!std::regex_match(line, match, macro_regex)) {
            return std::nullopt;
        }
        return match[1].str();
    }

    [[nodiscard]] inline std::optional<MacroWrapperScope> parse_scope_macro_open(const std::string& line) {
        const auto macro_name = parse_scope_macro_name(line);
        if (!macro_name.has_value()) {
            return std::nullopt;
        }
        const auto close_name = derive_matching_close_macro(*macro_name);
        if (!close_name.has_value()) {
            return std::nullopt;
        }

        MacroWrapperScope scope;
        scope.open_name = *macro_name;
        scope.open_text = trim_whitespace_copy(line);
        scope.close_name = *close_name;
        scope.close_text = *close_name;
        return scope;
    }

    [[nodiscard]] inline std::optional<std::string> parse_scope_macro_close(const std::string& line) {
        const auto macro_name = parse_scope_macro_name(line);
        if (!macro_name.has_value()) {
            return std::nullopt;
        }
        if (macro_name->ends_with("_END") ||
            macro_name->ends_with("_CLOSE") ||
            macro_name->ends_with("_POP") ||
            macro_name->starts_with("END_") ||
            macro_name->starts_with("CLOSE_") ||
            macro_name->starts_with("POP_")) {
            return *macro_name;
        }
        return std::nullopt;
    }

}  // namespace bha::suggestions

#endif
