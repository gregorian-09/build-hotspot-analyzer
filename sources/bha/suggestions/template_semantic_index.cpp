#include "bha/suggestions/template_semantic_index.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <utility>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
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
                record.use_files.push_back(source_file_);
                record.complete_definition = declaration->getDefinition() != nullptr;
                record.has_explicit_instantiation =
                    declaration->getSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration ||
                    declaration->getSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition;
                record.has_external_linkage = primary->getFormalLinkage() == clang::Linkage::External;
                record.has_dependent_arguments = std::ranges::any_of(
                    declaration->getTemplateArgs().asArray(),
                    [](const clang::TemplateArgument& argument) {
                        return argument.isInstantiationDependent();
                    }
                );
                record.has_unsupported_scope =
                    !primary->getDeclContext()->isFileContext() ||
                    primary->getNameAsString().empty();
                if (declaration->getSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition) {
                    record.explicit_definition_files.push_back(source_file_);
                }
                records_.push_back(std::move(record));
                return true;
            }

            bool VisitTypeLoc(clang::TypeLoc type_location) {
                const auto* record_type = type_location.getType()->getAs<clang::RecordType>();
                const auto* specialization = record_type
                    ? llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record_type->getDecl())
                    : nullptr;
                if (specialization && specialization->getSpecializedTemplate()) {
                    use_specializations_.push_back(
                        specialization->getSpecializedTemplate()->getQualifiedNameAsString() +
                        render_template_arguments(specialization->getTemplateArgs(), context_)
                    );
                }
                return true;
            }

            bool VisitVarDecl(clang::VarDecl* declaration) {
                if (declaration) {
                    record_type_use(declaration->getType(), "variable-declaration");
                }
                return true;
            }

            bool VisitFieldDecl(clang::FieldDecl* declaration) {
                if (declaration) {
                    record_type_use(declaration->getType(), "field-declaration");
                }
                return true;
            }

            bool VisitUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* expression) {
                if (expression) {
                    record_type_use(expression->getArgumentType(), "type-trait", true);
                }
                return true;
            }

            bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* expression) {
                if (expression && expression->getArgument()) {
                    record_type_use(expression->getArgument()->getType(), "delete-expression", true);
                }
                return true;
            }

            bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* expression) {
                if (expression && expression->getImplicitObjectArgument()) {
                    record_type_use(
                        expression->getImplicitObjectArgument()->getType(),
                        "member-call",
                        true
                    );
                }
                return true;
            }

            bool VisitCXXBaseSpecifier(clang::CXXBaseSpecifier* base) {
                if (base) {
                    record_type_use(base->getType(), "base-specifier", true);
                }
                return true;
            }

            bool VisitFunctionDecl(clang::FunctionDecl* declaration) {
                if (!declaration) {
                    return true;
                }
                record_type_use(declaration->getReturnType(), "function-return");
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
                record.use_files.push_back(source_file_);
                record.complete_definition = declaration->doesThisDeclarationHaveABody();
                record.has_explicit_instantiation =
                    declaration->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration ||
                    declaration->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition;
                record.has_external_linkage = primary->getFormalLinkage() == clang::Linkage::External;
                record.has_dependent_arguments = std::ranges::any_of(
                    declaration->getTemplateSpecializationArgs()->asArray(),
                    [](const clang::TemplateArgument& argument) {
                        return argument.isInstantiationDependent();
                    }
                );
                record.has_unsupported_scope =
                    !primary->getDeclContext()->isFileContext() ||
                    primary->getNameAsString().empty();
                if (declaration->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition) {
                    record.explicit_definition_files.push_back(source_file_);
                }
                records_.push_back(std::move(record));
                return true;
            }

            [[nodiscard]] std::vector<TemplateSemanticRecord> take_records() {
                return std::move(records_);
            }

            [[nodiscard]] const std::vector<std::string>& use_specializations() const noexcept {
                return use_specializations_;
            }

            [[nodiscard]] std::vector<TemplateSemanticUse> take_uses() {
                return std::move(uses_);
            }

        private:
            void record_type_use(
                clang::QualType type,
                std::string kind,
                const bool force_complete = false
            ) {
                if (type.isNull()) {
                    return;
                }

                const bool requires_complete_type = force_complete ||
                    (!type->isPointerType() && !type->isReferenceType());
                while (type->isPointerType() || type->isReferenceType()) {
                    type = type->getPointeeType();
                }

                const auto* record_type = type->getAs<clang::RecordType>();
                const auto* specialization = record_type
                    ? llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record_type->getDecl())
                    : nullptr;
                if (!specialization || !specialization->getSpecializedTemplate()) {
                    return;
                }

                const std::string key = specialization->getSpecializedTemplate()->getQualifiedNameAsString() +
                    render_template_arguments(specialization->getTemplateArgs(), context_);
                use_specializations_.push_back(key);
                uses_.push_back({key, source_file_, std::move(kind), requires_complete_type});
            }

            clang::ASTContext& context_;
            fs::path source_file_;
            std::vector<TemplateSemanticRecord> records_;
            std::vector<std::string> use_specializations_;
            std::vector<TemplateSemanticUse> uses_;
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
            auto uses = visitor.take_uses();
            const auto& use_specializations = visitor.use_specializations();
            for (auto& record : records) {
                if (std::ranges::find(use_specializations, record.specialization) != use_specializations.end() &&
                    std::ranges::find(record.use_files, command.source_file) == record.use_files.end()) {
                    record.use_files.push_back(command.source_file);
                }
                for (auto& use : uses) {
                    if (use.specialization == record.specialization) {
                        use.specialization.clear();
                        record.uses.push_back(std::move(use));
                    }
                }
            }
            records_.insert(records_.end(), records.begin(), records.end());
        }

        std::vector<TemplateSemanticRecord> merged;
        std::unordered_map<std::string, std::size_t> record_indices;
        for (auto& record : records_) {
            const auto [it, inserted] = record_indices.emplace(record.specialization, merged.size());
            if (inserted) {
                merged.push_back(std::move(record));
                continue;
            }

            auto& existing = merged[it->second];
            existing.complete_definition = existing.complete_definition || record.complete_definition;
            existing.has_explicit_instantiation =
                existing.has_explicit_instantiation || record.has_explicit_instantiation;
            existing.has_external_linkage = existing.has_external_linkage || record.has_external_linkage;
            existing.has_dependent_arguments = existing.has_dependent_arguments || record.has_dependent_arguments;
            existing.has_unsupported_scope = existing.has_unsupported_scope || record.has_unsupported_scope;
            if (existing.declaration_file.empty()) {
                existing.declaration_file = record.declaration_file;
            }
            for (const auto& file : record.use_files) {
                if (std::ranges::find(existing.use_files, file) == existing.use_files.end()) {
                    existing.use_files.push_back(file);
                }
            }
            for (const auto& file : record.explicit_definition_files) {
                if (std::ranges::find(existing.explicit_definition_files, file) ==
                    existing.explicit_definition_files.end()) {
                    existing.explicit_definition_files.push_back(file);
                }
            }
            for (auto& use : record.uses) {
                const auto duplicate = std::ranges::find_if(
                    existing.uses,
                    [&use](const TemplateSemanticUse& candidate) {
                        return candidate.source_file == use.source_file &&
                               candidate.kind == use.kind &&
                               candidate.requires_complete_type == use.requires_complete_type;
                    }
                );
                if (duplicate == existing.uses.end()) {
                    existing.uses.push_back(std::move(use));
                }
            }
        }

        for (auto& record : merged) {
            record.has_single_explicit_definition = record.explicit_definition_files.size() == 1;
        }
        records_ = std::move(merged);

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

    const TemplateSemanticRecord* TemplateSemanticIndex::find_exact(
        const std::string_view specialization
    ) const noexcept {
        const auto it = std::ranges::find(records_, specialization, &TemplateSemanticRecord::specialization);
        return it == records_.end() ? nullptr : &*it;
    }

}  // namespace bha::suggestions
