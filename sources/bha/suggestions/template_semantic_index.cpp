#include "bha/suggestions/template_semantic_index.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif
#ifndef BHA_HAVE_CLANG_DEP_SCANNING
#define BHA_HAVE_CLANG_DEP_SCANNING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/Core/Replacement.h>
#include <clang/Tooling/Tooling.h>
#if BHA_HAVE_CLANG_DEP_SCANNING
#if __has_include(<clang/Tooling/DependencyScanning/DependencyScanningTool.h>)
#include <clang/Tooling/DependencyScanning/DependencyScanningTool.h>
#else
#include <clang/Tooling/DependencyScanningTool.h>
#endif
#endif
#include <llvm/Support/raw_ostream.h>
#endif

namespace bha::suggestions {
    namespace {

#if BHA_HAVE_CLANG_TOOLING
        bool supported_language_mode(const std::string_view mode) {
            static constexpr std::array<std::string_view, 18> modes = {
                "c++98", "gnu++98", "c++03", "gnu++03", "c++11", "gnu++11",
                "c++14", "gnu++14", "c++17", "gnu++17", "c++20", "gnu++20",
                "c++23", "gnu++23", "c++26", "gnu++26", "c++2b", "gnu++2b"
            };
            return std::ranges::find(modes, mode) != modes.end();
        }

        bool supported_compile_language(const std::vector<std::string>& arguments) {
            for (const auto& argument : arguments) {
                if (argument.starts_with("-std=")) {
                    if (!supported_language_mode(argument.substr(5))) {
                        return false;
                    }
                } else if (argument.starts_with("/std:")) {
                    const auto mode = argument.substr(5);
                    if (mode != "c++14" && mode != "c++17" && mode != "c++20" &&
                        mode != "c++latest") {
                        return false;
                    }
                }
            }
            return true;
        }

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

