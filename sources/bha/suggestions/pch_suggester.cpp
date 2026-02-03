//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/pch_suggester.hpp"

#include <algorithm>
#include <sstream>

namespace bha::suggestions
{
    namespace {

        /**
         * Finds the repository root by looking for common markers.
         * Strips non-local paths down to a reasonable project root.
         */
        fs::path find_repository_root(const fs::path& path) {
            fs::path current = path;
            if (fs::exists(current)) {
                current = current.parent_path();
            } else {
                // For non-local paths, return empty to use defaults
                if (const std::string path_str = path.string(); !path_str.empty() && (path_str[0] == '/' ||
                    (path_str.length() > 2 && path_str[1] == ':'))) {
                    return {};
                }
                return path.parent_path();
            }

            // Walk up looking for project markers
            while (!current.empty() && current.has_parent_path() &&
                   current != current.parent_path()) {
                if (fs::exists(current / "CMakeLists.txt") ||
                    fs::exists(current / "meson.build") ||
                    fs::exists(current / ".git")) {
                    return current;
                }
                current = current.parent_path();
            }

            return path.parent_path();
        }

        Priority calculate_priority(
            const analyzers::DependencyAnalysisResult::HeaderInfo& header,
            const Duration total_build_time,
            const heuristics::PCHConfig& config
        ) {
            double time_ratio = 0.0;
            if (total_build_time.count() > 0) {
                time_ratio = static_cast<double>(header.total_parse_time.count()) /
                             static_cast<double>(total_build_time.count());
            }

            if (header.inclusion_count >= config.priority.critical_includes &&
                time_ratio > config.priority.critical_time_ratio) {
                return Priority::Critical;
                }
            if (header.inclusion_count >= config.priority.high_includes &&
                time_ratio > config.priority.high_time_ratio) {
                return Priority::High;
                }
            if (header.inclusion_count >= config.min_include_count) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        /**
         * Estimates savings from adding header to PCH.
         *
         * Model:
         * - Original: N * parse_time
         * - With PCH: 1 * parse_time + N * load_time (load_time ≈ 15% of parse_time)
         * - Savings = (N-1) * parse_time * 0.85
         */
        Duration estimate_pch_savings(
            const Duration total_parse_time,
            const std::size_t inclusion_count
        ) {
            if (inclusion_count <= 1) {
                return Duration::zero();
            }

            const Duration per_unit = total_parse_time / inclusion_count;

            constexpr double load_overhead = 0.15;
            constexpr double effective_savings = 1.0 - load_overhead;

            const auto savings_ns = static_cast<Duration::rep>(
                static_cast<double>(per_unit.count()) *
                static_cast<double>(inclusion_count - 1) *
                effective_savings
            );

            return Duration(savings_ns);
        }

    }  // namespace

    Result<SuggestionResult, Error> PCHSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& deps = context.analysis.dependencies;
        if (deps.headers.empty()) {
            auto end_time = std::chrono::steady_clock::now();
            result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        const auto& pch_config = context.options.heuristics.pch;

        std::vector<fs::path> pch_candidates;
        for (const auto& header : deps.headers) {
            if (header.inclusion_count >= pch_config.min_include_count &&
                header.total_parse_time >= pch_config.min_aggregate_time)
                {
                    pch_candidates.push_back(header.path);
                }
        }

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& header : deps.headers) {
            ++analyzed;

            if (header.inclusion_count < pch_config.min_include_count) {
                ++skipped;
                continue;
            }
            if (header.total_parse_time < pch_config.min_aggregate_time) {
                ++skipped;
                continue;
            }

            std::string filename = header.path.filename().string();
            bool is_std_header = filename.find('.') == std::string::npos ||
                                 filename.find("std") == 0;
            if (is_std_header) {
                ++skipped;
                continue;
            }

            if (!header.is_stable && !header.is_external) {
                ++skipped;
                continue;
            }

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("pch", header.path);
            suggestion.type = SuggestionType::PCHOptimization;
            suggestion.priority = calculate_priority(header, context.trace.total_time, pch_config);
            suggestion.confidence = 0.85;

            std::ostringstream title;
            title << "Add '" << header.path.filename().string() << "' to precompiled header";
            suggestion.title = title.str();

            auto parse_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                header.total_parse_time).count();

            std::ostringstream desc;
            desc << "Header '" << header.path.string() << "' is included in "
                 << header.inclusion_count << " translation units, spending "
                 << parse_time_ms << "ms total on parsing. ";

