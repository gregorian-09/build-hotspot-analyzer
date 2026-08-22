// PIMPL candidates require semantic evidence from a compile-command-backed AST.

#include "bha/suggestions/pimpl_suggester.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include "bha/refactor/pimpl_eligibility.hpp"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/Casting.h>
#endif

namespace bha::suggestions {
    namespace {

        fs::path normalize_path(const ProjectIndex& project_index, const fs::path& path) {
            return project_index.resolve(path).lexically_normal();
        }

        bool same_path(
            const ProjectIndex& project_index,
            const fs::path& left,
            const fs::path& right
        ) {
            return normalize_path(project_index, left) == normalize_path(project_index, right);
        }

        bool is_compile_input(
            const ProjectIndex& project_index,
            const CompilationUnit& command,
            const std::string& argument
        ) {
            fs::path candidate(argument);
            if (candidate.is_relative()) {
                candidate = command.working_directory / candidate;
            }
            if (same_path(project_index, candidate, command.source_file)) {
                return true;
            }
            return false;
        }

#if BHA_HAVE_CLANG_TOOLING
        std::vector<std::string> tooling_arguments(
            const ProjectIndex& project_index,
            const CompilationUnit& command
        ) {
            std::vector<std::string> arguments;
            if (command.command_line.empty()) {
                return arguments;
            }

            arguments.reserve(command.command_line.size() + 3);
            if (!command.working_directory.empty()) {
                arguments.push_back("-working-directory");
                arguments.push_back(command.working_directory.string());
            }

            for (std::size_t index = 1; index < command.command_line.size(); ++index) {
                const auto& argument = command.command_line[index];
                if (argument == "-c" || argument == "/c" ||
                    argument == "-Winvalid-pch" || argument == "-ftime-trace" ||
                    argument.starts_with("-ftime-trace=") || argument == "/ftime-trace") {
                    continue;
                }
                if (argument == "-o" || argument == "-MF" || argument == "-MT" ||
                    argument == "-MQ" || argument == "/Fo" || argument == "/Fe") {
                    ++index;
                    continue;
                }
                if (argument.starts_with("-o") && argument.size() > 2) {
                    continue;
                }
                if (argument.starts_with("/Fo") || argument.starts_with("/Fe")) {
                    continue;
                }
                if (is_compile_input(project_index, command, argument)) {
                    continue;
                }
                arguments.push_back(argument);
            }
            arguments.push_back("-fsyntax-only");
            return arguments;
        }

        struct PimplAstCandidate {
            fs::path header;
            std::string qualified_name;
            std::size_t private_data_members = 0;
            std::size_t line = 0;
            refactor::PimplEligibilityState eligibility;
        };

        fs::path declaration_path(
            const clang::SourceManager& source_manager,
            const clang::SourceLocation location
        ) {
            if (location.isInvalid()) {
                return {};
            }
            const auto spelling = source_manager.getSpellingLoc(location);
            if (spelling.isInvalid()) {
                return {};
            }
            return fs::path(source_manager.getFilename(spelling).str()).lexically_normal();
        }

