//
// Created by gregorian on 20/10/2025.
//

#include "bha/suggestions/forward_decl_suggester.h"
#include <algorithm>
#include "bha/utils/file_utils.h"
#include <regex>
#include "bha/utils/hash_utils.h"

namespace bha::suggestions {

    core::Result<std::vector<core::Suggestion>> ForwardDeclSuggester::suggest_forward_declarations(
        const std::string& file_path,
        const core::BuildTrace& trace
    ) {
        std::vector<core::Suggestion> suggestions;

        auto opportunities_result = analyze_includes(file_path, trace);
        if (!opportunities_result.is_success()) {
            return core::Result<std::vector<core::Suggestion>>::failure(
                opportunities_result.error()
            );
        }

        for (const auto& opportunities = opportunities_result.value(); const auto& opp : opportunities) {
            if (opp.confidence < 0.6) {
                continue;
            }

            if (!opp.used_by_value && !opp.used_by_pointer && !opp.used_by_reference) {
                continue;
            }

            core::Suggestion suggestion;
            suggestion.id = utils::generate_uuid();
            suggestion.type = core::SuggestionType::FORWARD_DECLARATION;
            suggestion.priority = opp.confidence > 0.8 ? core::Priority::HIGH : core::Priority::MEDIUM;
            suggestion.confidence = opp.confidence;
            suggestion.file_path = file_path;
            suggestion.related_files = {opp.include_file};

            suggestion.title = "Use forward declaration for " + opp.class_name;
            suggestion.description = "The class " + opp.class_name + " from " + opp.include_file +
                                    " is only used via pointer/reference. " +
                                    "Replace #include with forward declaration to reduce compilation time.";

            suggestion.estimated_time_savings_ms = opp.estimated_savings_ms;
            suggestion.is_safe = !opp.used_by_value;

            core::CodeChange change;
            change.file_path = file_path;
            change.type = core::ChangeType::REPLACE;
            change.before = "#include \"" + opp.include_file + "\"";
            change.after = "class " + opp.class_name + ";  // Forward declaration";

            suggestion.suggested_changes.push_back(change);

            suggestion.rationale = std::string("Using a forward declaration instead of including the full header ") +
                                  "eliminates the need to parse and compile the included header, " +
                                  "potentially saving significant compilation time.";

            if (opp.used_by_value) {
                suggestion.caveats.push_back(std::string("Class is used by value - full definition is required. ") +
                                            "Forward declaration may not be safe in all contexts.");
            }

            suggestions.push_back(suggestion);
        }

        return core::Result<std::vector<core::Suggestion>>::success(std::move(suggestions));
    }

    core::Result<std::vector<ForwardDeclOpportunity>> ForwardDeclSuggester::analyze_includes(
        const std::string& file_path,
        const core::BuildTrace& trace
    ) {
        auto file_content_result = utils::read_file(file_path);
        if (!file_content_result) {
            return core::Result<std::vector<ForwardDeclOpportunity>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Cannot read file: " + file_path
            );
        }

        const auto& file_content = *file_content_result;

        auto includes_result = extract_includes(file_path);
        if (!includes_result.is_success()) {
            return core::Result<std::vector<ForwardDeclOpportunity>>::failure(
                includes_result.error()
            );
        }

        std::vector<ForwardDeclOpportunity> opportunities;

        for (const auto& include : includes_result.value()) {
            auto classes_result = extract_classes(include);
            if (!classes_result.is_success()) {
                continue;
            }

            for (const auto& class_name : classes_result.value()) {
                if (is_full_definition_needed(file_content, class_name)) {
                    continue;
                }

                ForwardDeclOpportunity opp;
                opp.class_name = class_name;
                opp.include_file = include;
                opp.current_location = file_path;
                opp.used_by_pointer = is_class_used_by_pointer(file_content, class_name);
                opp.used_by_reference = is_class_used_by_reference(file_content, class_name);
                opp.used_by_value = is_class_used_by_value(file_content, class_name);
                opp.usage_count = count_usage(file_content, class_name);

                if (opp.used_by_pointer || opp.used_by_reference) {
                    opp.confidence = 0.85;
                    if (opp.used_by_value) {
                        opp.confidence = 0.5;
                    }
                } else {
                    opp.confidence = 0.9;
                }

                if (auto savings_result = estimate_time_savings(include, trace); savings_result.is_success()) {
                    opp.estimated_savings_ms = savings_result.value();
                }

                opportunities.push_back(opp);
            }
        }

