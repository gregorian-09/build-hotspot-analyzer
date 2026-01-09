//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/header_split_suggester.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {

        /**
         * Checks if a path is a C++ header file.
         */
        bool is_header(const fs::path& path) {
            const std::string ext = path.extension().string();
            return ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".H" ||
                   ext == ".hh" || ext == ".h++";
        }

        /**
         * Header split pattern types.
         *
         * Different split patterns are appropriate for different header types:
         * - ForwardDecl: Create a _fwd.h with forward declarations
         * - TypesAndFwd: Split into types and forward declarations
         * - FunctionalGroups: Split by logical functionality groups
         * - PublicPrivate: Split into public API and internal details
         */
        enum class SplitPattern {
            ForwardDecl,
            TypesAndFwd,
            FunctionalGroups,
            PublicPrivate
        };

        /**
         * Analyzes a header to determine the best split pattern.
         *
         * Based on:
         * - Filename patterns (utils, types, core suggests different splits)
         * - Number of includers (high fanout suggests forward decl pattern)
         * - Parse time distribution (if available)
         */
        SplitPattern determine_split_pattern(
            const fs::path& header_path,
            const std::size_t includer_count
        ) {
            const std::string filename = header_path.filename().string();
            std::string lower_filename;
            lower_filename.reserve(filename.size());
            for (const char c : filename) {
                lower_filename += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }

            // Type-focused headers benefit from types/fwd split
            if (lower_filename.find("type") != std::string::npos ||
                lower_filename.find("struct") != std::string::npos ||
                lower_filename.find("enum") != std::string::npos) {
                return SplitPattern::TypesAndFwd;
                }

            // Utility headers often benefit from functional group splits
            if (lower_filename.find("util") != std::string::npos ||
                lower_filename.find("helper") != std::string::npos ||
                lower_filename.find("common") != std::string::npos) {
                return SplitPattern::FunctionalGroups;
                }

            // High-fanout headers (>20 includers) benefit most from forward decls
            if (includer_count > 20) {
                return SplitPattern::ForwardDecl;
            }

            // Core/main headers often benefit from public/private split
            if (lower_filename.find("core") != std::string::npos ||
                lower_filename.find("main") != std::string::npos ||
                lower_filename.find("api") != std::string::npos) {
                return SplitPattern::PublicPrivate;
                }

            return SplitPattern::ForwardDecl;
        }

        /**
         * Calculates priority for header splitting.
         *
         * Thresholds based on build impact:
         * - Critical: Headers that significantly impact overall build time
         * - High: Headers with high parse time and many includers
         * - Medium: Moderate impact headers
         * - Low: Minor improvements possible
         */
        Priority calculate_priority(const Duration parse_time, const std::size_t includer_count) {
            const auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                parse_time).count();

            // Total impact = parse_time * includer_count (amortized across build)
            const double total_impact_ms = static_cast<double>(parse_ms) *
                                     static_cast<double>(includer_count);

            if (parse_ms > 1000 && includer_count >= 50) {
                return Priority::Critical;
            }
            if (parse_ms > 500 && includer_count >= 20) {
                return Priority::High;
            }
            if ((parse_ms > 200 && includer_count >= 10) || total_impact_ms > 5000) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        /**
         * Calculates confidence that splitting will help.
         *
         * Based on:
         * - Parse time relative to typical headers
         * - Number of includers (more = more likely to benefit)
         * - Header size indicators
         *
         * Returns 0.0 to 1.0.
         */
        double calculate_confidence(
            const Duration parse_time,
            const std::size_t includer_count,
            const std::size_t inclusion_count  // Total inclusions across all TUs
        ) {
            const auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                parse_time).count();

            // Base confidence from parse time (headers over 200ms benefit)
            double time_confidence = 0.0;
            if (parse_ms > 1000) {
                time_confidence = 0.9;
            } else if (parse_ms > 500) {
                time_confidence = 0.75;
            } else if (parse_ms > 200) {
                time_confidence = 0.6;
            } else {
                time_confidence = 0.4;
            }

            // Adjust for includer count (more files = more benefit from split)
            const double includer_confidence = std::min(1.0,
                std::log(static_cast<double>(includer_count) + 1) / std::log(50.0));

            // Repeated inclusions per file indicate potential for subsets
            double repetition_factor = 1.0;
            if (includer_count > 0 && inclusion_count > includer_count) {
                const double avg_inclusions = static_cast<double>(inclusion_count) /
                                        static_cast<double>(includer_count);
                if (avg_inclusions > 2.0) {
                    repetition_factor = 1.1;  // Slight boost for highly reused headers
                }
            }

            const double confidence = (time_confidence * 0.6 + includer_confidence * 0.4)
                                * repetition_factor;

            return std::max(0.3, std::min(0.95, confidence));
        }

        /**
         * Estimates savings from splitting a header.
         *
         * Assumes:
         * - Forward decl headers are ~90% smaller than full headers
         * - Types headers are ~60% smaller
         * - On average, 30-50% of includers only need forward decls
         *
         * Savings = parse_time * includer_fraction * reduction_factor
         */
        Duration estimate_savings(
            const Duration parse_time,
            const std::size_t includer_count,
            const SplitPattern pattern
        ) {
            // Base forward-declaration-only ratio: 30% of includers typically only need fwd decls
            constexpr double fwd_only_ratio = 0.30;
            double reduction_factor = fwd_only_ratio;
            switch (pattern) {
                case SplitPattern::ForwardDecl:
                    // 30-40% of includers might only need forward decls
                    reduction_factor = fwd_only_ratio;
                    break;
                case SplitPattern::TypesAndFwd:
                    // Types+fwd gives more options
                    reduction_factor = 0.25;
                    break;
                case SplitPattern::FunctionalGroups:
                    // Groups allow targeted includes
                    reduction_factor = 0.20;
                    break;
                case SplitPattern::PublicPrivate:
                    // Public/private split helps internal vs external
                    reduction_factor = 0.15;
                    break;
            }

            const auto parse_ns = parse_time.count();
            const double includer_factor = std::log(static_cast<double>(includer_count) + 1); // Log scale for includers (diminishing returns)

            const auto savings_ns = static_cast<Duration::rep>(
                static_cast<double>(parse_ns) * reduction_factor * includer_factor
            );

            return Duration(savings_ns);
        }

        /**
         * Generates split header name based on pattern.
         */
        std::string suggest_split_name(const fs::path& header, const std::string& suffix) {
            const std::string stem = header.stem().string();
            const std::string ext = header.extension().string();
            return stem + "_" + suffix + ext;
        }

        /**
         * Generates implementation steps based on split pattern.
         */
        std::vector<std::string> generate_implementation_steps(
            const fs::path& header_path,
            const SplitPattern pattern
        ) {
            const std::string filename = header_path.filename().string();
            const std::string fwd_header = suggest_split_name(header_path, "fwd");
            const std::string types_header = suggest_split_name(header_path, "types");

            std::vector<std::string> steps;

            switch (pattern) {
            case SplitPattern::ForwardDecl:
                steps = {
                "Identify classes and structs that can be forward-declared",
                "Create " + fwd_header + " with forward declarations",
                "Update " + filename + " to include " + fwd_header,
                "Audit includers: replace #include with forward decl where possible",
                "Run include-what-you-use (IWYU) to validate minimal includes",
                "Verify compilation and run tests"
            };
                break;

            case SplitPattern::TypesAndFwd:
                steps = {
                "Separate type definitions from function declarations",
                "Create " + fwd_header + " with forward declarations",
                "Create " + types_header + " with type definitions",
                "Update " + filename + " to include both split headers",
                "Update includers to use minimal required header",
                "Verify compilation and run tests"
            };
                break;

            case SplitPattern::FunctionalGroups:
                steps = {
                "Identify logical groups of related functions/classes",
                "Create separate headers for each functional group",
                "Move declarations to appropriate group headers",
                "Update " + filename + " to include all group headers",
                "Update includers to use specific group headers",
                "Consider deprecating the umbrella header",
                "Verify compilation and run tests"
            };
                break;

            case SplitPattern::PublicPrivate:
                steps = {
                "Identify public API vs internal implementation details",
                "Create " + suggest_split_name(header_path, "internal") + " for internals",
                "Keep " + filename + " as the public API header",
                "Move internal details to the internal header",
                "Update internal code to use the internal header",
                "Document that " + filename + " is the public interface",
                "Verify compilation and run tests"
            };
                break;
            }

            return steps;
        }

    }  // namespace

    Result<SuggestionResult, Error> HeaderSplitSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& deps = context.analysis.dependencies;

        // Thresholds for considering a split (based on ClangBuildAnalyzer patterns)
        constexpr auto min_parse_time = std::chrono::milliseconds(200);

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& header : deps.headers) {
            ++analyzed;

            if (!is_header(header.path)) {
                ++skipped;
                continue;
            }

            if (header.total_parse_time < min_parse_time) {
                ++skipped;
                continue;
            }

            if (constexpr std::size_t min_includer_count = 5; header.including_files < min_includer_count) {
                ++skipped;
                continue;
            }

            // Check if already split
            std::string filename = header.path.filename().string();
            std::string lower_filename;
            lower_filename.reserve(filename.size());
            for (char c : filename) {
                lower_filename += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }

            bool already_split = lower_filename.find("_fwd") != std::string::npos ||
                                 lower_filename.find("_types") != std::string::npos ||
                                 lower_filename.find("_decl") != std::string::npos ||
                                 lower_filename.find("_impl") != std::string::npos ||
                                 lower_filename.find("_internal") != std::string::npos ||
                                 lower_filename.find("_detail") != std::string::npos;
            if (already_split) {
                ++skipped;
                continue;
            }

            SplitPattern pattern = determine_split_pattern(header.path, header.including_files);

            double confidence = calculate_confidence(
                header.total_parse_time,
                header.including_files,
                header.inclusion_count
            );

            Priority priority = calculate_priority(
                header.total_parse_time,
                header.including_files
            );

            Duration savings = estimate_savings(
                header.total_parse_time,
                header.including_files,
                pattern
            );

            Suggestion suggestion;
            suggestion.id = "split-" + header.path.filename().string();
            suggestion.type = SuggestionType::HeaderSplit;
            suggestion.priority = priority;
            suggestion.confidence = confidence;

            std::ostringstream title;
            title << "Consider splitting " << header.path.filename().string();
            suggestion.title = title.str();

            auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                header.total_parse_time).count();

            std::ostringstream desc;
            desc << "Header '" << header.path.string() << "' takes "
                 << parse_ms << "ms to parse and is included by "
                 << header.including_files << " files";
            if (header.inclusion_count > header.including_files) {
                desc << " (" << header.inclusion_count << " total inclusions)";
            }
            desc << ". Splitting into smaller, focused headers can reduce "
                 << "compile times when files only need a subset of declarations.";
            suggestion.description = desc.str();

            std::ostringstream rationale;
            rationale << "Large, frequently-included headers cause unnecessary "
                      << "parsing overhead. ";

            switch (pattern) {
            case SplitPattern::ForwardDecl:
                rationale << "This header would benefit from a forward declaration "
                          << "header (_fwd.h) since many includers likely only need "
                          << "to reference types without seeing their full definition.";
                break;
            case SplitPattern::TypesAndFwd:
                rationale << "Separating type definitions from forward declarations "
                          << "allows includers to choose the minimal header they need.";
                break;
            case SplitPattern::FunctionalGroups:
                rationale << "This utility-style header contains multiple unrelated "
                          << "groups that could be split into focused headers.";
                break;
            case SplitPattern::PublicPrivate:
                rationale << "Separating public API from internal details prevents "
                          << "external code from depending on implementation.";
                break;
            }
            suggestion.rationale = rationale.str();

            suggestion.estimated_savings = savings;

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            suggestion.target_file.path = header.path;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Split into smaller, focused headers";

            // Example code
            std::string fwd_header_name = suggest_split_name(header.path, "fwd");
            std::string types_header_name = suggest_split_name(header.path, "types");

            std::ostringstream before;
            before << "// " << header.path.filename().string() << "\n"
                   << "#pragma once\n\n"
                   << "// All declarations, types, and implementations in one file\n"
                   << "class MyClass { ... };\n"
                   << "struct MyStruct { ... };\n"
                   << "void my_function();\n";
            suggestion.before_code.file = header.path;
            suggestion.before_code.code = before.str();

            std::ostringstream after;
            after << "// " << fwd_header_name << " - forward declarations only\n"
                  << "#pragma once\n"
                  << "class MyClass;\n"
                  << "struct MyStruct;\n\n"
                  << "// " << types_header_name << " - type definitions\n"
                  << "#pragma once\n"
                  << "#include \"" << fwd_header_name << "\"\n"
                  << "struct MyStruct { ... };\n\n"
                  << "// " << header.path.filename().string() << " - full header\n"
                  << "#pragma once\n"
                  << "#include \"" << types_header_name << "\"\n"
                  << "class MyClass { ... };\n"
                  << "void my_function();";
            suggestion.after_code.file = header.path;
            suggestion.after_code.code = after.str();

            suggestion.implementation_steps = generate_implementation_steps(header.path, pattern);

            suggestion.impact.total_files_affected = header.including_files;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;

            suggestion.caveats = {
                "Requires understanding of symbol dependencies between declarations",
                "May require updating include statements in many files",
                "Forward declarations cannot be used when full type is needed (sizeof, members)",
                "Split headers need to be kept in sync with main header",
                "IDE/tooling support may need reconfiguration"
            };

            suggestion.verification =
                "1. Create split headers incrementally, verifying compilation at each step\n"
                "2. Use include-what-you-use (IWYU) to optimize includes in client code\n"
                "3. Measure compile times before and after to verify improvement\n"
                "4. Run full test suite to ensure no functionality changes";

            suggestion.is_safe = false;

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

    void register_header_split_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<HeaderSplitSuggester>()
        );
    }
}  // namespace bha::suggestions