            if (header.is_external) {
                desc << "This is an external/third-party header (stable).\n\n";
            } else if (header.modification_count > 0) {
                auto days_since_mod = std::chrono::duration_cast<std::chrono::hours>(
                    header.time_since_modification).count() / 24;
                desc << "This header has been modified " << header.modification_count
                     << " times and hasn't changed in " << days_since_mod << " days (stable).\n\n";
            } else {
                desc << "\n\n";
            }

            desc << "**Add to pch.h:**\n```\n";
            if (header.path.string().find('<') == 0 || header.path.string().find('>') != std::string::npos) {
                desc << "#include " << header.path.string() << "\n";
            } else {
                desc << "#include \"" << header.path.string() << "\"\n";
            }
            desc << "```\n\n";

            desc << "Adding it to a precompiled header will parse it once and reuse the cached AST across all translation units.";
            suggestion.description = desc.str();

            std::ostringstream rationale;
            rationale << "Precompiled headers (PCH) store the compiler's internal representation of "
                      << "parsed headers, eliminating redundant parsing across translation units. ";

            if (header.is_external) {
                rationale << "This external header is inherently stable. ";
            } else if (header.modification_count > 0) {
                rationale << "This header is stable (modified only " << header.modification_count
                          << " times historically). ";
            }

            rationale << "Including stable, frequently-used headers in PCH maximizes benefit while "
                      << "minimizing rebuild impact.";
            suggestion.rationale = rationale.str();

            suggestion.estimated_savings = estimate_pch_savings(
                header.total_parse_time,
                header.inclusion_count
            );

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            suggestion.target_file.path = header.path;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Add to precompiled header";

            suggestion.implementation_steps = {
                "1. Create pch.h with stable, frequently-included headers",
                "2. Add #include \"" + header.path.filename().string() + "\" to pch.h",
                "3. Configure build system:",
                "   - CMake: target_precompile_headers(target PRIVATE pch.h)",
                "   - qmake: CONFIG += precompile_header; PRECOMPILED_HEADER = pch.h",
                "4. Optionally remove explicit includes from source files",
                "5. Rebuild and verify compilation times improved"
            };

            suggestion.impact.total_files_affected = header.including_files;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;

            suggestion.caveats = {
                "Only include stable headers that rarely change in PCH",
                "PCH changes trigger full rebuild of dependent files",
                "Large PCH files may increase memory usage during compilation",
                "Ensure all target source files can use the same PCH"
            };

            suggestion.documentation_link =
                "https://cmake.org/cmake/help/latest/command/target_precompile_headers.html";

            suggestion.verification =
                "Run 'time make clean && make' before and after to measure improvement. "
                "Expected improvement: 10-40% reduction in build time.";
            suggestion.is_safe = true;

            // For external/system headers, we can't create pch.h in system directories.
            // Using a generic "pch.h" as a placeholder so the consolidator will handle the actual path.
            fs::path pch_path;
            if (header.is_external) {
                pch_path = "pch.h";
            } else {
                fs::path project_root = find_repository_root(header.path);
                if (project_root.empty()) {
                    project_root = header.path.parent_path();
                }

                pch_path = project_root / "include" / "pch.h";
                if (!fs::exists(pch_path.parent_path())) {
                    pch_path = project_root / "pch.h";
                }
            }

            std::string include_line;
            if (header.path.string().find('<') == 0) {
                include_line = "#include " + header.path.string();
            } else {
                include_line = "#include \"" + header.path.filename().string() + "\"";
            }

            if (fs::exists(pch_path)) {
                std::size_t last_include = find_last_include_line(pch_path);
                suggestion.edits.push_back(make_insert_after_line_edit(pch_path, last_include, include_line));
            } else {
                TextEdit create_pch;
                create_pch.file = pch_path;
                create_pch.start_line = 0;
                create_pch.start_col = 0;
                create_pch.end_line = 0;
                create_pch.end_col = 0;
                create_pch.new_text = "#pragma once\n\n" + include_line + "\n";
                suggestion.edits.push_back(create_pch);

                FileTarget pch_target;
                pch_target.path = pch_path;
                pch_target.action = FileAction::Create;
                pch_target.note = "Create precompiled header file";
                suggestion.secondary_files.push_back(pch_target);
            }

            result.suggestions.push_back(std::move(suggestion));
        }

        result.items_analyzed = analyzed;
        result.items_skipped = skipped;

        std::ranges::sort(result.suggestions,
                          [](const Suggestion& a, const Suggestion& b) {
                              return a.estimated_savings > b.estimated_savings;
                          });

        auto end_time = std::chrono::steady_clock::now();
        result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);

        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_pch_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<PCHSuggester>()
        );
    }
}  // namespace bha::suggestions