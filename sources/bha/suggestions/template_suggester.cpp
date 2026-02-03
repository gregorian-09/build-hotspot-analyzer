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
            suggestion.id = generate_suggestion_id("template", analyzed);
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
                 << "ms.\n\n";

            desc << "**Add to template_instantiations.cpp:**\n```\n";
            desc << generate_explicit_instantiation(template_name) << "\n";
            desc << "```\n\n";

            desc << "**Add to header:**\n```\n";
            desc << generate_extern_template(template_name) << "\n";
            desc << "```\n\n";

            desc << "Using explicit instantiation eliminates redundant instantiations.";
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

            std::ostringstream before;
            before << "// Current: Implicit instantiation in each translation unit\n";
            before << "// Template instantiated " << tmpl.instantiation_count << " times across files:\n";
            std::size_t file_count = 0;
            for (const auto& file : tmpl.files_using) {
                if (file_count >= 5) {
                    before << "// ... and " << (tmpl.files_using.size() - 5) << " more files\n";
                    break;
                }
                before << "// - " << file << "\n";
                ++file_count;
            }
            before << "\n// Each file pays the instantiation cost:\n";
            before << "// " << template_name << "\n";
            before << "// Total time wasted: "
                   << std::chrono::duration_cast<std::chrono::milliseconds>(tmpl.total_time).count()
                   << "ms";
            suggestion.before_code.file = "Multiple files";
            suggestion.before_code.code = before.str();

            std::ostringstream after;
            after << "// template_instantiations.cpp - Explicit instantiation\n";
            if (!tmpl.files_using.empty()) {
                fs::path first_file(tmpl.files_using[0]);
                after << "#include \"" << first_file.parent_path().string() << "/templates.h\"\n";
            }
            after << "\n// Explicitly instantiate once:\n"
                  << generate_explicit_instantiation(template_name) << "\n\n"
                  << "// In header file:\n"
                  << "// Prevent implicit instantiation in other TUs:\n"
                  << generate_extern_template(template_name) << "\n\n"
                  << "// Result: Instantiated once, linked " << tmpl.instantiation_count << " times\n"
                  << "// Savings: ~"
                  << std::chrono::duration_cast<std::chrono::milliseconds>(suggestion.estimated_savings).count()
                  << "ms";
            suggestion.after_code.file = "template_instantiations.cpp";
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

            fs::path project_root;
            if (!tmpl.files_using.empty()) {
                project_root = fs::path(tmpl.files_using[0]).parent_path();
                while (project_root.has_parent_path() &&
                       !fs::exists(project_root / "CMakeLists.txt") &&
                       !fs::exists(project_root / "meson.build")) {
                    project_root = project_root.parent_path();
                }
            }

            fs::path inst_file = project_root / "src" / "template_instantiations.cpp";
            if (!fs::exists(inst_file.parent_path())) {
                inst_file = project_root / "template_instantiations.cpp";
            }

            if (fs::exists(inst_file)) {
                std::string inst_content;
                std::ifstream in(inst_file);
                inst_content = std::string((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());

                std::size_t last_line = 0;
                for (std::size_t i = 0; i < inst_content.size(); ++i) {
                    if (inst_content[i] == '\n') ++last_line;
                }

                TextEdit add_inst;
                add_inst.file = inst_file;
                add_inst.start_line = last_line;
                add_inst.start_col = 0;
                add_inst.end_line = last_line;
                add_inst.end_col = 0;
                add_inst.new_text = "\n" + generate_explicit_instantiation(template_name) + "\n";
                suggestion.edits.push_back(add_inst);
            } else {
                std::ostringstream new_file_content;
                new_file_content << "// Explicit template instantiations\n";
                new_file_content << "// Auto-generated by BHA\n\n";
                if (!tmpl.files_using.empty()) {
                    auto header_path = fs::path(tmpl.files_using[0]);
                    header_path.replace_extension(".hpp");
                    if (!fs::exists(header_path)) {
                        header_path.replace_extension(".h");
                    }
                    if (fs::exists(header_path)) {
                        new_file_content << "#include \"" << header_path.filename().string() << "\"\n\n";
                    }
                }
                new_file_content << generate_explicit_instantiation(template_name) << "\n";

                TextEdit create_inst;
                create_inst.file = inst_file;
                create_inst.start_line = 0;
                create_inst.start_col = 0;
                create_inst.end_line = 0;
                create_inst.end_col = 0;
                create_inst.new_text = new_file_content.str();
                suggestion.edits.push_back(create_inst);
            }

            suggestion.target_file.path = inst_file;

            for (const auto& using_file : tmpl.files_using) {
                auto header_path = fs::path(using_file);
                header_path.replace_extension(".hpp");
                if (!fs::exists(header_path)) {
                    header_path.replace_extension(".h");
                }
                if (fs::exists(header_path)) {
                    std::size_t last_include = find_last_include_line(header_path);
                    TextEdit extern_edit = make_insert_after_line_edit(
                        header_path,
                        last_include,
                        generate_extern_template(template_name)
                    );
                    suggestion.edits.push_back(extern_edit);

                    FileTarget header_target;
                    header_target.path = header_path;
                    header_target.action = FileAction::AddInclude;
                    header_target.line_start = last_include + 2;
                    header_target.line_end = last_include + 2;
                    header_target.note = "Add extern template declaration";
                    suggestion.secondary_files.push_back(header_target);
                    break;
                }
            }

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