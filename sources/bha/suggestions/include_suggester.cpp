//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/include_suggester.hpp"

#include <algorithm>
#include <sstream>

namespace bha::suggestions
{
    namespace {

        bool is_expensive_header(const analyzers::DependencyAnalysisResult::HeaderInfo& header) {
            const auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                header.total_parse_time
            ).count();
            return parse_ms > 100;
        }

        Priority calculate_priority(const Duration savings, const std::size_t affected_files) {
            const auto savings_ms = std::chrono::duration_cast<std::chrono::milliseconds>(savings).count();

            if (savings_ms > 1000 && affected_files >= 20) {
                return Priority::Critical;
            }
            if (savings_ms > 500 && affected_files >= 10) {
                return Priority::High;
            }
            if (savings_ms > 100) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        bool looks_removable(const analyzers::DependencyAnalysisResult::HeaderInfo& header) {
            if (const std::string filename = header.path.filename().string(); filename.find("fwd") != std::string::npos ||
                filename.find("forward") != std::string::npos ||
                filename.find("decl") != std::string::npos) {
                return false;
                }

            return header.inclusion_count > header.including_files * 2;
        }

    }  // namespace

    Result<SuggestionResult, Error> IncludeSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& deps = context.analysis.dependencies;
        const auto& files = context.analysis.files;

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& header : deps.headers) {
            ++analyzed;

            if (!is_expensive_header(header)) {
                ++skipped;
                continue;
            }

            if (looks_removable(header)) {
                Suggestion suggestion;
                suggestion.id = "unused-" + header.path.filename().string();
                suggestion.type = SuggestionType::IncludeRemoval;
                suggestion.priority = calculate_priority(header.total_parse_time,
                                                          header.including_files);
                suggestion.confidence = 0.5;

                std::ostringstream title;
                title << "Review includes of " << header.path.filename().string();
                suggestion.title = title.str();

                std::ostringstream desc;
                desc << "Header '" << header.path.string() << "' is included "
                     << header.inclusion_count << " times across "
                     << header.including_files << " files. "
                     << "Some includes may be unnecessary or could be moved to .cpp files.";
                suggestion.description = desc.str();

                suggestion.rationale = "Removing unnecessary includes reduces preprocessing "
                    "time and breaks dependency chains, speeding up incremental builds.";

                suggestion.estimated_savings = header.total_parse_time / 4;

                if (context.trace.total_time.count() > 0) {
                    suggestion.estimated_savings_percent =
                        100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                        static_cast<double>(context.trace.total_time.count());
                }

                suggestion.target_file.path = header.path;
                suggestion.target_file.action = FileAction::Remove;
                suggestion.target_file.note = "Review and potentially remove include";

                suggestion.implementation_steps = {
                    "Run include-what-you-use (IWYU) or similar tool",
                    "Remove includes that are not directly needed",
                    "Move includes from .h to .cpp where possible",
                    "Replace with forward declarations where applicable"
                };

                suggestion.impact.total_files_affected = header.including_files;
                suggestion.impact.cumulative_savings = suggestion.estimated_savings;

                suggestion.caveats = {
                    "Requires manual verification of actual usage",
                    "May break builds if include is transitively required",
                    "Consider using IWYU for accurate analysis"
                };

                suggestion.verification = "Compile all affected files after changes";
                suggestion.is_safe = false;

                result.suggestions.push_back(std::move(suggestion));
            }

            for (const auto& file_result : files) {
                bool is_header = file_result.file.extension() == ".h" ||
                                 file_result.file.extension() == ".hpp";

                if (!is_header) {
                    continue;
                }

                for (const auto& includer : header.included_by) {
                    if (includer == file_result.file) {
                        Suggestion suggestion;
                        suggestion.id = "move-" + header.path.filename().string() +
                                        "-from-" + file_result.file.filename().string();
                        suggestion.type = SuggestionType::MoveToCpp;
                        suggestion.priority = Priority::Medium;
                        suggestion.confidence = 0.4;

                        std::ostringstream title;
                        title << "Move " << header.path.filename().string()
                              << " include to .cpp";
                        suggestion.title = title.str();

                        std::ostringstream desc;
                        desc << "Consider moving #include \""
                             << header.path.string() << "\" from "
                             << file_result.file.filename().string() << " to its .cpp file "
                             << "to reduce header dependencies.";
                        suggestion.description = desc.str();

                        suggestion.rationale = "Moving includes from headers to source "
                            "files reduces compilation dependencies and speeds up "
                            "incremental builds.";

                        suggestion.estimated_savings = header.total_parse_time /
                                                       (header.inclusion_count + 1);

                        suggestion.target_file.path = file_result.file;
                        suggestion.target_file.action = FileAction::Modify;

                        suggestion.implementation_steps = {
                            "Remove include from header file",
                            "Add include to corresponding .cpp file",
                            "Use forward declaration in header if needed"
                        };

                        suggestion.caveats = {
                            "May require adding forward declaration",
                            "Only works if type not used in header inline code"
                        };

                        suggestion.is_safe = false;

                        result.suggestions.push_back(std::move(suggestion));
                        break;
                    }
                }
            }
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

    void register_include_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<IncludeSuggester>()
        );
    }
}  // namespace bha::suggestions