        std::string render_function_instantiation(
            const clang::FunctionDecl& declaration,
            const clang::FunctionTemplateDecl& primary,
            const clang::TemplateArgumentList& arguments,
            const clang::ASTContext& context
        ) {
            std::string qualified_name = primary.getQualifiedNameAsString();
            if (llvm::isa<clang::CXXMethodDecl>(&declaration)) {
                const auto declaration_name = declaration.getQualifiedNameAsString();
                if (declaration_name.find('<') != std::string::npos) {
                    qualified_name = declaration_name;
                }
            }
            std::string rendered;
            llvm::raw_string_ostream output(rendered);
            declaration.getReturnType().print(output, context.getPrintingPolicy());
            output << ' ' << qualified_name
                   << render_template_arguments(arguments, context) << '(';
            for (unsigned index = 0; index < declaration.getNumParams(); ++index) {
                if (index != 0) {
                    output << ", ";
                }
                declaration.getParamDecl(index)->getType().print(output, context.getPrintingPolicy());
            }
            if (declaration.isVariadic()) {
                if (declaration.getNumParams() != 0) {
                    output << ", ";
                }
                output << "...";
            }
            output << ')';
            if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&declaration)) {
                const auto qualifiers = method->getMethodQualifiers();
                if (qualifiers.hasConst()) {
                    output << " const";
                }
                if (qualifiers.hasVolatile()) {
                    output << " volatile";
                }
                switch (method->getType()->castAs<clang::FunctionProtoType>()->getRefQualifier()) {
                    case clang::RQ_LValue:
                        output << " &";
                        break;
                    case clang::RQ_RValue:
                        output << " &&";
                        break;
                    case clang::RQ_None:
                        break;
                }
            }
            switch (declaration.getExceptionSpecType()) {
                case clang::EST_BasicNoexcept:
                case clang::EST_NoexceptTrue:
                    output << " noexcept";
                    break;
                case clang::EST_NoexceptFalse:
                    output << " noexcept(false)";
                    break;
                default:
                    break;
            }
            output.flush();
            return rendered;
        }

        std::string render_variable_instantiation(
            const clang::VarDecl& declaration,
            const clang::VarTemplateDecl& primary,
            const clang::TemplateArgumentList& arguments,
            const clang::ASTContext& context
        ) {
            std::string rendered;
            llvm::raw_string_ostream output(rendered);
            declaration.getType().print(output, context.getPrintingPolicy());
            output << ' ' << primary.getQualifiedNameAsString()
                   << render_template_arguments(arguments, context) << ';';
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

            bool shouldVisitTemplateInstantiations() const {
                return true;
            }

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
                record.declaration_kind = "class";
                record.canonical_extern_declaration = "extern template class " + record.specialization + ";";
                record.canonical_explicit_definition = "template class " + record.specialization + ";";
                record.source_file = source_file_;
                record.declaration_file = source_path(context_.getSourceManager(), primary->getLocation());
                const auto declaration_location = context_.getFullLoc(primary->getLocation());
                if (declaration_location.isValid()) {
                    record.declaration_line = declaration_location.getSpellingLineNumber();
                    record.declaration_column = declaration_location.getSpellingColumnNumber();
                }
                const auto declaration_end = context_.getFullLoc(
                    context_.getSourceManager().getSpellingLoc(primary->getSourceRange().getEnd())
                );
                if (declaration_end.isValid()) {
                    record.declaration_end_line = declaration_end.getSpellingLineNumber();
                    record.declaration_end_column = declaration_end.getSpellingColumnNumber();
                }
                const auto end_token = clang::Lexer::getLocForEndOfToken(
                    context_.getSourceManager().getSpellingLoc(primary->getSourceRange().getEnd()),
                    0,
                    context_.getSourceManager(),
                    context_.getLangOpts()
                );
                if (end_token.isValid()) {
                    record.declaration_end_offset = context_.getSourceManager().getFileOffset(end_token);
                }
                record.use_files.push_back(source_file_);
                record.complete_definition = declaration->getDefinition() != nullptr;
                record.has_explicit_instantiation =
                    declaration->getSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration ||
                    declaration->getSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition;
                record.has_explicit_instantiation_declaration =
                    declaration->getSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration;
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

            bool VisitVarDecl(clang::VarDecl* declaration) {
                if (declaration) {
                    record_type_use(
                        declaration->getType(),
                        "variable-declaration",
                        false,
                        declaration->getDeclContext()->isDependentContext()
                    );
                }
                return true;
            }

            bool VisitFieldDecl(clang::FieldDecl* declaration) {
                if (declaration) {
                    record_type_use(
                        declaration->getType(),
                        "field-declaration",
                        false,
                        declaration->getDeclContext()->isDependentContext()
                    );
                }
                return true;
            }

            bool VisitParmVarDecl(clang::ParmVarDecl* declaration) {
                if (declaration) {
                    record_type_use(
                        declaration->getType(),
                        "parameter-declaration",
                        false,
                        declaration->getDeclContext()->isDependentContext()
                    );
                }
                return true;
            }

            bool VisitUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* expression) {
                if (expression && expression->isArgumentType()) {
                    record_type_use(
                        expression->getArgumentType(),
                        "type-trait",
                        true,
                        expression->getArgumentType()->isInstantiationDependentType()
                    );
                } else if (expression && expression->getArgumentExpr()) {
                    record_type_use(
                        expression->getArgumentExpr()->getType(),
                        "type-trait-expression",
                        true,
                        expression->getArgumentExpr()->getType()->isInstantiationDependentType()
                    );
                }
                return true;
            }

            bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* expression) {
                if (expression && expression->getArgument()) {
                    record_type_use(
                        expression->getArgument()->getType(),
                        "delete-expression",
                        true,
                        expression->getArgument()->getType()->isInstantiationDependentType()
                    );
                }
                return true;
            }

            bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* expression) {
                if (expression && expression->getImplicitObjectArgument()) {
                    record_type_use(
                        expression->getImplicitObjectArgument()->getType(),
                        "member-call",
                        true,
                        expression->getImplicitObjectArgument()->getType()->isInstantiationDependentType()
                    );
                }
                return true;
            }

            bool VisitCXXBaseSpecifier(clang::CXXBaseSpecifier* base) {
                if (base) {
                    record_type_use(
                        base->getType(),
                        "base-specifier",
                        true,
                        base->getType()->isInstantiationDependentType()
                    );
                }
                return true;
            }

            bool VisitCXXNewExpr(clang::CXXNewExpr* expression) {
                if (expression) {
                    record_type_use(
                        expression->getAllocatedType(),
                        "new-expression",
                        true,
                        expression->getAllocatedType()->isInstantiationDependentType()
                    );
                }
                return true;
            }

            bool VisitCXXConstructExpr(clang::CXXConstructExpr* expression) {
                if (expression) {
                    record_type_use(
                        expression->getType(),
                        "construct-expression",
                        true,
                        expression->getType()->isInstantiationDependentType()
                    );
                }
                return true;
            }

            bool VisitFunctionDecl(clang::FunctionDecl* declaration) {
                if (!declaration) {
                    return true;
                }
                record_type_use(
                    declaration->getReturnType(),
                    "function-return",
                    false,
                    declaration->getDeclContext()->isDependentContext()
                );
                auto* primary = declaration->getPrimaryTemplate();
                if (!primary) {
                    if (auto* pattern = declaration->getTemplateInstantiationPattern()) {
                        primary = pattern->getDescribedFunctionTemplate();
                    }
                }
                if (!primary || !declaration->getTemplateSpecializationArgs()) {
                    return true;
                }
                record_function_specialization(
                    *declaration,
                    *primary,
                    *declaration->getTemplateSpecializationArgs()
                );
                return true;
            }

            bool VisitFunctionTemplateDecl(clang::FunctionTemplateDecl* declaration) {
                if (!declaration) {
                    return true;
                }
                for (auto* specialization : declaration->specializations()) {
                    if (!specialization || !specialization->getTemplateSpecializationArgs()) {
                        continue;
                    }
                    record_function_specialization(
                        *specialization,
                        *declaration,
                        *specialization->getTemplateSpecializationArgs()
                    );
                }
                return true;
            }

            bool VisitVarTemplateDecl(clang::VarTemplateDecl* declaration) {
                if (!declaration) {
                    return true;
                }
                for (auto* specialization : declaration->specializations()) {
                    if (!specialization) {
                        continue;
                    }
                    record_variable_specialization(*specialization, *declaration);
                }
                return true;
            }

        private:
            void record_variable_specialization(
                const clang::VarTemplateSpecializationDecl& declaration,
                const clang::VarTemplateDecl& primary
            ) {
                TemplateSemanticRecord record;
                record.template_name = primary.getQualifiedNameAsString();
                record.specialization = record.template_name + render_template_arguments(
                    declaration.getTemplateArgs(), context_
                );
                record.specialization_kind = specialization_kind(
                    declaration.getSpecializationKind()
                );
                record.declaration_kind = "variable";
                record.canonical_extern_declaration = "extern template " + render_variable_instantiation(
                    declaration, primary, declaration.getTemplateArgs(), context_
                );
                record.canonical_explicit_definition = "template " + render_variable_instantiation(
                    declaration, primary, declaration.getTemplateArgs(), context_
                );
                record.source_file = source_file_;
                record.declaration_file = source_path(context_.getSourceManager(), primary.getLocation());
                const auto declaration_location = context_.getFullLoc(primary.getLocation());
                if (declaration_location.isValid()) {
                    record.declaration_line = declaration_location.getSpellingLineNumber();
                    record.declaration_column = declaration_location.getSpellingColumnNumber();
                }
                const auto declaration_end = context_.getFullLoc(
                    context_.getSourceManager().getSpellingLoc(primary.getSourceRange().getEnd())
                );
                if (declaration_end.isValid()) {
                    record.declaration_end_line = declaration_end.getSpellingLineNumber();
                    record.declaration_end_column = declaration_end.getSpellingColumnNumber();
                }
                const auto end_token = clang::Lexer::getLocForEndOfToken(
                    context_.getSourceManager().getSpellingLoc(primary.getSourceRange().getEnd()),
                    0,
                    context_.getSourceManager(),
                    context_.getLangOpts()
                );
                if (end_token.isValid()) {
                    record.declaration_end_offset = context_.getSourceManager().getFileOffset(end_token);
                }
                record.use_files.push_back(source_file_);
                record.complete_definition = declaration.isThisDeclarationADefinition();
                record.has_explicit_instantiation =
                    declaration.getSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration ||
                    declaration.getSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition;
                record.has_external_linkage = primary.getFormalLinkage() == clang::Linkage::External;
                record.has_dependent_arguments = std::ranges::any_of(
                    declaration.getTemplateArgs().asArray(),
                    [](const clang::TemplateArgument& argument) {
                        return argument.isInstantiationDependent();
                    }
                );
                record.has_unsupported_scope =
                    !primary.getDeclContext()->isFileContext() ||
                    primary.getNameAsString().empty();
                const auto* pattern = primary.getTemplatedDecl();
                record.has_unsupported_variable_form =
                    !pattern || pattern->isInline() || pattern->isConstexpr() ||
                    declaration.hasExternalStorage() || declaration.getType()->isDependentType();
                if (declaration.getSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition) {
                    record.explicit_definition_files.push_back(source_file_);
                }
                records_.push_back(std::move(record));
            }

            void record_function_specialization(
                const clang::FunctionDecl& declaration,
                const clang::FunctionTemplateDecl& primary,
                const clang::TemplateArgumentList& arguments
            ) {
                TemplateSemanticRecord record;
                record.template_name = primary.getQualifiedNameAsString();
                record.specialization = function_specialization_key(declaration, primary, arguments, context_);
                record.specialization_kind = specialization_kind(
                    declaration.getTemplateSpecializationKind()
                );
                record.declaration_kind = "function";
                const auto function_instantiation = render_function_instantiation(
                    declaration,
                    primary,
                    arguments,
                    context_
                );
                record.canonical_extern_declaration = "extern template " + function_instantiation + ";";
                record.canonical_explicit_definition = "template " + function_instantiation + ";";
                record.source_file = source_file_;
                record.declaration_file = source_path(context_.getSourceManager(), primary.getLocation());
                const auto declaration_location = context_.getFullLoc(primary.getLocation());
                if (declaration_location.isValid()) {
                    record.declaration_line = declaration_location.getSpellingLineNumber();
                    record.declaration_column = declaration_location.getSpellingColumnNumber();
                }
                const auto declaration_end = context_.getFullLoc(
                    context_.getSourceManager().getSpellingLoc(primary.getSourceRange().getEnd())
                );
                if (declaration_end.isValid()) {
                    record.declaration_end_line = declaration_end.getSpellingLineNumber();
                    record.declaration_end_column = declaration_end.getSpellingColumnNumber();
                }
                const auto end_token = clang::Lexer::getLocForEndOfToken(
                    context_.getSourceManager().getSpellingLoc(primary.getSourceRange().getEnd()),
                    0,
                    context_.getSourceManager(),
                    context_.getLangOpts()
                );
                if (end_token.isValid()) {
                    record.declaration_end_offset = context_.getSourceManager().getFileOffset(end_token);
                }
                record.use_files.push_back(source_file_);
                const auto* instantiation_pattern = declaration.getTemplateInstantiationPattern();
                record.complete_definition = primary.getTemplatedDecl()->doesThisDeclarationHaveABody() ||
                    (instantiation_pattern != nullptr &&
                     instantiation_pattern->doesThisDeclarationHaveABody());
                record.has_explicit_instantiation =
                    declaration.getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration ||
                    declaration.getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition;
                record.has_external_linkage = primary.getFormalLinkage() == clang::Linkage::External;
                record.has_dependent_arguments = std::ranges::any_of(
                    arguments.asArray(),
                    [](const clang::TemplateArgument& argument) {
                        return argument.isInstantiationDependent();
                    }
                );
                const auto* parent_record = llvm::dyn_cast<clang::CXXRecordDecl>(
                    primary.getDeclContext()
                );
                const auto* class_specialization = parent_record == nullptr
                    ? nullptr
                    : llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(parent_record);
                const bool dependent_member_owner = parent_record != nullptr &&
                    (parent_record->isDependentContext() ||
                     (class_specialization == nullptr && parent_record->getDescribedClassTemplate()) ||
                     (class_specialization != nullptr && std::ranges::any_of(
                         class_specialization->getTemplateArgs().asArray(),
                         [](const clang::TemplateArgument& argument) {
                             return argument.isInstantiationDependent();
                         }
                     )));
                record.has_unsupported_scope =
                    (parent_record == nullptr && !primary.getDeclContext()->isFileContext()) ||
                    dependent_member_owner || primary.getNameAsString().empty();
                record.has_unsupported_function_form =
                    declaration.isInlineSpecified() || declaration.isConstexpr() ||
                    declaration.isConsteval() || declaration.isDeleted() ||
                    declaration.isDefaulted() ||
                    (declaration.getExceptionSpecType() != clang::EST_None &&
                     declaration.getExceptionSpecType() != clang::EST_BasicNoexcept &&
                     declaration.getExceptionSpecType() != clang::EST_NoexceptTrue &&
                     declaration.getExceptionSpecType() != clang::EST_NoexceptFalse);
                if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&declaration)) {
                    record.has_unsupported_function_form =
                        record.has_unsupported_function_form || method->isVirtual();
                }
                if (declaration.getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition) {
                    record.explicit_definition_files.push_back(source_file_);
                }
                records_.push_back(std::move(record));
            }

        public:
            bool VisitCallExpr(clang::CallExpr* expression) {
                if (expression) {
                    record_function_use(expression->getDirectCallee(), "function-call");
                }
                return true;
            }

            bool VisitDeclRefExpr(clang::DeclRefExpr* expression) {
                if (expression) {
                    record_function_use(
                        llvm::dyn_cast<clang::FunctionDecl>(expression->getDecl()),
                        "function-reference"
                    );
                    record_variable_use(
                        llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(expression->getDecl()),
                        "variable-reference"
                    );
                }
                return true;
            }

            [[nodiscard]] std::vector<TemplateSemanticRecord> take_records() {
                return std::move(records_);
            }

            struct PendingUse {
                std::string specialization;
                TemplateSemanticUse use;
            };

            [[nodiscard]] std::vector<PendingUse> take_uses() {
                return std::move(uses_);
            }

        private:
            std::string function_specialization_key(
                const clang::FunctionDecl& declaration,
                const clang::FunctionTemplateDecl& primary,
                const clang::TemplateArgumentList& arguments,
                const clang::ASTContext& context
            ) const {
                std::string qualified_name = primary.getQualifiedNameAsString();
                if (llvm::isa<clang::CXXMethodDecl>(&declaration)) {
                    const auto declaration_name = declaration.getQualifiedNameAsString();
                    if (declaration_name.find('<') != std::string::npos) {
                        qualified_name = declaration_name;
                    }
                }
                return qualified_name + render_template_arguments(arguments, context);
            }

            void record_function_use(
                const clang::FunctionDecl* declaration,
                std::string kind
            ) {
                if (!declaration) {
                    return;
                }
                auto* primary = declaration->getPrimaryTemplate();
                if (!primary) {
                    if (auto* pattern = declaration->getTemplateInstantiationPattern()) {
                        primary = pattern->getDescribedFunctionTemplate();
                    }
                }
                if (!primary || !declaration->getTemplateSpecializationArgs()) {
                    return;
                }
                const auto& arguments = *declaration->getTemplateSpecializationArgs();
                uses_.push_back({
                    function_specialization_key(*declaration, *primary, arguments, context_),
                    {
                        source_file_,
                        std::move(kind),
                        false,
                        declaration->getDeclContext()->isDependentContext()
                    }
                });
            }

            void record_variable_use(
                const clang::VarTemplateSpecializationDecl* declaration,
                std::string kind
            ) {
                if (!declaration || !declaration->getSpecializedTemplate()) {
                    return;
                }
                const auto* primary = declaration->getSpecializedTemplate();
                uses_.push_back({
                    primary->getQualifiedNameAsString() + render_template_arguments(
                        declaration->getTemplateArgs(), context_
                    ),
                    {
                        source_file_,
                        std::move(kind),
                        false,
                        declaration->getDeclContext()->isDependentContext()
                    }
                });
            }

            void record_type_use(
                clang::QualType type,
                std::string kind,
                const bool force_complete = false,
                const bool in_dependent_context = false
            ) {
                if (type.isNull()) {
                    return;
                }

                type = type.getCanonicalType();

                bool requires_complete_type = force_complete ||
                    (!type->isPointerType() && !type->isReferenceType());
                while (type->isPointerType() || type->isReferenceType()) {
                    type = type->getPointeeType();
                }
                while (type->isArrayType()) {
                    requires_complete_type = true;
                    type = type->getAsArrayTypeUnsafe()->getElementType();
                }

                const auto* record_type = type->getAs<clang::RecordType>();
                const auto* specialization = record_type
                    ? llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record_type->getDecl())
                    : nullptr;
                const auto record_nested_arguments = [this, &in_dependent_context](
                    const auto& arguments
                ) {
                    for (const auto& argument : arguments) {
                        if (argument.getKind() == clang::TemplateArgument::Type) {
                            record_type_use(
                                argument.getAsType(),
                                "template-argument",
                                true,
                                in_dependent_context || argument.getAsType()->isInstantiationDependentType()
                            );
                        }
                    }
                };

                if (!specialization || !specialization->getSpecializedTemplate()) {
                    if (const auto* template_type = type->getAs<clang::TemplateSpecializationType>()) {
                        record_nested_arguments(template_type->template_arguments());
                    }
                    return;
                }

                const std::string key = specialization->getSpecializedTemplate()->getQualifiedNameAsString() +
                    render_template_arguments(specialization->getTemplateArgs(), context_);
                uses_.push_back({
                    key,
                    {
                        source_file_,
                        std::move(kind),
                        requires_complete_type,
                        in_dependent_context || type->isInstantiationDependentType()
                    }
                });
                record_nested_arguments(specialization->getTemplateArgs().asArray());
            }

            clang::ASTContext& context_;
            fs::path source_file_;
            std::vector<TemplateSemanticRecord> records_;
            std::vector<PendingUse> uses_;
        };

        using json = nlohmann::json;

        std::uint64_t fnv1a_append(std::uint64_t hash, const std::string_view value) {
            for (const char character : value) {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ULL;
            }
            return hash;
        }

        fs::path resolve_compiler_executable(const std::string& compiler) {
            const fs::path direct(compiler);
            if (direct.is_absolute() || direct.has_parent_path()) {
                return direct.lexically_normal();
            }

            const char* path_value = std::getenv("PATH");
            if (path_value == nullptr) {
                return {};
            }
            const char separator =
#ifdef _WIN32
                ';';
#else
                ':';
#endif
            std::string search_path(path_value);
            std::size_t start = 0;
            while (start <= search_path.size()) {
                const auto end = search_path.find(separator, start);
                const auto directory = search_path.substr(start, end - start);
                const fs::path candidate = (directory.empty() ? fs::path(".") : fs::path(directory)) / compiler;
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec)) {
                    return candidate.lexically_normal();
                }
