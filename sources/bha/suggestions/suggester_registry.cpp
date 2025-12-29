//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/suggester.hpp"

#include <algorithm>

namespace bha::suggestions
{
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
        const SuggesterOptions& options
    ) {
        std::vector<Suggestion> all_suggestions;

        const SuggestionContext context{trace, analysis, options};

        for (const auto& suggester : SuggesterRegistry::instance().suggesters()) {
            if (!options.enabled_types.empty()) {
                bool enabled = false;
                for (const auto type : options.enabled_types) {
                    if (type == suggester->suggestion_type()) {
                        enabled = true;
                        break;
                    }
                }
                if (!enabled) {
                    continue;
                }
            }

            auto result = suggester->suggest(context);
            if (!result.is_ok()) {
                continue;
            }

            for (auto& suggestion : result.value().suggestions) {
                if (suggestion.priority > options.min_priority) {
                    continue;
                }
                if (suggestion.confidence < options.min_confidence) {
                    continue;
                }
                if (!suggestion.is_safe && !options.include_unsafe) {
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