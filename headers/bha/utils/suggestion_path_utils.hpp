#pragma once

#include "bha/types.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/suggestions/suggester.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace bha::utils
{
    namespace fs = std::filesystem;

    [[nodiscard]] inline std::optional<fs::path> find_compile_commands_path(
        const fs::path& project_root
    ) {
        if (project_root.empty()) {
            return std::nullopt;
        }

        const std::vector<fs::path> candidates = {
            project_root / "compile_commands.json",
            project_root / "build" / "compile_commands.json",
            project_root / "out" / "build" / "compile_commands.json",
            project_root / "cmake-build-debug" / "compile_commands.json",
            project_root / "cmake-build-release" / "compile_commands.json",
        };

        for (const auto& candidate : candidates) {
            if (fs::exists(candidate)) {
                return candidate;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] inline fs::path resolve_project_root_for_suggestions(
        const std::vector<fs::path>& inputs,
        const BuildTrace& build_trace,
        const analyzers::AnalysisResult& analysis,
        const std::size_t probe_limit = 0
    ) {
        fs::path project_root;

        if (!inputs.empty()) {
            const fs::path input_path = inputs.front();
            const fs::path abs_input = input_path.is_relative() ? fs::absolute(input_path) : input_path;
            project_root = suggestions::find_project_root_from_trace_path(abs_input);
        }

        if (project_root.empty()) {
            std::size_t checked = 0;
            for (const auto& unit : build_trace.units) {
                const auto resolved = suggestions::resolve_source_path(unit.source_file);
                if (!resolved.is_absolute()) {
                    if (probe_limit > 0 && ++checked >= probe_limit) {
                        break;
                    }
                    continue;
                }

                const auto root = suggestions::find_repository_root(resolved);
                if (!root.empty() && suggestions::has_build_system_marker(root)) {
                    project_root = root;
                    break;
                }

                if (probe_limit > 0 && ++checked >= probe_limit) {
                    break;
                }
            }
        }

        if (project_root.empty()) {
            std::size_t checked = 0;
            for (const auto& header : analysis.dependencies.headers) {
                const auto resolved = suggestions::resolve_source_path(header.path);
                if (!resolved.is_absolute()) {
                    if (probe_limit > 0 && ++checked >= probe_limit) {
                        break;
                    }
                    continue;
                }

                const auto root = suggestions::find_repository_root(resolved);
                if (!root.empty() && suggestions::has_build_system_marker(root)) {
                    project_root = root;
                    break;
                }

                if (probe_limit > 0 && ++checked >= probe_limit) {
                    break;
                }
            }
        }

        if (project_root.empty()) {
            project_root = fs::current_path();
        }
        return project_root;
    }
}