#ifdef _WIN32
                if (candidate.extension().empty()) {
                    const fs::path exe_candidate = candidate.string() + ".exe";
                    if (fs::is_regular_file(exe_candidate, ec)) {
                        return exe_candidate.lexically_normal();
                    }
                }
#endif
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            return {};
        }

#if BHA_HAVE_CLANG_DEP_SCANNING
        std::vector<fs::path> parse_dependency_files(
            const std::string& dependency_file,
            const fs::path& working_directory
        ) {
            const auto separator = dependency_file.find(':');
            if (separator == std::string::npos) {
                return {};
            }

            std::vector<fs::path> files;
            std::string token;
            const auto flush = [&]() {
                if (!token.empty()) {
                    fs::path path(token);
                    if (path.is_relative()) {
                        path = working_directory / path;
                    }
                    files.push_back(path.lexically_normal());
                    token.clear();
                }
            };
            for (std::size_t index = separator + 1; index < dependency_file.size(); ++index) {
                const char character = dependency_file[index];
                if (character == '\\' && index + 1 < dependency_file.size()) {
                    const char escaped = dependency_file[++index];
                    if (escaped != '\n') {
                        token.push_back(escaped);
                    }
                } else if (std::isspace(static_cast<unsigned char>(character))) {
                    flush();
                } else {
                    token.push_back(character);
                }
            }
            flush();
            return files;
        }
