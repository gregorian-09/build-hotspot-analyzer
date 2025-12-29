//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/forward_decl_suggester.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {

        bool is_header_file(const fs::path& path) {
            const std::string ext = path.extension().string();
            return ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".H";
        }

        std::string extract_class_name(const fs::path& header) {
            const std::string stem = header.stem().string();
            std::string result;
            bool capitalize_next = true;

            for (const char c : stem) {
                if (c == '_' || c == '-') {
                    capitalize_next = true;
                    continue;
                }
                if (capitalize_next && std::isalpha(c)) {
                    result += static_cast<char>(std::toupper(c));
                    capitalize_next = false;
                } else {
                    result += c;
                }
            }

            return result;
        }

        std::string generate_forward_decl(const fs::path& header) {
            const std::string class_name = extract_class_name(header);
            return "class " + class_name + ";";
        }

        Priority calculate_priority(const Duration parse_time, const std::size_t includer_count) {
            const auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(parse_time).count();

            if (parse_ms > 500 && includer_count >= 10) {
                return Priority::Critical;
            }
            if (parse_ms > 200 && includer_count >= 5) {
                return Priority::High;
            }
            if (parse_ms > 50) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

    }  // namespace

    Result<SuggestionResult, Error> ForwardDeclSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& deps = context.analysis.dependencies;

        std::unordered_set<std::string> processed;
        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        constexpr auto min_parse_time = std::chrono::milliseconds(20);

        for (const auto& header : deps.headers) {
            ++analyzed;

            if (!is_header_file(header.path)) {
                ++skipped;
                continue;
            }

            if (header.total_parse_time < min_parse_time) {
                ++skipped;
                continue;
            }

            if (header.included_by.empty()) {
                ++skipped;
                continue;
            }

            std::string header_key = header.path.string();
            if (processed.contains(header_key)) {
                ++skipped;
                continue;
            }
            processed.insert(header_key);

            for (const auto& includer : header.included_by) {
                if (!is_header_file(fs::path(includer))) {
                    continue;
                }

                Suggestion suggestion;
                suggestion.id = "fwd-" + header.path.filename().string() +
                                "-in-" + fs::path(includer).filename().string();
                suggestion.type = SuggestionType::ForwardDeclaration;
                suggestion.priority = calculate_priority(header.total_parse_time,
                                                          header.inclusion_count);
                suggestion.confidence = 0.6;

                std::ostringstream title;
                title << "Use forward declaration for "
                      << header.path.filename().string()
                      << " in " << fs::path(includer).filename().string();
                suggestion.title = title.str();

                std::ostringstream desc;
                desc << "Consider replacing #include \""
                     << header.path.string() << "\" with a forward declaration "
                     << "in header file " << includer << ". This reduces compilation "
                     << "dependencies when only pointers/references are used.";
                suggestion.description = desc.str();

                suggestion.rationale = "Forward declarations break include chains, "
                    "reducing recompilation when headers change. Use when types are "
                    "only used by pointer/reference, not by value.";

                Duration savings_per_file = header.total_parse_time / header.inclusion_count;
                suggestion.estimated_savings = savings_per_file;

                if (context.trace.total_time.count() > 0) {
                    suggestion.estimated_savings_percent =
                        100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                        static_cast<double>(context.trace.total_time.count());
                }

                suggestion.target_file.path = fs::path(includer);
                suggestion.target_file.action = FileAction::Modify;
                suggestion.target_file.note = "Replace include with forward declaration";

                suggestion.before_code.file = fs::path(includer);
                suggestion.before_code.code = "#include \"" + header.path.string() + "\"";

                std::string fwd_decl = generate_forward_decl(header.path);
                suggestion.after_code.file = fs::path(includer);
                suggestion.after_code.code = fwd_decl;

                suggestion.implementation_steps = {
                    "Replace #include with forward declaration",
                    "Move #include to .cpp file if needed",
                    "Use pointers/references instead of values",
                    "Verify compilation succeeds"
                };

                suggestion.impact.total_files_affected = 1;
                suggestion.impact.cumulative_savings = savings_per_file;

                suggestion.caveats = {
                    "Only works when type is used by pointer/reference",
                    "May require moving implementation to .cpp",
                    "Cannot use with inline functions needing full type",
                    "Cannot use with inheritance or member values"
                };

                suggestion.verification = "Compile the modified header to verify correctness";
                suggestion.is_safe = false;

                result.suggestions.push_back(std::move(suggestion));
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

    void register_forward_decl_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<ForwardDeclSuggester>()
        );
    }
}  // namespace bha::suggestions