        std::ranges::sort(opportunities.begin(), opportunities.end(),
                  [](const ForwardDeclOpportunity& a, const ForwardDeclOpportunity& b) {
                      return a.confidence * a.estimated_savings_ms >
                             b.confidence * b.estimated_savings_ms;
                  });

        return core::Result<std::vector<ForwardDeclOpportunity>>::success(std::move(opportunities));
    }

    core::Result<std::vector<std::string>> ForwardDeclSuggester::extract_classes(
        const std::string& file_path
    ) {
        const auto content_result = utils::read_file(file_path);
        if (!content_result) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Cannot read file: " + file_path
            );
        }

        std::vector<std::string> classes;
        const std::regex class_regex(R"(\b(?:class|struct)\s+(\w+))");

        std::smatch match;
        const std::string& content = *content_result;
        auto search_start(content.cbegin());

        while (std::regex_search(search_start, content.cend(), match, class_regex)) {
            classes.push_back(match[1].str());
            search_start = match.suffix().first;
        }

        std::ranges::sort(classes);
        classes.erase(std::ranges::unique(classes).begin(), classes.end());

        return core::Result<std::vector<std::string>>::success(std::move(classes));
    }

    core::Result<std::vector<std::string>> ForwardDeclSuggester::extract_includes(
        const std::string& file_path
    ) {
        const auto content_result = utils::read_file(file_path);
        if (!content_result) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Cannot read file: " + file_path
            );
        }

        std::vector<std::string> includes;
        const std::regex include_regex(R"(#\s*include\s*[<"]([^>"]+)[>"])");

        std::smatch match;
        const std::string& content = *content_result;
        auto search_start(content.cbegin());

        while (std::regex_search(search_start, content.cend(), match, include_regex)) {
            includes.push_back(match[1].str());
            search_start = match.suffix().first;
        }

        std::ranges::sort(includes);
        includes.erase(std::ranges::unique(includes).begin(), includes.end());

        return core::Result<std::vector<std::string>>::success(std::move(includes));
    }

    double ForwardDeclSuggester::calculate_confidence(
        const ForwardDeclOpportunity& opportunity
    ) {
        return opportunity.confidence;
    }

    core::Result<double> ForwardDeclSuggester::estimate_time_savings(
        const std::string& include_file,
        const core::BuildTrace& trace
    ) {
        for (const auto& unit : trace.compilation_units) {
            if (unit.file_path == include_file) {
                return core::Result<double>::success(unit.preprocessing_time_ms * 0.8);
            }
        }

        return core::Result<double>::success(50.0);
    }

    bool ForwardDeclSuggester::is_class_used_by_pointer(
        const std::string& file_content,
        const std::string& class_name
    )
    {
        const std::regex pattern(class_name + R"(\s*\*|\*\s*)" + class_name);
        return std::regex_search(file_content, pattern);
    }

    bool ForwardDeclSuggester::is_class_used_by_reference(
        const std::string& file_content,
        const std::string& class_name
    )
    {
        const std::regex pattern(class_name + R"(\s*&|&\s*)" + class_name);
        return std::regex_search(file_content, pattern);
    }

    bool ForwardDeclSuggester::is_class_used_by_value(
        const std::string& file_content,
        const std::string& class_name
    )
    {
        const std::regex pattern(R"(\b)" + class_name + R"(\s+\w+\s*[;,\)])");
        return std::regex_search(file_content, pattern);
    }

    bool ForwardDeclSuggester::is_full_definition_needed(
        const std::string& file_content,
        const std::string& class_name
    )
    {
        if (const std::regex sizeof_pattern(R"(sizeof\s*\(\s*)" + class_name + R"(\s*\))"); std::regex_search(file_content, sizeof_pattern)) {
            return true;
        }

        if (const std::regex member_access_pattern(class_name + R"(\.\w+)"); std::regex_search(file_content, member_access_pattern)) {
            return true;
        }

        return false;
    }

    int ForwardDeclSuggester::count_usage(
        const std::string& file_content,
        const std::string& class_name
    )
    {
        int count = 0;
        const std::regex pattern(R"(\b)" + class_name + R"(\b)");

        std::smatch match;
        auto search_start(file_content.cbegin());

        while (std::regex_search(search_start, file_content.cend(), match, pattern)) {
            count++;
            search_start = match.suffix().first;
        }

        return count;
    }

} // namespace bha::suggestions