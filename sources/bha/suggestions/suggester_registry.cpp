//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/suggester.hpp"
#include "bha/suggestions/consolidator.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bha::suggestions
{
    namespace fs = std::filesystem;

    SuggesterRegistry& SuggesterRegistry::instance() {
        static SuggesterRegistry instance;
        return instance;
    }

    void SuggesterRegistry::register_suggester(std::unique_ptr<ISuggester> suggester) {
        if (suggester) {
            suggesters_.push_back(std::move(suggester));
        }
    }

    const ISuggester* SuggesterRegistry::find(const std::string_view name) const {
        for (const auto& suggester : suggesters_) {
            if (suggester->name() == name) {
                return suggester.get();
            }
        }
        return nullptr;
    }

    Result<std::vector<Suggestion>, Error> generate_all_suggestions(
        const BuildTrace& trace,
        const analyzers::AnalysisResult& analysis,
        const SuggesterOptions& options,
        const fs::path& project_root
    ) {
        std::vector<Suggestion> all_suggestions;

        std::atomic<bool> cancelled{false};
        SuggestionContext context{trace, analysis, options, project_root};
        const ProjectLanguageProfile project_languages = summarize_project_language_profile(trace);
        context.cancelled = &cancelled;
        if (options.restrict_to_trace) {
            std::unordered_set<std::string> targets;
            targets.reserve(analysis.files.size() * 2 + 64);

            auto normalize = [&](const fs::path& path) {
                fs::path resolved = path;
                if (resolved.is_relative()) {
                    if (!project_root.empty()) {
                        resolved = project_root / resolved;
                    } else {
                        std::error_code ec;
                        resolved = fs::absolute(resolved, ec);
                    }
                }
                resolved = resolved.lexically_normal();
                if (resolved.parent_path().empty()) {
                    return resolved.filename().string();
                }
                return resolved.generic_string();
            };

            std::vector<std::string> queue;
            queue.reserve(analysis.files.size());
            for (const auto& file : analysis.files) {
                const std::string key = normalize(file.file);
                if (targets.insert(key).second) {
                    queue.push_back(std::move(key));
                }
            }

            std::unordered_map<std::string, std::vector<fs::path>> included_by_map;
            included_by_map.reserve(analysis.dependencies.headers.size());
            for (const auto& header : analysis.dependencies.headers) {
                for (const auto& includer : header.included_by) {
                    const std::string key = normalize(includer);
                    included_by_map[key].push_back(header.path);
                }
            }

            std::size_t queue_index = 0;
            while (queue_index < queue.size()) {
                const std::string& includer_key = queue[queue_index++];
                auto it = included_by_map.find(includer_key);
                if (it == included_by_map.end()) {
                    continue;
                }
                for (const auto& header_path : it->second) {
                    const std::string header_key = normalize(header_path);
                    if (targets.insert(header_key).second) {
                        queue.push_back(header_key);
                    }
                }
            }

            context.target_files.reserve(targets.size());
            context.target_files_lookup = std::move(targets);
            for (const auto& entry : context.target_files_lookup) {
                context.target_files.emplace_back(entry);
            }
        }

        const auto total_start = std::chrono::steady_clock::now();
        const auto total_deadline = options.max_total_time != Duration::zero()
            ? std::optional<std::chrono::steady_clock::time_point>(total_start + options.max_total_time)
            : std::optional<std::chrono::steady_clock::time_point>();

        for (const auto& suggester : SuggesterRegistry::instance().suggesters()) {
            if (!language_support_matches(suggester->policy().language_support, project_languages)) {
                continue;
            }

            if (options.max_total_time != Duration::zero()) {
                const auto total_elapsed = std::chrono::steady_clock::now() - total_start;
                if (total_elapsed >= options.max_total_time) {
                    break;
                }
            }

            if (!options.enabled_types.empty()) {
                bool enabled = false;
                const auto supported_types = suggester->supported_types();
                for (const auto type : options.enabled_types) {
                    if (std::ranges::find(supported_types, type) != supported_types.end()) {
                        enabled = true;
                        break;
                    }
                }
                if (!enabled) {
                    continue;
                }
            }

            const auto suggester_start = std::chrono::steady_clock::now();
            if (options.max_suggester_time != Duration::zero()) {
                auto deadline = suggester_start + options.max_suggester_time;
                if (total_deadline.has_value() && *total_deadline < deadline) {
                    deadline = *total_deadline;
                }
                context.deadline = deadline;
            } else {
                context.deadline = total_deadline;
            }

            auto result = suggester->suggest(context);
            const auto suggester_elapsed = std::chrono::steady_clock::now() - suggester_start;
            if (!result.is_ok()) {
                continue;
            }

            auto result_value = std::move(result.value());
            if (result_value.generation_time == Duration::zero()) {
                result_value.generation_time =
                    std::chrono::duration_cast<Duration>(suggester_elapsed);
            }
            if (options.on_suggester_completed) {
                options.on_suggester_completed(
                    suggester->name(),
                    result_value.generation_time,
                    result_value.suggestions.size()
                );
            }
            if (options.on_suggester_diagnostic) {
                for (const auto& diagnostic : result_value.diagnostics) {
                    options.on_suggester_diagnostic(
                        suggester->name(),
                        diagnostic.code,
                        diagnostic.message
                    );
                }
            }

            if (options.max_suggester_time != Duration::zero() &&
                suggester_elapsed >= options.max_suggester_time) {
                continue;
            }

            for (auto& suggestion : result_value.suggestions) {
                if (options.conservative_abi_sensitive_headers &&
                    suggester->policy().abi_sensitivity == SuggesterAbiSensitivity::HeaderSurface &&
                    suggestion_touches_abi_sensitive_header(suggestion, project_root)) {
                    downgrade_suggestion_to_manual_review(
                        suggestion,
                        "Touches a public or extern \"C\" header surface",
                        "Review and apply this change manually after validating exported or C-ABI headers across supported consumers."
                    );
                }

                if (suggestion.priority > options.min_priority) {
                    continue;
                }
                if (suggestion.confidence < options.min_confidence) {
                    continue;
                }
                if (!suggestion.is_safe &&
                    resolve_application_mode(suggestion) != SuggestionApplicationMode::ExternalRefactor &&
                    !options.include_unsafe) {
                    continue;
                }

                all_suggestions.push_back(std::move(suggestion));

                if (all_suggestions.size() >= options.max_suggestions) {
                    break;
                }
            }

            if (all_suggestions.size() >= options.max_suggestions) {
                break;
            }
        }

        if (options.enable_consolidation) {
            ConsolidationOptions consol_opts;
            consol_opts.enable_consolidation = true;

            const SuggestionConsolidator consolidator(consol_opts);
            all_suggestions = consolidator.consolidate(std::move(all_suggestions));
        }

        std::ranges::sort(all_suggestions,
                          [](const Suggestion& a, const Suggestion& b) {
                              if (a.priority != b.priority) {
                                  return a.priority < b.priority;
                              }
                              return a.estimated_savings > b.estimated_savings;
                          });

        return Result<std::vector<Suggestion>, Error>::success(std::move(all_suggestions));
    }
}  // namespace bha::suggestions
