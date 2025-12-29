//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/template_suggester.hpp"

#include <algorithm>
#include <sstream>

namespace bha::suggestions
{
    namespace {

        Priority calculate_priority(const analyzers::TemplateAnalysisResult::TemplateStats& tmpl,
                                    const Duration total_build_time) {
            const auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                tmpl.total_time
            ).count();

            double time_ratio = 0.0;
            if (total_build_time.count() > 0) {
                time_ratio = static_cast<double>(tmpl.total_time.count()) /
                             static_cast<double>(total_build_time.count());
            }

            if (time_ms > 5000 && tmpl.instantiation_count >= 50) {
                return Priority::Critical;
            }
            if (time_ms > 1000 && tmpl.instantiation_count >= 20) {
                return Priority::High;
            }
            if (time_ratio > 0.01) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        std::string generate_extern_template(const std::string& name) {
            if (name.find('<') == std::string::npos) {
                return "extern template class " + name + ";";
            }
            return "extern template class " + name + ";";
        }

        std::string generate_explicit_instantiation(const std::string& name) {
            if (name.find('<') == std::string::npos) {
                return "template class " + name + ";";
            }
            return "template class " + name + ";";
        }

    }  // namespace

    Result<SuggestionResult, Error> TemplateSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& templates = context.analysis.templates;

        if (templates.templates.empty()) {
            auto end_time = std::chrono::steady_clock::now();
            result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        constexpr auto min_template_time = std::chrono::milliseconds(50);

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& tmpl : templates.templates) {
            ++analyzed;

            if (constexpr std::size_t min_instantiation_count = 3; tmpl.instantiation_count < min_instantiation_count) {
                ++skipped;
                continue;
            }

            if (tmpl.total_time < min_template_time) {
                ++skipped;
                continue;
            }

            const std::string& template_name = !tmpl.full_signature.empty() ? tmpl.full_signature : tmpl.name;

            // Skip std:: templates as they're usually not worth explicit instantiation
            if (template_name.find("std::") == 0) {
                ++skipped;
                continue;
            }

            // Skip testing:: templates (gtest/gmock internals)
            if (template_name.find("testing::") == 0) {
                ++skipped;
                continue;
            }

            Suggestion suggestion;
            suggestion.id = "template-" + std::to_string(analyzed);
            suggestion.type = SuggestionType::ExplicitTemplate;
            suggestion.priority = calculate_priority(tmpl, context.trace.total_time);
            suggestion.confidence = 0.7;

            // Extract short name for title (just the class/function name)
            std::string short_name = template_name;
            if (auto angle_pos = template_name.find('<'); angle_pos != std::string::npos) {
                if (auto last_colon = template_name.rfind("::", angle_pos); last_colon != std::string::npos) {
                    short_name = template_name.substr(last_colon + 2, angle_pos - last_colon - 2);
                } else {
                    short_name = template_name.substr(0, angle_pos);
                }
            }

            std::ostringstream title;
            title << "Add explicit instantiation for " << short_name;
            suggestion.title = title.str();

            std::ostringstream desc;
            desc << "Template '" << template_name << "' is instantiated "
                 << tmpl.instantiation_count << " times with total time of "
                 << std::chrono::duration_cast<std::chrono::milliseconds>(tmpl.total_time).count()
                 << "ms. Using explicit instantiation eliminates redundant instantiations.";
            suggestion.description = desc.str();

            suggestion.rationale = "Explicit template instantiation forces the compiler to "
                "instantiate a template in a single translation unit, while extern template "
                "prevents duplicate instantiations in other units.";

            Duration savings = tmpl.total_time * (tmpl.instantiation_count - 1) /
                              tmpl.instantiation_count;
            suggestion.estimated_savings = savings;

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            suggestion.target_file.path = "template_instantiations.cpp";
            suggestion.target_file.action = FileAction::Create;
            suggestion.target_file.note = "Create file for explicit instantiations";

            suggestion.before_code.code = "// Implicit instantiation in each TU";

            std::ostringstream after;
            after << "// In template_instantiations.cpp:\n"
                  << generate_explicit_instantiation(template_name) << "\n\n"
                  << "// In header or using files:\n"
                  << generate_extern_template(template_name);
            suggestion.after_code.code = after.str();

            suggestion.implementation_steps = {
                "Create template_instantiations.cpp (or similar)",
                "Add explicit instantiation: " + generate_explicit_instantiation(template_name),
                "Add extern template in header: " + generate_extern_template(template_name),
                "Rebuild and verify link succeeds"
            };

            suggestion.impact.total_files_affected = tmpl.files_using.size();
            suggestion.impact.cumulative_savings = savings;

            suggestion.caveats = {
                "Requires identifying all type arguments used",
                "Must instantiate for each combination of template arguments",
                "Header users must see extern template before implicit use"
            };

            suggestion.verification = "Check that total template time decreases in next trace";
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

    void register_template_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<TemplateSuggester>()
        );
    }
}  // namespace bha::suggestions