#include "bha/suggestions/template_semantic_index.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <utility>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/raw_ostream.h>
#endif

namespace bha::suggestions {
    namespace {

#if BHA_HAVE_CLANG_TOOLING
        std::string specialization_kind(const clang::TemplateSpecializationKind kind) {
            switch (kind) {
                case clang::TSK_ExplicitSpecialization:
                    return "explicit-specialization";
                case clang::TSK_ExplicitInstantiationDeclaration:
                    return "explicit-instantiation-declaration";
                case clang::TSK_ExplicitInstantiationDefinition:
                    return "explicit-instantiation-definition";
                case clang::TSK_ImplicitInstantiation:
                    return "implicit-instantiation";
                case clang::TSK_Undeclared:
                    return "undeclared";
            }
            return "unknown";
        }

        std::string render_template_arguments(
            const clang::TemplateArgumentList& arguments,
            const clang::ASTContext& context
        ) {
            std::string rendered;
            llvm::raw_string_ostream output(rendered);
            output << '<';
            for (unsigned index = 0; index < arguments.size(); ++index) {
                if (index != 0) {
                    output << ", ";
                }
                arguments[index].print(context.getPrintingPolicy(), output, true);
            }
            output << '>';
            output.flush();
            return rendered;
        }

        fs::path source_path(
            const clang::SourceManager& source_manager,
            const clang::SourceLocation location
        ) {
            if (location.isInvalid()) {
                return {};
            }
            return fs::path(source_manager.getFilename(source_manager.getSpellingLoc(location)).str());
        }

        class TemplateVisitor final : public clang::RecursiveASTVisitor<TemplateVisitor> {
        public:
            explicit TemplateVisitor(clang::ASTContext& context, fs::path source_file)
                : context_(context), source_file_(std::move(source_file)) {}

            bool VisitClassTemplateSpecializationDecl(
                clang::ClassTemplateSpecializationDecl* declaration
            ) {
                if (!declaration || declaration->getSpecializationKind() == clang::TSK_Undeclared) {
                    return true;
                }

                const auto* primary = declaration->getSpecializedTemplate();
                if (!primary || !declaration->getCanonicalDecl()->getLocation().isValid()) {
                    return true;
                }

                TemplateSemanticRecord record;
                record.template_name = primary->getQualifiedNameAsString();
                record.specialization = record.template_name + render_template_arguments(
                    declaration->getTemplateArgs(), context_
                );
                record.specialization_kind = specialization_kind(declaration->getSpecializationKind());
                record.source_file = source_file_;
                record.declaration_file = source_path(context_.getSourceManager(), primary->getLocation());
                record.complete_definition = declaration->getDefinition() != nullptr;
                record.has_explicit_instantiation =
                    declaration->getSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration ||
                    declaration->getSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition;
                records_.push_back(std::move(record));
                return true;
            }

            bool VisitFunctionDecl(clang::FunctionDecl* declaration) {
                if (!declaration) {
                    return true;
                }
                const auto* info = declaration->getTemplateSpecializationInfo();
                if (!info || !declaration->getTemplateSpecializationArgs()) {
                    return true;
                }

                const auto* primary = info->getTemplate();
                if (!primary) {
                    return true;
                }

                TemplateSemanticRecord record;
                record.template_name = primary->getQualifiedNameAsString();
                record.specialization = record.template_name + render_template_arguments(
                    *declaration->getTemplateSpecializationArgs(), context_
                );
                record.specialization_kind = specialization_kind(
                    declaration->getTemplateSpecializationKind()
                );
                record.source_file = source_file_;
                record.declaration_file = source_path(context_.getSourceManager(), primary->getLocation());
                record.complete_definition = declaration->doesThisDeclarationHaveABody();
                record.has_explicit_instantiation =
                    declaration->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration ||
                    declaration->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition;
                records_.push_back(std::move(record));
                return true;
            }

            [[nodiscard]] std::vector<TemplateSemanticRecord> take_records() {
                return std::move(records_);
            }

        private:
            clang::ASTContext& context_;
            fs::path source_file_;
            std::vector<TemplateSemanticRecord> records_;
        };

        std::vector<std::string> tooling_arguments(
            const CompilationUnit& command,
            const fs::path& source_file
        ) {
            std::vector<std::string> arguments;
            if (command.command_line.empty()) {
                return arguments;
            }

            arguments.reserve(command.command_line.size() + 1);
            for (std::size_t index = 1; index < command.command_line.size(); ++index) {
                const std::string& argument = command.command_line[index];
                if (argument == "-c" || argument == "/c") {
                    continue;
                }
                if (argument == "-o" || argument == "/Fo" || argument == "/Fe") {
                    ++index;
                    continue;
                }
                if (argument.starts_with("-o") || argument.starts_with("/Fo")) {
                    continue;
                }

                const fs::path candidate(argument);
                if (candidate.is_absolute() && candidate.lexically_normal() == source_file.lexically_normal()) {
                    continue;
                }
                arguments.push_back(argument);
            }
            arguments.push_back("-fsyntax-only");
            return arguments;
        }
#endif

    }  // namespace

    TemplateSemanticIndex::TemplateSemanticIndex(ProjectIndex& project_index)
        : project_index_(project_index) {}

    void TemplateSemanticIndex::build() {
        records_.clear();
        diagnostic_.clear();

#if !BHA_HAVE_CLANG_TOOLING
        status_ = TemplateSemanticStatus::Unavailable;
        diagnostic_ = "Clang LibTooling is not available in this BHA build";
        return;
#else
        if (project_index_.compile_commands_status() != CompilationDatabaseStatus::Loaded) {
            status_ = TemplateSemanticStatus::NoCompilationDatabase;
            diagnostic_ = "A valid compile_commands.json is required for Clang AST validation";
            return;
        }

        for (const auto& command : project_index_.compile_commands()) {
            const auto source = project_index_.read_file(command.source_file);
            if (!source.has_value()) {
                status_ = TemplateSemanticStatus::Failed;
                diagnostic_ = "Failed to read a translation unit from the compilation database";
                records_.clear();
                return;
            }

            auto ast = clang::tooling::buildASTFromCodeWithArgs(
                *source,
                tooling_arguments(command, command.source_file),
                command.source_file.string()
            );
            if (!ast || ast->getDiagnostics().hasErrorOccurred()) {
                status_ = TemplateSemanticStatus::Failed;
                diagnostic_ = "Clang failed to build a diagnostic-free AST for a translation unit";
                records_.clear();
                return;
            }

            TemplateVisitor visitor(ast->getASTContext(), command.source_file);
            visitor.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());
            auto records = visitor.take_records();
            records_.insert(records_.end(), records.begin(), records.end());
        }

        status_ = TemplateSemanticStatus::Parsed;
#endif
    }

    TemplateSemanticStatus TemplateSemanticIndex::status() const noexcept {
        return status_;
    }

    const std::string& TemplateSemanticIndex::diagnostic() const noexcept {
        return diagnostic_;
    }

    const std::vector<TemplateSemanticRecord>& TemplateSemanticIndex::records() const noexcept {
        return records_;
    }

}  // namespace bha::suggestions
