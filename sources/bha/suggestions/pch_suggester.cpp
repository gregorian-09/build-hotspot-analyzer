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

        Priority calculate_priority(const analyzers::DependencyAnalysisResult::HeaderInfo& header,
                                    const Duration total_build_time) {
            double time_ratio = 0.0;
            if (total_build_time.count() > 0) {
                time_ratio = static_cast<double>(header.total_parse_time.count()) /
                             static_cast<double>(total_build_time.count());
            }

            if (header.inclusion_count >= 50 && time_ratio > 0.05) {
                return Priority::Critical;
            }
            if (header.inclusion_count >= 20 && time_ratio > 0.02) {
                return Priority::High;
            }
            if (header.inclusion_count >= 10) {
                return Priority::Medium;
            }
            return Priority::Low;
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

        constexpr auto min_parse_time = std::chrono::milliseconds(100);

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& header : deps.headers) {
            ++analyzed;

            if (constexpr std::size_t min_inclusion_count = 5; header.inclusion_count < min_inclusion_count) {
                ++skipped;
                continue;
            }
            if (header.total_parse_time < min_parse_time) {
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

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id(header.path);
            suggestion.type = SuggestionType::PCHOptimization;
            suggestion.priority = calculate_priority(header, context.trace.total_time);
            suggestion.confidence = 0.8;

            std::ostringstream title;
            title << "Add " << header.path.filename().string() << " to precompiled header";
            suggestion.title = title.str();

            std::ostringstream desc;
            desc << "Header '" << header.path.string() << "' is included in "
                 << header.inclusion_count << " files with total parse time of "
                 << std::chrono::duration_cast<std::chrono::milliseconds>(header.total_parse_time).count()
                 << "ms. Adding to PCH would parse it only once.";
            suggestion.description = desc.str();

            suggestion.rationale = "Precompiled headers cache the parsed AST, "
                                   "eliminating redundant parsing across translation units.";

            Duration savings_per_unit = header.total_parse_time / header.inclusion_count;
            suggestion.estimated_savings = savings_per_unit * (header.inclusion_count - 1);

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            suggestion.target_file.path = "pch.h";
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Add include to precompiled header";

            suggestion.before_code.file = "source.cpp";
            suggestion.before_code.code = "#include \"" + header.path.string() + "\"";

            suggestion.after_code.file = "pch.h";
            suggestion.after_code.code = "#include \"" + header.path.string() + "\"";

            suggestion.implementation_steps = {
                "Create or modify pch.h",
                "Add #include \"" + header.path.string() + "\"",
                "Configure build system for PCH",
                "Remove explicit includes from source files (optional)"
            };

            suggestion.impact.total_files_affected = header.including_files;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;

            suggestion.caveats = {
                "PCH increases incremental build time when modified",
                "Ensure header is stable and rarely changes",
                "May increase memory usage during compilation"
            };

            suggestion.verification = "Rebuild and compare total compilation time";
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