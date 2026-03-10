#pragma once

#include "bha/suggestions/suggester.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bha::suggestions
{
    enum class UnrealPCHUsageMode {
        Unknown,
        NoPCHs,
        UseExplicitOrSharedPCHs,
        UseSharedPCHs,
        Other
    };

    struct UnrealModuleRules {
        std::string module_name;
        fs::path build_cs_path;
        std::optional<bool> enforce_iwyu;
        std::optional<std::size_t> enforce_iwyu_line;
        std::optional<bool> use_unity;
        std::optional<std::size_t> use_unity_line;
        UnrealPCHUsageMode pch_usage = UnrealPCHUsageMode::Unknown;
        std::optional<std::size_t> pch_usage_line;
    };

    struct UnrealModuleStats {
        std::size_t source_files = 0;
        Duration total_compile_time = Duration::zero();
        Duration include_parse_time = Duration::zero();
    };

    struct UnrealModuleContext {
        UnrealModuleRules rules;
        UnrealModuleStats stats;
    };

    [[nodiscard]] inline std::string to_lower_copy(const std::string& value) {
        std::string lowered = value;
        std::ranges::transform(
            lowered,
            lowered.begin(),
            [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            }
        );
        return lowered;
    }

    [[nodiscard]] inline std::string trim_copy(const std::string& value) {
        std::size_t begin = 0;
        while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
            ++begin;
        }
        std::size_t end = value.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
            --end;
        }
        return value.substr(begin, end - begin);
    }

    [[nodiscard]] inline std::string strip_line_comment(std::string line) {
        if (const auto pos = line.find("//"); pos != std::string::npos) {
            line.erase(pos);
        }
        return line;
    }

    [[nodiscard]] inline bool is_unreal_build_file(const fs::path& path) {
        return path.filename().string().ends_with(".Build.cs");
    }

    [[nodiscard]] inline std::string module_name_from_build_file(const fs::path& path) {
        std::string stem = path.stem().string();
        if (stem.ends_with(".Build")) {
            stem.erase(stem.size() - std::string(".Build").size());
        }
        return stem;
    }

    [[nodiscard]] inline std::optional<std::string> module_name_from_source_path(const fs::path& source) {
        std::vector<std::string> parts;
        parts.reserve(16);
        for (const auto& part : source.lexically_normal()) {
            parts.push_back(part.string());
        }
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            if (to_lower_copy(parts[i]) == "source") {
                const std::string module_name = parts[i + 1];
                if (!module_name.empty()) {
                    return module_name;
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline bool has_uproject_marker(const fs::path& project_root) {
        if (project_root.empty() || !fs::exists(project_root)) {
            return false;
        }

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(project_root, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() == ".uproject") {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline bool has_unreal_build_markers(const fs::path& project_root) {
        const fs::path source_root = project_root / "Source";
        if (!fs::exists(source_root)) {
            return false;
        }

        std::error_code ec;
        std::size_t scanned = 0;
        for (const auto& entry : fs::recursive_directory_iterator(source_root, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (++scanned > 4000) {
                break;
            }
            if (is_unreal_build_file(entry.path())) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline bool is_unreal_project_root(const fs::path& project_root) {
        if (project_root.empty() || !fs::exists(project_root)) {
            return false;
        }
        return has_uproject_marker(project_root) || has_unreal_build_markers(project_root);
    }

    [[nodiscard]] inline bool is_unreal_mode_active(const SuggestionContext& context) {
        const auto& unreal = context.options.heuristics.unreal;
        if (unreal.enabled) {
            return true;
        }
        if (!unreal.auto_detect) {
            return false;
        }
        if (!context.project_root.empty() && is_unreal_project_root(context.project_root)) {
            return true;
        }

        std::size_t checked = 0;
        for (const auto& unit : context.trace.units) {
            if (++checked > 8) {
                break;
            }
            const fs::path source = resolve_source_path(unit.source_file);
            if (!source.is_absolute()) {
                continue;
            }
            fs::path current = source.parent_path();
            while (!current.empty() && current.has_parent_path()) {
                if (is_unreal_project_root(current)) {
                    return true;
                }
                const fs::path parent = current.parent_path();
                if (parent == current) {
                    break;
                }
                current = parent;
            }
        }

        return false;
    }

    [[nodiscard]] inline UnrealPCHUsageMode parse_pch_usage_mode(const std::string& mode_name) {
        const std::string normalized = to_lower_copy(trim_copy(mode_name));
        if (normalized == "nopchs") {
            return UnrealPCHUsageMode::NoPCHs;
        }
        if (normalized == "useexplicitorsharedpchs") {
            return UnrealPCHUsageMode::UseExplicitOrSharedPCHs;
        }
        if (normalized == "usesharedpchs") {
            return UnrealPCHUsageMode::UseSharedPCHs;
        }
        if (normalized.empty()) {
            return UnrealPCHUsageMode::Unknown;
        }
        return UnrealPCHUsageMode::Other;
    }

    [[nodiscard]] inline std::optional<UnrealModuleRules> parse_unreal_build_file(const fs::path& build_cs_path) {
        if (!fs::exists(build_cs_path)) {
            return std::nullopt;
        }
        if (!is_unreal_build_file(build_cs_path)) {
            return std::nullopt;
        }

        UnrealModuleRules rules;
        rules.module_name = module_name_from_build_file(build_cs_path);
        rules.build_cs_path = build_cs_path;
        if (rules.module_name.empty()) {
            return std::nullopt;
        }

        std::ifstream input(build_cs_path);
        if (!input) {
            return std::nullopt;
        }

        const std::regex iwyu_regex(
            R"(\bbEnforceIWYU\s*=\s*(true|false)\s*;)",
            std::regex_constants::icase
        );
        const std::regex unity_regex(
            R"(\bbUseUnity\s*=\s*(true|false)\s*;)",
            std::regex_constants::icase
        );
        const std::regex pch_regex(
            R"(\bPCHUsage\s*=\s*PCHUsageMode\.(\w+)\s*;)",
            std::regex_constants::icase
        );

        std::string line;
        std::size_t line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            const std::string sanitized = strip_line_comment(line);
            std::smatch match;

            if (!rules.enforce_iwyu.has_value() &&
                std::regex_search(sanitized, match, iwyu_regex)) {
                const std::string value = to_lower_copy(match[1].str());
                rules.enforce_iwyu = (value == "true");
                rules.enforce_iwyu_line = line_number;
            }

            if (!rules.use_unity.has_value() &&
                std::regex_search(sanitized, match, unity_regex)) {
                const std::string value = to_lower_copy(match[1].str());
                rules.use_unity = (value == "true");
                rules.use_unity_line = line_number;
            }

            if (rules.pch_usage == UnrealPCHUsageMode::Unknown &&
                std::regex_search(sanitized, match, pch_regex)) {
                rules.pch_usage = parse_pch_usage_mode(match[1].str());
                rules.pch_usage_line = line_number;
            }
        }

        return rules;
    }

    [[nodiscard]] inline std::vector<UnrealModuleRules> discover_unreal_module_rules(const fs::path& project_root) {
        std::vector<UnrealModuleRules> rules;
        if (project_root.empty() || !fs::exists(project_root)) {
            return rules;
        }

        std::unordered_set<std::string> seen_build_files;
        std::error_code ec;
        std::size_t scanned = 0;
        for (const auto& entry : fs::recursive_directory_iterator(project_root, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (++scanned > 8000) {
                break;
            }
            const fs::path path = entry.path();
            if (!is_unreal_build_file(path)) {
                continue;
            }
            const std::string normalized_path = path.lexically_normal().generic_string();
            if (!seen_build_files.insert(normalized_path).second) {
                continue;
            }
            if (auto parsed = parse_unreal_build_file(path)) {
                rules.push_back(std::move(*parsed));
            }
        }

        std::ranges::sort(
            rules,
            [](const UnrealModuleRules& lhs, const UnrealModuleRules& rhs) {
                if (lhs.module_name != rhs.module_name) {
                    return lhs.module_name < rhs.module_name;
                }
                return lhs.build_cs_path < rhs.build_cs_path;
            }
        );
        return rules;
    }

    [[nodiscard]] inline std::unordered_map<std::string, UnrealModuleStats> collect_unreal_module_stats(
        const SuggestionContext& context
    ) {
        std::unordered_map<std::string, UnrealModuleStats> stats_by_module;
        for (const auto& unit : context.trace.units) {
            const fs::path source = resolve_source_path(unit.source_file).lexically_normal();
            const auto module_name = module_name_from_source_path(source);
            if (!module_name.has_value()) {
                continue;
            }
            const std::string key = to_lower_copy(*module_name);
            auto& stats = stats_by_module[key];
            ++stats.source_files;
            stats.total_compile_time += unit.metrics.total_time;
            for (const auto& include : unit.includes) {
                stats.include_parse_time += include.parse_time;
            }
        }
        return stats_by_module;
    }

    [[nodiscard]] inline std::vector<UnrealModuleContext> collect_unreal_module_context(
        const SuggestionContext& context
    ) {
        std::vector<UnrealModuleContext> module_contexts;
        if (context.project_root.empty()) {
            return module_contexts;
        }

        const auto rules = discover_unreal_module_rules(context.project_root);
        if (rules.empty()) {
            return module_contexts;
        }

        auto stats_by_module = collect_unreal_module_stats(context);
        module_contexts.reserve(rules.size());
        for (const auto& module_rules : rules) {
            UnrealModuleContext ctx;
            ctx.rules = module_rules;
            if (const auto it = stats_by_module.find(to_lower_copy(module_rules.module_name));
                it != stats_by_module.end()) {
                ctx.stats = it->second;
            }
            module_contexts.push_back(std::move(ctx));
        }

        return module_contexts;
    }
}
