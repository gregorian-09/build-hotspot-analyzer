//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/forward_decl_suggester.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>

#include "bha/heuristics/config.hpp"

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

        Priority calculate_priority(const Duration parse_time, const std::size_t includer_count,
                                    const heuristics::ForwardDeclConfig& config
        ) {
            const auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(parse_time);

            if (parse_ms > std::chrono::milliseconds(500) && includer_count >= 10) {
                return Priority::Critical;
            }
            if (parse_ms > std::chrono::milliseconds(200) && includer_count >= 5) {
                return Priority::High;
            }
            if (parse_ms > config.min_parse_time) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        /**
         * Generates the "before" code showing current include pattern.
         */
        std::string generate_before_code(
            const fs::path& header_path,
            const fs::path& includer_path
        ) {
            std::ostringstream oss;
            oss << "// " << includer_path.filename().string() << "\n";
            oss << "#pragma once\n\n";
            oss << "#include \"" << header_path.filename().string() << "\"  // Full include\n";
            oss << "#include <string>\n\n";
            oss << "class Consumer {\n";
            oss << "    " << extract_class_name(header_path) << "* ptr;  // Only pointer used\n";
            oss << "    void process(" << extract_class_name(header_path) << "& ref);  // Only reference\n";
            oss << "};";
            return oss.str();
        }

        /**
         * Generates the "after" code showing forward declaration pattern.
         */
        std::string generate_after_code(
            const fs::path& header_path,
            const fs::path& includer_path
        ) {
            const std::string class_name = extract_class_name(header_path);

            std::ostringstream oss;
            oss << "// " << includer_path.filename().string() << " (header)\n";
            oss << "#pragma once\n\n";
            oss << "// Forward declaration instead of full include\n";
            oss << "class " << class_name << ";\n\n";
            oss << "#include <string>\n\n";
            oss << "class Consumer {\n";
            oss << "    " << class_name << "* ptr;  // OK: pointer to incomplete type\n";
            oss << "    void process(" << class_name << "& ref);  // OK: reference to incomplete\n";
            oss << "};\n\n";

            const std::string cpp_name = includer_path.stem().string() + ".cpp";
            oss << "// " << cpp_name << " (implementation)\n";
            oss << "#include \"" << includer_path.filename().string() << "\"\n";
            oss << "#include \"" << header_path.filename().string() << "\"  // Full include here\n\n";
            oss << "void Consumer::process(" << class_name << "& ref) {\n";
            oss << "    // Implementation uses full type\n";
            oss << "}";

            return oss.str();
        }

    }  // namespace

    Result<SuggestionResult, Error> ForwardDeclSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& deps = context.analysis.dependencies;
        const auto& config = context.options.heuristics.forward_decl;

        std::unordered_set<std::string> processed;
        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& header : deps.headers) {
            ++analyzed;

            if (!is_header_file(header.path)) {
                ++skipped;
                continue;
            }

            if (header.total_parse_time < config.min_parse_time) {
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
                const fs::path& includer_path(includer);
                if (!is_header_file(includer_path)) {
                    continue;
                }

                Suggestion suggestion;
                suggestion.id = "fwd-" + header.path.filename().string() +
                                "-in-" + includer_path.filename().string();
                suggestion.type = SuggestionType::ForwardDeclaration;
                suggestion.priority = calculate_priority(
                    header.total_parse_time,
                    header.inclusion_count,
                    config
                );
                suggestion.confidence = 0.65;

                std::ostringstream title;
                title << "Use forward declaration for '"
                      << header.path.filename().string()
                      << "' in " << includer_path.filename().string();
                suggestion.title = title.str();

                auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    header.total_parse_time).count();

                std::ostringstream desc;
                desc << "Header '" << header.path.filename().string()
                     << "' takes " << parse_ms << "ms to parse and is included in "
                     << includer_path.filename().string() << ". If only pointers or "
                     << "references to types from this header are used, replace the "
                     << "#include with a forward declaration to reduce compilation dependencies.";
                suggestion.description = desc.str();

                suggestion.rationale =
                    "Forward declarations break include chains, reducing recompilation when "
                    "headers change. The compiler only needs the full type definition when "
                    "it needs to know the size or layout of a type (value members, inheritance, "
                    "calling methods). Pointers and references work with incomplete types.";

                Duration savings_per_file = header.total_parse_time / header.inclusion_count;
                suggestion.estimated_savings = savings_per_file;

                if (context.trace.total_time.count() > 0) {
                    suggestion.estimated_savings_percent =
                        100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                        static_cast<double>(context.trace.total_time.count());
                }

                suggestion.target_file.path = includer_path;
                suggestion.target_file.action = FileAction::Modify;
                suggestion.target_file.note = "Replace #include with forward declaration";

                // Generate detailed before/after code examples
                suggestion.before_code.file = includer_path;
                suggestion.before_code.code = generate_before_code(header.path, includer_path);

                suggestion.after_code.file = includer_path;
                suggestion.after_code.code = generate_after_code(header.path, includer_path);

                suggestion.implementation_steps = {
                    "1. Check if the header is only used for pointers/references",
                    "2. Replace #include with: class " + extract_class_name(header.path) + ";",
                    "3. Move the #include to the corresponding .cpp file",
                    "4. If there are build errors, restore the #include in the header",
                    "5. Repeat for other headers that can use forward declarations"
                };

                suggestion.impact.total_files_affected = 1;
                suggestion.impact.cumulative_savings = savings_per_file;

                suggestion.caveats = {
                    "Only works when type is used by pointer or reference, not by value",
                    "Cannot forward-declare if you need sizeof(), inheritance, or member access",
                    "Template classes may require full definition for certain uses",
                    "May require moving some code from header to .cpp file"
                };

                suggestion.documentation_link =
                    "https://google.github.io/styleguide/cppguide.html#Forward_Declarations";

                suggestion.verification =
                    "Compile the project after making changes. If compilation fails, "
                    "the type is used in a way that requires the full definition.";
                suggestion.is_safe = false;  // Requires manual verification

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