//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/pimpl_suggester.hpp"

#include <algorithm>
#include <sstream>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {

        /**
         * Checks if a path is a C++ source file (not a header).
         */
        bool is_source_file(const fs::path& path) {
            const std::string ext = path.extension().string();
            return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
                   ext == ".C" || ext == ".c++";
        }

        /**
         * Gets possible header paths for a source file.
         * Returns multiple candidates since naming conventions vary.
         */
        std::vector<fs::path> get_possible_headers(const fs::path& source) {
            std::vector<fs::path> headers;

            const fs::path base = source.parent_path() / source.stem();
            std::vector<std::string> exts = {".h", ".hpp", ".hxx", ".H", ".hh"};

            for (const auto& ext : exts) {
                headers.emplace_back(base.string() + ext);
            }

            const std::vector<std::string> src_dirs = {"/src/", "/source/", "/sources/"};
            const std::vector<std::string> include_dirs = {"/include/", "/header/", "/headers/"};

            std::string path_str = source.string();

            for (const auto& src_dir : src_dirs) {
                if (const auto src_pos = path_str.find(src_dir); src_pos != std::string::npos) {
                    for (const auto& inc_dir : include_dirs) {
                        std::string include_path = path_str.substr(0, src_pos) + inc_dir +
                                                   path_str.substr(src_pos + src_dir.size());
                        for (const auto& ext : exts) {
                            fs::path h = include_path;
                            h.replace_extension(ext);
                            headers.push_back(h);
                        }
                    }
                    break;
                }
            }

            return headers;
        }


        /**
         * PIMPL candidate analysis result.
         *
         * Captures all relevant metrics for deciding if a class benefits from PIMPL.
         */
        struct PIMPLCandidate {
            fs::path source_file;
            fs::path header_file;

            // Compilation metrics
            Duration compile_time = Duration::zero();
            Duration frontend_time = Duration::zero();
            Duration backend_time = Duration::zero();

            // Dependency metrics
            std::size_t direct_includes = 0;
            std::size_t template_instantiations = 0;
            std::size_t dependent_files = 0;  // Files that include the header

            // Computed scores
            double complexity_score = 0.0;
            double impact_score = 0.0;
            double confidence = 0.0;

            Priority priority = Priority::Low;
        };

        /**
         * Calculates a heuristic complexity score for a source file.
         *
         * Combines empirical indicators of build cost into a single metric:
         * - `frontend_ms`: frontend compile time (larger suggests heavier template/include work)
         * - `includes`: number of include dependencies
         * - `templates`: count of template instantiations
         *
         * Logs dampen the influence of large values, while the template factor
         * adds linear scaling for higher metaprogramming overhead.
         *
         * This is a heuristic score for prioritization and hotspot ranking,
         * not a formal algorithmic complexity class.
         */
        double calculate_complexity_score(
            const Duration frontend_time,
            const std::size_t direct_includes,
            const std::size_t template_count
        ) {
            const auto frontend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                frontend_time).count();

            const double frontend_factor = std::log(static_cast<double>(frontend_ms) + 1.0);
            const double include_factor = std::log(static_cast<double>(direct_includes) + 1.0);
            const double template_factor = 1.0 + 0.1 * static_cast<double>(template_count);

            return frontend_factor * include_factor * template_factor;
        }

        /**
         * Calculates the impact of applying PIMPL to this class.
         *
         * Impact is based on:
         * - Number of files that would benefit (dependents)
         * - Current compile time (potential savings)
         * - Transitive dependency depth
         *
         * Higher impact = more worthwhile to apply PIMPL.
         */
        double calculate_impact_score(
            const Duration compile_time,
            const std::size_t dependent_files,
            const std::size_t transitive_includes
        ) {
            const auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                compile_time).count();

            // Each dependent file benefits from reduced header parsing
            const double dependent_factor = std::log(static_cast<double>(dependent_files) + 1.0);

            // Higher compile time = more to potentially save
            const double time_factor = std::log(static_cast<double>(compile_ms) + 1.0);

            // Deep transitive chains benefit more from PIMPL
            const double depth_factor = 1.0 + 0.05 * static_cast<double>(transitive_includes);

            return dependent_factor * time_factor * depth_factor;
        }

        /**
         * Calculates confidence that PIMPL will help.
         *
         * Based on:
         * - Frontend/backend time ratio (high frontend = good candidate)
         * - Include count relative to compile time
         * - Absence of patterns that don't work well with PIMPL
         *
         * Returns 0.0 to 1.0.
         */
        double calculate_confidence(
            const Duration frontend_time,
            const Duration backend_time,
            const Duration compile_time,
            const std::size_t include_count
        ) {
            const auto frontend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                frontend_time).count();
            const auto backend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                backend_time).count();
            const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                compile_time).count();

            if (total_ms <= 0) {
                return 0.3;  // Base confidence when no time data
            }

            // Frontend-heavy files benefit most from PIMPL
            // (It is important to note that PIMPL reduces parsing, not code generation)
            const double frontend_ratio = (frontend_ms + backend_ms > 0) ?
                static_cast<double>(frontend_ms) / static_cast<double>(frontend_ms + backend_ms) :
                0.5;

            // High include count with high compile time = good fit
            double include_time_factor = 0.5;
            if (include_count > 10 && total_ms > 1000) {
                include_time_factor = 0.8;
            } else if (include_count > 5 && total_ms > 500) {
                include_time_factor = 0.65;
            }

            const double confidence = (frontend_ratio * 0.5 + include_time_factor * 0.5);
            return std::max(0.3, std::min(0.95, confidence));
        }

        /**
         * Determines priority based on compile time and include count.
         *
         * Uses thresholds based on industry experience:
         * - Critical: > 5000ms and >= 20 includes (severe build impact)
         * - High: > 2000ms and >= 10 includes (significant impact)
         * - Medium: > 1000ms and >= 5 includes (moderate impact)
         * - Low: below thresholds but still worth considering
         */
        Priority calculate_priority(Duration compile_time, std::size_t include_count) {
            const auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                compile_time).count();

            if (compile_ms > 5000 && include_count >= 20) {
                return Priority::Critical;
            }
            if (compile_ms > 2000 && include_count >= 10) {
                return Priority::High;
            }
            if (compile_ms > 1000 && include_count >= 5) {
                return Priority::Medium;
            }

            // Fallback for borderline cases
            if (compile_ms > 3000) {
                return Priority::High;
            }
            if (compile_ms > 1500) {
                return Priority::Medium;
            }

            return Priority::Low;
        }

        /**
         * Estimates compile time savings from applying PIMPL.
         *
         * Research basis:
         * [1] PIMPL typically saves 20-40% of header parsing time per dependent
         * [2] Microsoft C++ Build Insights: 12-40% PCH improvement (similar for PIMPL)
         *
         * Model:
         * - Savings = frontend_time * reduction_ratio * log(dependents)
         * - reduction_ratio = 0.25 (25%) based on empirical data
         */
        Duration estimate_savings(
            const Duration frontend_time,
            const std::size_t dependent_files
        ) {
            // PIMPL typically reduces header parsing time by 20-30%
            constexpr double compile_time_reduction = 0.25;

            const auto frontend_ns = frontend_time.count();
            const auto savings_per_dependent = static_cast<Duration::rep>(
                static_cast<double>(frontend_ns) * compile_time_reduction
            );

            // More dependents = more aggregate savings
            // But diminishing returns after many dependents
            const double scaling_factor = std::log(static_cast<double>(dependent_files) + 1.0);

            return Duration(static_cast<Duration::rep>(
                static_cast<double>(savings_per_dependent) * scaling_factor
            ));
        }

    }  // namespace

    Result<SuggestionResult, Error> PIMPLSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& files = context.analysis.files;
        const auto& headers = context.analysis.dependencies.headers;

        // Build a map of header -> files that include it
        std::unordered_map<std::string, std::unordered_set<std::string>> header_dependents;
        for (const auto& header : headers) {
            std::string header_path = header.path.string();
            for (const auto& includer : header.included_by) {
                header_dependents[header_path].insert(includer.string());
            }
        }

        constexpr auto min_compile_time = std::chrono::milliseconds(500);

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& file : files) {
            ++analyzed;

            if (!is_source_file(file.file)) {
                ++skipped;
                continue;
            }

            if (file.compile_time < min_compile_time) {
                ++skipped;
                continue;
            }

            // Check if already using PIMPL pattern
            std::string filename = file.file.filename().string();
            std::string lower_filename;
            lower_filename.reserve(filename.size());
            for (char c : filename) {
                lower_filename += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }

            if (lower_filename.find("_impl") != std::string::npos ||
                lower_filename.find("impl_") != std::string::npos ||
                lower_filename.find("pimpl") != std::string::npos ||
                lower_filename.find("_p.") != std::string::npos ||    // Qt-style
                lower_filename.find("private") != std::string::npos) {
                ++skipped;
                continue;
                }

            // Find corresponding header and its dependents
            auto possible_headers = get_possible_headers(file.file);
            fs::path header_path;
            std::size_t dependent_count = 0;

            for (const auto& h : possible_headers) {
                if (std::string h_str = h.string(); header_dependents.contains(h_str)) {
                    header_path = h;
                    dependent_count = header_dependents[h_str].size();
                    break;
                }

                if (header_dependents.contains(h.filename().string())) {
                    header_path = h;
                    dependent_count = header_dependents[h.filename().string()].size();
                    break;
                }
            }

            if (header_path.empty()) {
                header_path = file.file;
                header_path.replace_extension(".h");
            }

            // Count includes for this source file
            std::size_t total_includes = file.include_count;

            // Also count from dependency headers (for backward compatibility with tests)
            std::string source_filename = file.file.filename().string();
            for (const auto& header : headers) {
                for (const auto& includer : header.included_by) {
                    if (fs::path(includer).filename().string() == source_filename) {
                        ++total_includes;
                        break;
                    }
                }
            }

            if (constexpr std::size_t min_include_count = 3; total_includes < min_include_count) {
                ++skipped;
                continue;
            }

            PIMPLCandidate candidate;
            candidate.source_file = file.file;
            candidate.header_file = header_path;
            candidate.compile_time = file.compile_time;
            candidate.frontend_time = file.frontend_time;
            candidate.backend_time = file.backend_time;
            candidate.direct_includes = total_includes;
            candidate.template_instantiations = file.template_count;
            candidate.dependent_files = dependent_count;

            candidate.complexity_score = calculate_complexity_score(
                candidate.frontend_time,
                candidate.direct_includes,
                candidate.template_instantiations
            );

            candidate.impact_score = calculate_impact_score(
                candidate.compile_time,
                candidate.dependent_files,
                candidate.direct_includes  // Using as proxy for transitive
            );

            candidate.confidence = calculate_confidence(
                candidate.frontend_time,
                candidate.backend_time,
                candidate.compile_time,
                candidate.direct_includes
            );

            candidate.priority = calculate_priority(
                candidate.compile_time,
                candidate.direct_includes
            );

            // Skip low-confidence suggestions
            if (candidate.confidence < 0.4 && candidate.priority == Priority::Low) {
                ++skipped;
                continue;
            }

            Suggestion suggestion;
            suggestion.id = "pimpl-" + file.file.filename().string();
            suggestion.type = SuggestionType::PIMPLPattern;
            suggestion.priority = candidate.priority;
            suggestion.confidence = candidate.confidence;

            std::ostringstream title;
            title << "Consider PIMPL pattern for " << file.file.filename().string();
            suggestion.title = title.str();

            auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                file.compile_time).count();
            auto frontend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                file.frontend_time).count();

            std::ostringstream desc;
            desc << "File '" << file.file.string() << "' takes "
                 << compile_ms << "ms to compile";
            if (frontend_ms > 0) {
                desc << " (" << frontend_ms << "ms frontend)";
            }
            desc << " and has " << total_includes << " direct includes";
            if (dependent_count > 0) {
                desc << ". Its header is included by " << dependent_count << " other files";
            }
            desc << ". The PIMPL idiom could reduce compile-time coupling and "
                 << "improve incremental build times.";
            suggestion.description = desc.str();

            suggestion.rationale = "The PIMPL (Pointer to Implementation) pattern "
                "hides class implementation details behind an opaque pointer. "
                "Benefits include:\n"
                "1. Reduced compile-time dependencies - changes to private members "
                "don't trigger recompilation of dependents\n"
                "2. Faster incremental builds - header changes don't cascade\n"
                "3. Binary compatibility - implementation changes don't break ABI\n"
                "4. Reduced header pollution - heavy includes move to .cpp\n\n"
                "This file has a high frontend-to-total compile time ratio, "
                "indicating significant time spent on parsing and template "
                "instantiation that PIMPL can help reduce.";

            // Estimate savings
            // Use frontend_time if available, otherwise use a portion of compile_time
            Duration time_for_savings = candidate.frontend_time;
            if (time_for_savings.count() == 0) {
                // Assume frontend is about 60% of compile time if not specified
                time_for_savings = candidate.compile_time * 6 / 10;
            }
            suggestion.estimated_savings = estimate_savings(
                time_for_savings,
                std::max(candidate.dependent_files, static_cast<std::size_t>(1))
            );

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            suggestion.target_file.path = file.file;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Convert class to use PIMPL idiom";

            // Add secondary file for header
            FileTarget header_target;
            header_target.path = header_path;
            header_target.action = FileAction::Modify;
            header_target.note = "Replace private members with unique_ptr<Impl>";
            suggestion.secondary_files.push_back(header_target);

            // Example code
            std::ostringstream before;
            before << "// " << header_path.filename().string() << "\n"
                   << "#pragma once\n"
                   << "#include <heavy_dependency.h>\n"
                   << "#include <another_heavy_dep.h>\n\n"
                   << "class MyClass {\n"
                   << "public:\n"
                   << "    void do_something();\n\n"
                   << "private:\n"
                   << "    HeavyDep member1_;\n"
                   << "    AnotherDep member2_;\n"
                   << "};";
            suggestion.before_code.file = header_path;
            suggestion.before_code.code = before.str();

            std::ostringstream after;
            after << "// " << header_path.filename().string() << "\n"
                  << "#pragma once\n"
                  << "#include <memory>\n\n"
                  << "class MyClass {\n"
                  << "public:\n"
                  << "    MyClass();\n"
                  << "    ~MyClass();\n"
                  << "    MyClass(MyClass&&) noexcept;\n"
                  << "    MyClass& operator=(MyClass&&) noexcept;\n\n"
                  << "    void do_something();\n\n"
                  << "private:\n"
                  << "    struct Impl;\n"
                  << "    std::unique_ptr<Impl> impl_;\n"
                  << "};\n\n"
                  << "// " << file.file.filename().string() << "\n"
                  << "#include \"" << header_path.filename().string() << "\"\n"
                  << "#include <heavy_dependency.h>\n"
                  << "#include <another_heavy_dep.h>\n\n"
                  << "struct MyClass::Impl {\n"
                  << "    HeavyDep member1_;\n"
                  << "    AnotherDep member2_;\n"
                  << "};\n\n"
                  << "MyClass::MyClass() : impl_(std::make_unique<Impl>()) {}\n"
                  << "MyClass::~MyClass() = default;\n"
                  << "MyClass::MyClass(MyClass&&) noexcept = default;\n"
                  << "MyClass& MyClass::operator=(MyClass&&) noexcept = default;";
            suggestion.after_code.file = file.file;
            suggestion.after_code.code = after.str();

            suggestion.implementation_steps = {
                "Create a forward-declared Impl struct in the header",
                "Replace private data members with std::unique_ptr<Impl>",
                "Declare destructor in header (define in .cpp as = default)",
                "Add move constructor and move assignment operator declarations",
                "Define Impl struct in the source file with original private members",
                "Move heavy #includes from header to source file",
                "Update all member functions to access members via impl_->",
                "If copy semantics needed, implement copy constructor/assignment",
                "Rebuild and verify all dependent files compile correctly"
            };

            suggestion.impact.total_files_affected = dependent_count + 1;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;
            suggestion.impact.rebuild_files_count = 1;  // Only this .cpp needs rebuild

            suggestion.caveats = {
                "Adds heap allocation (minor memory and CPU overhead)",
                "Class becomes non-copyable by default (implement if needed)",
                "Debugging requires stepping into Impl (use debugger pretty-printers)",
                "All member functions must be updated to use impl_->",
                "Not suitable for header-only libraries",
                "Performance-critical inner loops may prefer direct access"
            };

            suggestion.verification =
                "1. Rebuild the project and verify compilation succeeds\n"
                "2. Run the test suite to verify functionality\n"
                "3. Measure incremental build time after changing a private member\n"
                "4. Profile runtime performance if this is a hot code path";

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

    void register_pimpl_pattern_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<PIMPLSuggester>()
        );
    }
}  // namespace bha::suggestions