        class PimplRecordVisitor final
            : public clang::RecursiveASTVisitor<PimplRecordVisitor> {
        public:
            explicit PimplRecordVisitor(const clang::SourceManager& source_manager)
                : source_manager_(source_manager) {}

            bool VisitCXXRecordDecl(clang::CXXRecordDecl* record) {
                if (record == nullptr || !record->isThisDeclarationADefinition() ||
                    record->getNameAsString().empty() || record->isInjectedClassName()) {
                    return true;
                }

                if (record->getLocation().isMacroID()) {
                    return true;
                }

                PimplAstCandidate candidate;
                candidate.header = declaration_path(source_manager_, record->getLocation());
                candidate.qualified_name = record->getQualifiedNameAsString();
                candidate.line = source_manager_.getSpellingLineNumber(record->getLocation());
                candidate.eligibility.has_compile_context = true;
                candidate.eligibility.has_macro_generated_class =
                    record->getLocation().isMacroID();
                candidate.eligibility.has_template_declaration =
                    record->getDescribedClassTemplate() != nullptr ||
                    record->getTemplateSpecializationKind() != clang::TSK_Undeclared;
                candidate.eligibility.has_inheritance = record->getNumBases() != 0;

                for (const auto* field : record->fields()) {
                    if (field == nullptr || field->getAccess() != clang::AS_private) {
                        continue;
                    }
                    ++candidate.private_data_members;
                    if (field->getLocation().isMacroID()) {
                        candidate.eligibility.has_macro_generated_private_declarations = true;
                    }
                }
                candidate.eligibility.private_data_members = candidate.private_data_members;

                for (const auto* method : record->methods()) {
                    if (method == nullptr || method->isImplicit()) {
                        continue;
                    }
                    if (method->isVirtual()) {
                        candidate.eligibility.has_virtual_members = true;
                    }
                    if (method->getAccess() == clang::AS_private) {
                        if (method->hasInlineBody() && !method->isDefaulted()) {
                            candidate.eligibility.has_private_inline_method_bodies = true;
                        }
                    }
                    if (method->getLocation().isMacroID()) {
                        candidate.eligibility.has_macro_generated_private_declarations = true;
                    }

                    const auto* copy_constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(method);
                    const bool explicit_copy_constructor =
                        copy_constructor != nullptr && copy_constructor->isCopyConstructor() &&
                        !copy_constructor->isImplicit() && !copy_constructor->isDefaulted() &&
                        !copy_constructor->isDeleted();
                    const bool explicit_copy_assignment =
                        method->isCopyAssignmentOperator() && !method->isImplicit() &&
                        !method->isDefaulted() && !method->isDeleted();
                    if (explicit_copy_constructor || explicit_copy_assignment) {
                        candidate.eligibility.has_explicit_copy_definition = true;
                    }
                }

                candidates_.push_back(std::move(candidate));
                return true;
            }

            std::vector<PimplAstCandidate> take_candidates() {
                return std::move(candidates_);
            }

        private:
            const clang::SourceManager& source_manager_;
            std::vector<PimplAstCandidate> candidates_;
        };

