//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/pch_suggester.hpp"

#include <algorithm>
#include <sstream>

namespace bha::suggestions
{
    namespace {

        std::string generate_suggestion_id(const fs::path& header) {
            std::ostringstream oss;
            oss << "pch-" << header.filename().string();
            return oss.str();
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
         * Generates the "before" code showing current scattered includes.
         */
        std::string generate_before_code(
            const fs::path& header_path,
            const std::vector<fs::path>& including_files
        ) {
            std::ostringstream oss;
            oss << "// Currently included separately in each source file:\n";
            oss << "// ";

            // Show up to 3 example files
            std::size_t count = 0;
            for (const auto& file : including_files) {
                if (count >= 3) {
                    oss << "... and " << (including_files.size() - 3) << " more files";
                    break;
                }
                if (count > 0) oss << ", ";
                oss << file.filename().string();
                ++count;
            }
            oss << "\n\n";

            oss << "// source.cpp\n";
            oss << "#include \"" << header_path.filename().string() << "\"  // Parsed every time\n";
            oss << "#include <vector>\n";
            oss << "#include <string>\n";
            oss << "// ... other includes\n";

            return oss.str();
        }

        /**
         * Generates the "after" code showing PCH configuration.
         *
         * References:
         * - Qt PCH: https://doc.qt.io/qt-6/qmake-precompiledheaders.html
         * - CMake: target_precompile_headers()
         */
        std::string generate_after_code(
            const fs::path& header_path,
            const std::vector<fs::path>& other_pch_candidates
        ) {
            std::ostringstream oss;

            oss << "// pch.h - Precompiled header (stable headers only)\n";
            oss << "#pragma once\n\n";
            oss << "// Standard library headers\n";
            oss << "#include <vector>\n";
            oss << "#include <string>\n";
            oss << "#include <memory>\n";
            oss << "#include <algorithm>\n\n";
            oss << "// Frequently included project headers\n";
            oss << "#include \"" << header_path.filename().string() << "\"\n";

            std::size_t added = 0;
            for (const auto& candidate : other_pch_candidates) {
                if (candidate != header_path && added < 2) {
                    oss << "#include \"" << candidate.filename().string() << "\"\n";
                    ++added;
                }
            }

            oss << "\n// CMakeLists.txt configuration:\n";
            oss << "// target_precompile_headers(mylib PRIVATE pch.h)\n";
            oss << "\n// qmake (.pro) configuration:\n";
            oss << "// CONFIG += precompile_header\n";
            oss << "// PRECOMPILED_HEADER = pch.h\n";

            return oss.str();
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

            // Parse time per inclusion
            const Duration per_unit = total_parse_time / inclusion_count;

            // PCH load overhead is typically 10-20% of parse time
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

        // Collect PCH candidates for showing in "after" code
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

            // Skip if below thresholds
            if (header.inclusion_count < pch_config.min_include_count) {
                ++skipped;
                continue;
            }
            if (header.total_parse_time < pch_config.min_aggregate_time) {
                ++skipped;
                continue;
            }

            // Skip standard library headers (they should already be in PCH)
            std::string filename = header.path.filename().string();
            bool is_std_header = filename.find('.') == std::string::npos ||
                                 filename.find("std") == 0;
            if (is_std_header) {
                ++skipped;
                continue;
            }

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id(header.path);
            suggestion.type = SuggestionType::PCHOptimization;
            suggestion.priority = calculate_priority(header, context.trace.total_time, pch_config);
            suggestion.confidence = 0.85;

            std::ostringstream title;
            title << "Add '" << header.path.filename().string() << "' to precompiled header";
            suggestion.title = title.str();

            auto parse_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                header.total_parse_time).count();

            std::ostringstream desc;
            desc << "Header '" << header.path.filename().string() << "' is included in "
                 << header.inclusion_count << " translation units, spending "
                 << parse_time_ms << "ms total on parsing. "
                 << "Adding it to a precompiled header will parse it once and reuse the cached AST.";
            suggestion.description = desc.str();

            suggestion.rationale =
                "Precompiled headers (PCH) store the compiler's internal representation of "
                "parsed headers, eliminating redundant parsing across translation units. "
                "This is most effective for stable headers that rarely change and are "
                "included in many source files.";

            suggestion.estimated_savings = estimate_pch_savings(
                header.total_parse_time,
                header.inclusion_count
            );

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            suggestion.target_file.path = "pch.h";
            suggestion.target_file.action = FileAction::Create;
            suggestion.target_file.note = "Create or modify precompiled header";

            suggestion.before_code.file = "Current state (multiple source files)";
            suggestion.before_code.code = generate_before_code(
                header.path,
                header.included_by  // Use including files as examples
            );

            suggestion.after_code.file = "With precompiled header";
            suggestion.after_code.code = generate_after_code(header.path, pch_candidates);

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