#endif

        std::string cache_fingerprint(
            ProjectIndex& project_index,
            const std::vector<CompilationUnit>& commands
        ) {
            std::uint64_t hash = 1469598103934665603ULL;
            hash = fnv1a_append(hash, "bha-template-semantic-index-v11");
            for (const auto& command : commands) {
                hash = fnv1a_append(hash, command.source_file.generic_string());
                hash = fnv1a_append(hash, std::string_view{"\0", 1});
                hash = fnv1a_append(hash, command.working_directory.generic_string());
                hash = fnv1a_append(hash, std::string_view{"\0", 1});
                if (!command.command_line.empty()) {
                    const auto compiler = resolve_compiler_executable(command.command_line.front());
                    hash = fnv1a_append(hash, compiler.generic_string());
                    std::error_code compiler_ec;
                    hash = fnv1a_append(hash, std::to_string(fs::file_size(compiler, compiler_ec)));
                    hash = fnv1a_append(
                        hash,
                        std::to_string(fs::last_write_time(compiler, compiler_ec).time_since_epoch().count())
                    );
                }
                hash = fnv1a_append(hash, std::string_view{"\0", 1});
                for (const auto& argument : command.command_line) {
                    hash = fnv1a_append(hash, argument);
                    hash = fnv1a_append(hash, std::string_view{"\0", 1});
                }
                hash = fnv1a_append(hash, std::string_view{"\0", 1});
                if (const auto source = project_index.read_file(command.source_file)) {
                    hash = fnv1a_append(hash, *source);
                }
                hash = fnv1a_append(hash, std::string_view{"\0", 1});
#if BHA_HAVE_CLANG_DEP_SCANNING
                clang::tooling::dependencies::DependencyScanningService service(
                    clang::tooling::dependencies::ScanningMode::DependencyDirectivesScan,
                    clang::tooling::dependencies::ScanningOutputFormat::Make
                );
                clang::tooling::dependencies::DependencyScanningTool scanner(service);
                auto dependency_result = scanner.getDependencyFile(
                    command.command_line,
                    command.working_directory.string()
                );
                if (dependency_result) {
                    for (const auto& dependency : parse_dependency_files(
                        *dependency_result,
                        command.working_directory
                    )) {
                        hash = fnv1a_append(hash, dependency.generic_string());
                        if (const auto content = project_index.read_file(dependency)) {
                            hash = fnv1a_append(hash, *content);
                        }
                        hash = fnv1a_append(hash, std::string_view{"\0", 1});
                    }
                } else {
                    hash = fnv1a_append(hash, "dependency-scan-failed");
                }
#endif
            }
#if !BHA_HAVE_CLANG_DEP_SCANNING
            for (const auto& header : project_index.files(ProjectFileKind::Header)) {
                std::error_code ec;
                const auto size = fs::file_size(header, ec);
                const auto timestamp = fs::last_write_time(header, ec);
                hash = fnv1a_append(hash, header.generic_string());
                hash = fnv1a_append(hash, std::string_view{"\0", 1});
                hash = fnv1a_append(hash, std::to_string(ec ? 0 : size));
                hash = fnv1a_append(
                    hash,
                    std::to_string(timestamp.time_since_epoch().count())
                );
                hash = fnv1a_append(hash, std::string_view{"\0", 1});
                if (const auto content = project_index.read_file(header)) {
                    hash = fnv1a_append(hash, *content);
                }
                hash = fnv1a_append(hash, std::string_view{"\0", 1});
            }
#endif
            std::ostringstream output;
            output << std::hex << hash;
            return output.str();
        }

        json serialize_use(const TemplateSemanticUse& use) {
            return {
                {"source_file", use.source_file.generic_string()},
                {"kind", use.kind},
                {"requires_complete_type", use.requires_complete_type},
                {"in_dependent_context", use.in_dependent_context}
            };
        }

        TemplateSemanticUse deserialize_use(const json& value) {
            return {
                value.value("source_file", ""),
                value.value("kind", ""),
                value.value("requires_complete_type", false),
                value.value("in_dependent_context", false)
            };
        }

        json serialize_record(const TemplateSemanticRecord& record) {
            json uses = json::array();
            for (const auto& use : record.uses) {
                uses.push_back(serialize_use(use));
            }
            json use_files = json::array();
            for (const auto& file : record.use_files) {
                use_files.push_back(file.generic_string());
            }
            json definitions = json::array();
            for (const auto& file : record.explicit_definition_files) {
                definitions.push_back(file.generic_string());
            }
            return {
                {"template_name", record.template_name},
                {"specialization", record.specialization},
                {"specialization_kind", record.specialization_kind},
                {"declaration_kind", record.declaration_kind},
                {"canonical_extern_declaration", record.canonical_extern_declaration},
                {"canonical_explicit_definition", record.canonical_explicit_definition},
                {"source_file", record.source_file.generic_string()},
                {"declaration_file", record.declaration_file.generic_string()},
                {"declaration_line", record.declaration_line},
                {"declaration_column", record.declaration_column},
                {"declaration_end_line", record.declaration_end_line},
                {"declaration_end_column", record.declaration_end_column},
                {"declaration_end_offset", record.declaration_end_offset},
                {"use_files", use_files},
                {"uses", uses},
                {"explicit_definition_files", definitions},
                {"complete_definition", record.complete_definition},
                {"has_explicit_instantiation", record.has_explicit_instantiation},
                {"has_explicit_instantiation_declaration", record.has_explicit_instantiation_declaration},
                {"has_external_linkage", record.has_external_linkage},
                {"has_single_explicit_definition", record.has_single_explicit_definition},
                {"has_dependent_arguments", record.has_dependent_arguments},
                {"has_dependent_use_context", record.has_dependent_use_context},
                {"has_unsupported_scope", record.has_unsupported_scope},
                {"has_unsupported_function_form", record.has_unsupported_function_form},
                {"has_unsupported_variable_form", record.has_unsupported_variable_form}
            };
        }

        TemplateSemanticRecord deserialize_record(const json& value) {
            TemplateSemanticRecord record;
            record.template_name = value.value("template_name", "");
            record.specialization = value.value("specialization", "");
            record.specialization_kind = value.value("specialization_kind", "");
            record.declaration_kind = value.value("declaration_kind", "");
            record.canonical_extern_declaration = value.value("canonical_extern_declaration", "");
            record.canonical_explicit_definition = value.value("canonical_explicit_definition", "");
            record.source_file = value.value("source_file", "");
            record.declaration_file = value.value("declaration_file", "");
            record.declaration_line = value.value("declaration_line", std::size_t{0});
            record.declaration_column = value.value("declaration_column", std::size_t{0});
            record.declaration_end_line = value.value("declaration_end_line", std::size_t{0});
            record.declaration_end_column = value.value("declaration_end_column", std::size_t{0});
            record.declaration_end_offset = value.value("declaration_end_offset", std::size_t{0});
            record.complete_definition = value.value("complete_definition", false);
            record.has_explicit_instantiation = value.value("has_explicit_instantiation", false);
            record.has_explicit_instantiation_declaration = value.value(
                "has_explicit_instantiation_declaration", false
            );
            record.has_external_linkage = value.value("has_external_linkage", false);
            record.has_single_explicit_definition = value.value("has_single_explicit_definition", false);
            record.has_dependent_arguments = value.value("has_dependent_arguments", false);
            record.has_dependent_use_context = value.value("has_dependent_use_context", false);
            record.has_unsupported_scope = value.value("has_unsupported_scope", false);
            record.has_unsupported_function_form = value.value(
                "has_unsupported_function_form", false
            );
            record.has_unsupported_variable_form = value.value(
                "has_unsupported_variable_form", false
            );
            if (value.contains("use_files") && value["use_files"].is_array()) {
                for (const auto& file : value["use_files"]) {
                    record.use_files.emplace_back(file.get<std::string>());
                }
            }
            if (value.contains("uses") && value["uses"].is_array()) {
                for (const auto& use : value["uses"]) {
                    record.uses.push_back(deserialize_use(use));
                }
            }
            if (value.contains("explicit_definition_files") && value["explicit_definition_files"].is_array()) {
                for (const auto& file : value["explicit_definition_files"]) {
                    record.explicit_definition_files.emplace_back(file.get<std::string>());
                }
            }
            return record;
        }

        fs::path semantic_cache_path(const ProjectIndex& project_index) {
            return project_index.project_root() / ".bha" / "template-semantic-index-v3.json";
        }

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

        const auto commands = project_index_.compile_commands();
        const auto fingerprint = cache_fingerprint(project_index_, commands);
        const auto cache_path = semantic_cache_path(project_index_);
        std::optional<json> reusable_cache;
        if (!cache_path.empty()) {
            std::ifstream input(cache_path);
            if (input) {
                try {
                    json cache;
                    input >> cache;
                    if (cache.value("schema", "") == "bha-template-semantic-index-v11" &&
                        cache.value("fingerprint", "") == fingerprint &&
                        cache.contains("records") && cache["records"].is_array()) {
                        for (const auto& value : cache["records"]) {
                            records_.push_back(deserialize_record(value));
                        }
                        status_ = TemplateSemanticStatus::Parsed;
                        return;
                    }
                    if (cache.value("schema", "") == "bha-template-semantic-index-v11" &&
                        cache.contains("translation_units") && cache["translation_units"].is_array()) {
                        reusable_cache = std::move(cache);
                    }
                } catch (const nlohmann::json::exception&) {
                    // A corrupt or old cache is ignored and rebuilt below.
                }
            }
        }

        json translation_units = json::array();
        std::unordered_map<std::string, std::vector<TemplateSemanticUse>> all_uses;
        for (const auto& command : commands) {
            const auto unit_fingerprint = cache_fingerprint(
                project_index_,
                std::vector<CompilationUnit>{command}
            );
            bool reused = false;
            if (reusable_cache.has_value()) {
                for (const auto& cached_unit : (*reusable_cache)["translation_units"]) {
                    if (!cached_unit.is_object() ||
                        cached_unit.value("source_file", "") != command.source_file.generic_string() ||
                        cached_unit.value("fingerprint", "") != unit_fingerprint ||
                        !cached_unit.contains("records") || !cached_unit["records"].is_array()) {
                        continue;
                    }
                    for (const auto& value : cached_unit["records"]) {
                        records_.push_back(deserialize_record(value));
                    }
                    if (cached_unit.contains("uses") && cached_unit["uses"].is_array()) {
                        for (const auto& value : cached_unit["uses"]) {
                            if (!value.is_object() || !value.contains("specialization") ||
                                !value.contains("use")) {
                                continue;
                            }
                            all_uses[value.value("specialization", "")].push_back(
                                deserialize_use(value["use"])
                            );
                        }
                    }
                    translation_units.push_back(cached_unit);
                    reused = true;
                    break;
                }
            }
            if (reused) {
                continue;
            }

            const auto arguments = tooling_arguments(command, command.source_file);
            if (!supported_compile_language(arguments)) {
                status_ = TemplateSemanticStatus::Failed;
                diagnostic_ = "Unsupported language mode in compilation database command";
                records_.clear();
                return;
            }
            const auto source = project_index_.read_file(command.source_file);
            if (!source.has_value()) {
                status_ = TemplateSemanticStatus::Failed;
                diagnostic_ = "Failed to read a translation unit from the compilation database";
                records_.clear();
                return;
            }

            auto ast = clang::tooling::buildASTFromCodeWithArgs(
                *source,
                arguments,
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
            json serialized_records = json::array();
            for (const auto& record : records) {
                serialized_records.push_back(serialize_record(record));
            }
            json serialized_uses = json::array();
            for (const auto& use : uses) {
                serialized_uses.push_back({
                    {"specialization", use.specialization},
                    {"use", serialize_use(use.use)}
                });
            }
            for (auto& use : uses) {
                all_uses[use.specialization].push_back(std::move(use.use));
            }
            records_.insert(records_.end(), records.begin(), records.end());

            translation_units.push_back({
                {"source_file", command.source_file.generic_string()},
                {"fingerprint", unit_fingerprint},
                {"records", std::move(serialized_records)},
                {"uses", std::move(serialized_uses)}
            });
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
            existing.has_explicit_instantiation_declaration =
                existing.has_explicit_instantiation_declaration || record.has_explicit_instantiation_declaration;
            existing.has_external_linkage = existing.has_external_linkage || record.has_external_linkage;
            existing.has_dependent_arguments = existing.has_dependent_arguments || record.has_dependent_arguments;
            existing.has_dependent_use_context =
                existing.has_dependent_use_context || record.has_dependent_use_context;
            existing.has_unsupported_scope = existing.has_unsupported_scope || record.has_unsupported_scope;
            existing.has_unsupported_function_form =
                existing.has_unsupported_function_form || record.has_unsupported_function_form;
            existing.has_unsupported_variable_form =
                existing.has_unsupported_variable_form || record.has_unsupported_variable_form;
            if (existing.declaration_kind == "function" && record.declaration_kind == "function" &&
                !existing.canonical_extern_declaration.empty() &&
                !record.canonical_extern_declaration.empty() &&
                existing.canonical_extern_declaration != record.canonical_extern_declaration) {
                // Trace signatures may omit overload parameter types. Distinct
                // overloads must never collapse into one edit candidate.
                existing.has_unsupported_function_form = true;
            }
            if (existing.declaration_file.empty()) {
                existing.declaration_file = record.declaration_file;
            }
            if (existing.declaration_line == 0) {
                existing.declaration_line = record.declaration_line;
                existing.declaration_column = record.declaration_column;
                existing.declaration_end_line = record.declaration_end_line;
                existing.declaration_end_column = record.declaration_end_column;
                existing.declaration_end_offset = record.declaration_end_offset;
            }
            if (existing.canonical_extern_declaration.empty()) {
                existing.canonical_extern_declaration = record.canonical_extern_declaration;
                existing.canonical_explicit_definition = record.canonical_explicit_definition;
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
                               candidate.requires_complete_type == use.requires_complete_type &&
                               candidate.in_dependent_context == use.in_dependent_context;
                    }
                );
                if (duplicate == existing.uses.end()) {
                    existing.uses.push_back(std::move(use));
                }
            }
        }

        for (auto& [specialization, uses] : all_uses) {
            const auto record_it = record_indices.find(specialization);
            if (record_it == record_indices.end()) {
                continue;
            }
            auto& record = merged[record_it->second];
            for (auto& use : uses) {
                const auto duplicate = std::ranges::find_if(
                    record.uses,
                    [&use](const TemplateSemanticUse& candidate) {
                        return candidate.source_file == use.source_file &&
                               candidate.kind == use.kind &&
                               candidate.requires_complete_type == use.requires_complete_type &&
                               candidate.in_dependent_context == use.in_dependent_context;
                    }
                );
                if (duplicate == record.uses.end()) {
                    record.uses.push_back(std::move(use));
                    const auto& source_file = record.uses.back().source_file;
                    if (std::ranges::find(record.use_files, source_file) == record.use_files.end()) {
                        record.use_files.push_back(source_file);
                    }
                }
            }
        }

        for (auto& record : merged) {
            record.has_dependent_use_context = record.has_dependent_use_context ||
                std::ranges::any_of(record.uses, [](const TemplateSemanticUse& use) {
                    return use.in_dependent_context;
                });
            record.has_single_explicit_definition = record.explicit_definition_files.size() == 1;
        }
        records_ = std::move(merged);

        if (!cache_path.empty()) {
            std::error_code ec;
            fs::create_directories(cache_path.parent_path(), ec);
            if (!ec) {
                json cache;
                cache["schema"] = "bha-template-semantic-index-v11";
                cache["fingerprint"] = fingerprint;
                cache["records"] = json::array();
                for (const auto& record : records_) {
                    cache["records"].push_back(serialize_record(record));
                }
                cache["translation_units"] = std::move(translation_units);
                std::ofstream output(cache_path);
                if (output) {
                    output << cache.dump(2);
                }
            }
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

    const TemplateSemanticRecord* TemplateSemanticIndex::find_exact(
        const std::string_view specialization
    ) const noexcept {
        const auto it = std::ranges::find(records_, specialization, &TemplateSemanticRecord::specialization);
        return it == records_.end() ? nullptr : &*it;
    }

}  // namespace bha::suggestions