        std::vector<PimplAstCandidate> parse_translation_unit(
            const ProjectIndex& project_index,
            const CompilationUnit& command,
            std::string& diagnostic
        ) {
            const auto source = project_index.read_file(command.source_file);
            if (!source.has_value()) {
                diagnostic = "Failed to read a compile-command-backed translation unit";
                return {};
            }

            const auto arguments = tooling_arguments(project_index, command);
            if (arguments.empty()) {
                diagnostic = "The compilation database command has no usable Clang arguments";
                return {};
            }

            auto ast = clang::tooling::buildASTFromCodeWithArgs(
                *source,
                arguments,
                command.source_file.string()
            );
            if (!ast || ast->getDiagnostics().hasErrorOccurred()) {
                diagnostic = "Clang failed to build a diagnostic-free AST for a translation unit";
                return {};
            }

            PimplRecordVisitor visitor(ast->getASTContext().getSourceManager());
            visitor.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());
            return visitor.take_candidates();
        }
#endif

#if BHA_HAVE_CLANG_TOOLING
        Suggestion make_advisory(
            const PimplAstCandidate& candidate,
            const fs::path& source_file
        ) {
            Suggestion suggestion;
            suggestion.id = generate_suggestion_id(
                "pimpl-ast",
                candidate.header,
                candidate.qualified_name
            );
            suggestion.type = SuggestionType::PIMPLPattern;
            suggestion.priority = Priority::Medium;
            suggestion.confidence = 1.0;
            suggestion.title = "AST-verified PIMPL candidate: " + candidate.qualified_name;
            suggestion.description =
                "Clang identified " + candidate.qualified_name + " as a project-owned class "
                "with " + std::to_string(candidate.private_data_members) +
                " private non-static data member(s). The class is observed in the measured "
                "translation unit " + source_file.generic_string() + ".";
            suggestion.rationale =
                "The class declaration, ownership, access, and data members were obtained "
                "from the Clang AST for the exact compile command. No filename convention, "
                "include-count threshold, text parser, or savings percentage is used.";
            suggestion.estimated_savings = Duration::zero();
            suggestion.estimated_savings_percent = 0.0;
            suggestion.target_file.path = candidate.header;
            suggestion.target_file.line_start = candidate.line;
            suggestion.target_file.line_end = candidate.line;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "AST declaration location; manual PIMPL design review required";
            suggestion.impact.files_benefiting.push_back(source_file);
            suggestion.impact.total_files_affected = 1;
            suggestion.impact.cumulative_savings = Duration::zero();
            suggestion.implementation_steps = {
                "Design the Impl boundary for the AST-identified class",
                "Run bha-refactor with the exact compilation database, source, header, and class name",
                "Run syntax and full-build validation for every affected compile command",
                "Collect a post-edit trace before claiming a build-time saving"
            };
            suggestion.caveats = {
                "This is an advisory candidate only; the analysis pipeline does not auto-apply PIMPL edits",
                "The structural tool accepts only its documented fail-closed class subset",
                "Estimated savings remain zero until a post-edit trace measures the change",
                "Unsupported class shapes are rejected by the semantic eligibility rules"
            };
            suggestion.verification =
                "Run the structural AST replacement tool, inspect its replacements, then run "
                "Clang syntax validation, the full build, and a fresh trace comparison";
            suggestion.is_safe = false;
            suggestion.application_mode = SuggestionApplicationMode::Advisory;
            suggestion.application_summary = "Manual review only";
            suggestion.application_guidance =
                "Review the class design manually, then invoke bha-refactor explicitly; automatic "
                "PIMPL application from suggestions remains disabled";
            suggestion.auto_apply_blocked_reason =
                "PIMPL suggestions remain advisory; explicit structural refactor invocation and "
                "post-edit build validation are required";
            return suggestion;
        }
#endif

    }  // namespace

    Result<SuggestionResult, Error> PIMPLSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        const auto started = std::chrono::steady_clock::now();

        if (!context.project_index ||
            context.project_index->compile_commands_status() != CompilationDatabaseStatus::Loaded) {
            result.diagnostics.push_back({
                "pimpl.evidence.compile_database_required",
                "PIMPL candidates require a valid compilation database for AST analysis"
            });
            result.generation_time = std::chrono::steady_clock::now() - started;
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

#if !BHA_HAVE_CLANG_TOOLING
        result.diagnostics.push_back({
            "pimpl.evidence.clang_tooling_required",
            "PIMPL candidates are disabled because this build has no Clang LibTooling backend"
        });
        result.generation_time = std::chrono::steady_clock::now() - started;
        return Result<SuggestionResult, Error>::success(std::move(result));
#else
        std::unordered_set<std::string> emitted;
        for (const auto& file : context.analysis.files) {
            if (context.is_cancelled()) {
                break;
            }
            if (!context.should_analyze(file.file)) {
                continue;
            }
            ++result.items_analyzed;
            if (file.compile_time <= Duration::zero()) {
                ++result.items_skipped;
                continue;
            }

            const auto command = context.project_index->compile_command_for(file.file);
            if (!command.has_value()) {
                ++result.items_skipped;
                continue;
            }

            std::string diagnostic;
            const auto candidates = parse_translation_unit(
                *context.project_index,
                *command,
                diagnostic
            );
            if (!diagnostic.empty()) {
                result.diagnostics.push_back({"pimpl.evidence.ast_failed", diagnostic});
                ++result.items_skipped;
                continue;
            }

            for (const auto& candidate : candidates) {
                if (candidate.header.empty() || candidate.private_data_members == 0 ||
                    same_path(*context.project_index, candidate.header, file.file) ||
                    !context.project_index->find_file(candidate.header).has_value() ||
                    refactor::first_pimpl_eligibility_blocker(candidate.eligibility).has_value()) {
                    continue;
                }
                const auto key = candidate.header.generic_string() + "\n" + candidate.qualified_name;
                if (!emitted.insert(key).second) {
                    continue;
                }
                result.suggestions.push_back(make_advisory(candidate, file.file));
            }
        }

        std::ranges::sort(result.suggestions, [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
        result.generation_time = std::chrono::steady_clock::now() - started;
        return Result<SuggestionResult, Error>::success(std::move(result));
#endif
    }

    void register_pimpl_pattern_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<PIMPLSuggester>()
        );
    }

}  // namespace bha::suggestions
