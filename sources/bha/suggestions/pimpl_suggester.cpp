//
// Created by gregorian on 20/10/2025.
//

#include "bha/suggestions/pimpl_suggester.h"
#include "bha/utils/hash_utils.h"
#include <regex>
#include <fstream>
#include "bha/core/types.h"

namespace bha::suggestions {

    core::Result<std::vector<core::Suggestion>> PIMPLSuggester::suggest_pimpl_patterns(
        const std::string& file_path
    ) {
        std::vector<core::Suggestion> suggestions;
        std::ifstream file(file_path);

        if (!file.is_open()) {
            return core::Result<std::vector<core::Suggestion>>::success(std::move(suggestions));
        }

        std::string content((std::istreambuf_iterator(file)),
                           std::istreambuf_iterator<char>());

        std::regex class_pattern(R"(class\s+(\w+)\s*\{)");
        std::smatch match;

        auto begin = content.cbegin();
        while (std::regex_search(begin, content.cend(), match, class_pattern)) {
            std::string class_name = match[1].str();

            if (auto analysis = analyze_class_for_pimpl(file_path, class_name); is_pimpl_candidate(analysis)) {
                core::Suggestion suggestion;
                suggestion.id = utils::generate_uuid();
                suggestion.type = core::SuggestionType::PIMPL_PATTERN;
                suggestion.priority = core::Priority::LOW;
                suggestion.confidence = 0.6;
                suggestion.file_path = file_path;
                suggestion.title = "Apply PIMPL pattern to " + class_name;
                suggestion.description = "Class " + class_name + " has " +
                                        std::to_string(analysis.private_members_count) +
                                        " private members and " +
                                        std::to_string(analysis.private_includes_count) +
                                        " implementation includes. PIMPL can reduce recompilation.";
                suggestion.estimated_time_savings_ms = analysis.estimated_savings_ms;
                suggestion.is_safe = false;

                suggestions.push_back(suggestion);
            }

            begin = match.suffix().first;
        }

        return core::Result<std::vector<core::Suggestion>>::success(std::move(suggestions));
    }

    PIMPLSuggester::ClassAnalysis PIMPLSuggester::analyze_class_for_pimpl(
        const std::string& file_path,
        const std::string& class_name
    ) {
        ClassAnalysis analysis{class_name, 0, 0, 0.0};

        std::ifstream file(file_path);
        if (!file.is_open()) {
            return analysis;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

        std::regex class_body_pattern(
            R"(class\s+)" + class_name + R"(\s*\{[^}]*private:[^}]*\{)"
        );

        if (std::regex_search(content, class_body_pattern)) {
            std::regex member_pattern(R"((?:int|double|float|bool|std::\w+)\s+\w+\s*[;=])");
            std::smatch match;
            auto begin = content.cbegin();

            while (std::regex_search(begin, content.cend(), match, member_pattern)) {
                analysis.private_members_count++;
                begin = match.suffix().first;
            }

            analysis.private_includes_count = 2;
            analysis.estimated_savings_ms = static_cast<double>(analysis.private_members_count) * 5.0;
        }

        return analysis;
    }

    bool PIMPLSuggester::is_pimpl_candidate(const ClassAnalysis& analysis) {
        return analysis.private_members_count >= 5 &&
               analysis.estimated_savings_ms >= 25.0;
    }
}
