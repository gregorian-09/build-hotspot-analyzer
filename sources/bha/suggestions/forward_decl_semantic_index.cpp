#include "bha/suggestions/forward_decl_semantic_index.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string_view>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Tooling/Core/Replacement.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/raw_ostream.h>
#endif

namespace bha::suggestions {
    namespace {

#if BHA_HAVE_CLANG_TOOLING
        fs::path spelling_path(
            const clang::SourceManager& source_manager,
            const clang::SourceLocation location
        ) {
            if (location.isInvalid()) {
                return {};
            }
            return fs::path(source_manager.getFilename(source_manager.getSpellingLoc(location)).str())
                .lexically_normal();
        }

        std::string record_keyword(const clang::CXXRecordDecl& declaration) {
            switch (declaration.getTagKind()) {
                case clang::TagTypeKind::Struct:
                    return "struct";
                case clang::TagTypeKind::Union:
                    return "union";
                case clang::TagTypeKind::Class:
                case clang::TagTypeKind::Interface:
                    return "class";
                case clang::TagTypeKind::Enum:
                    return {};
            }
            return {};
        }

        std::vector<std::string> tooling_arguments(const CompilationUnit& command) {
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
                if (fs::path(argument).lexically_normal() == command.source_file.lexically_normal()) {
                    continue;
                }
                arguments.push_back(argument);
            }
            arguments.push_back("-fsyntax-only");
            return arguments;
        }

        class ForwardDeclVisitor final : public clang::RecursiveASTVisitor<ForwardDeclVisitor> {
        public:
            ForwardDeclVisitor(
                clang::ASTContext& context,
                const fs::path& header
            )
                : source_manager_(context.getSourceManager()),
                  header_(header.lexically_normal()) {}

            bool VisitCXXRecordDecl(clang::CXXRecordDecl* declaration) {
                if (!declaration || !declaration->getIdentifier() ||
                    !declaration->isThisDeclarationADefinition()) {
                    return true;
                }
                const auto location = declaration->getLocation();
                if (spelling_path(source_manager_, location) != header_) {
                    return true;
                }
                const auto* canonical = declaration->getCanonicalDecl();
                if (!canonical || !canonical->getIdentifier()) {
                    return true;
                }
                const auto* context = declaration->getDeclContext();
                const bool file_or_namespace_scope = context &&
                    (context->isFileContext() || llvm::isa<clang::NamespaceDecl>(context));
                ForwardDeclSemanticRecord record;
                record.declaration_file = header_;
                record.qualified_name = canonical->getQualifiedNameAsString();
                record.keyword = record_keyword(*canonical);
                record.complete_definition = true;
                record.template_declaration = declaration->getDescribedClassTemplate() != nullptr;
                record.macro_generated = source_manager_.isMacroBodyExpansion(location) ||
                    source_manager_.isMacroArgExpansion(location);
                record.unsupported_scope = !file_or_namespace_scope || record.qualified_name.empty();
                records_.push_back(std::move(record));
                return true;
            }

            bool VisitVarDecl(clang::VarDecl* declaration) {
                if (declaration) {
                    record_use(
                        declaration->getType(),
                        declaration->getBeginLoc(),
                        false,
                        declaration->getDeclContext()->isDependentContext()
                    );
                }
                return true;
            }

            bool VisitFieldDecl(clang::FieldDecl* declaration) {
                if (declaration) {
                    record_use(
                        declaration->getType(),
                        declaration->getBeginLoc(),
                        false,
                        declaration->getDeclContext()->isDependentContext()
                    );
                }
                return true;
            }

            bool VisitParmVarDecl(clang::ParmVarDecl* declaration) {
                if (declaration) {
                    record_use(
                        declaration->getType(),
                        declaration->getBeginLoc(),
                        false,
                        declaration->getDeclContext()->isDependentContext()
                    );
                }
                return true;
            }

            bool VisitFunctionDecl(clang::FunctionDecl* declaration) {
                if (declaration) {
                    record_use(
                        declaration->getReturnType(),
                        declaration->getBeginLoc(),
                        false,
                        declaration->getDeclContext()->isDependentContext()
                    );
                }
                return true;
            }

            bool VisitUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* expression) {
                if (expression) {
                    record_use(expression->getArgumentType(), expression->getBeginLoc(), true);
                }
                return true;
            }

            bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* expression) {
                if (expression && expression->getArgument()) {
                    record_use(expression->getArgument()->getType(), expression->getBeginLoc(), true);
                }
                return true;
            }

            bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr* expression) {
                if (expression && expression->getImplicitObjectArgument()) {
                    record_use(
                        expression->getImplicitObjectArgument()->getType(),
                        expression->getBeginLoc(),
                        true
                    );
                }
                return true;
            }

            bool VisitCXXBaseSpecifier(clang::CXXBaseSpecifier* base) {
                if (base) {
                    record_use(base->getType(), base->getBeginLoc(), true);
                }
                return true;
            }

            bool VisitCXXNewExpr(clang::CXXNewExpr* expression) {
                if (expression) {
                    record_use(expression->getAllocatedType(), expression->getBeginLoc(), true);
                }
                return true;
            }

            bool VisitCXXConstructExpr(clang::CXXConstructExpr* expression) {
                if (expression) {
                    record_use(expression->getType(), expression->getBeginLoc(), true);
                }
                return true;
            }

            std::vector<ForwardDeclSemanticRecord> take_records() {
                return std::move(records_);
            }

        private:
            void record_use(
                clang::QualType type,
                const clang::SourceLocation location,
                const bool force_complete,
                const bool in_dependent_context = false
            ) {
                if (type.isNull()) {
                    return;
                }
                const fs::path use_file = spelling_path(source_manager_, location);
                if (use_file.empty() || use_file == header_) {
                    return;
                }
                const bool requires_complete = force_complete ||
                    (!type->isPointerType() && !type->isReferenceType());
                const bool macro_expanded = source_manager_.isMacroBodyExpansion(location) ||
                    source_manager_.isMacroArgExpansion(location);
                bool through_alias = false;
                bool through_template = false;
                if (type->getAs<clang::TypedefType>() != nullptr) {
                    through_alias = true;
                }
                if (const auto* template_type = type->getAs<clang::TemplateSpecializationType>()) {
                    for (const auto& argument : template_type->template_arguments()) {
                        if (argument.getKind() != clang::TemplateArgument::Type) {
                            continue;
                        }
                        const auto argument_type = argument.getAsType();
                        const auto* argument_record = argument_type->getAs<clang::RecordType>();
                        if (argument_record && spelling_path(
                                source_manager_,
                                argument_record->getDecl()->getCanonicalDecl()->getLocation()
                            ) == header_) {
                            through_template = true;
                        }
                    }
                }
                while (type->isPointerType() || type->isReferenceType()) {
                    type = type->getPointeeType();
                }
                if (type->getAs<clang::TypedefType>() != nullptr) {
                    through_alias = true;
                }
                const auto* record_type = type->getAs<clang::RecordType>();
                const auto* declaration = record_type
                    ? llvm::dyn_cast<clang::CXXRecordDecl>(record_type->getDecl())
                    : nullptr;
                if (!declaration || !declaration->getCanonicalDecl()) {
                    return;
                }
                const auto canonical_location = declaration->getCanonicalDecl()->getLocation();
                if (spelling_path(source_manager_, canonical_location) != header_) {
                    return;
                }
                const std::string name = declaration->getCanonicalDecl()->getQualifiedNameAsString();
                for (auto& record : records_) {
                    if (record.qualified_name != name) {
                        continue;
                    }
                    record.uses.push_back({
                        use_file,
                        requires_complete,
                        in_dependent_context || type->isInstantiationDependentType(),
                        through_alias,
                        through_template,
                        macro_expanded
                    });
                }
            }

            clang::SourceManager& source_manager_;
            fs::path header_;
            std::vector<ForwardDeclSemanticRecord> records_;
        };

        class IncludeCollector final : public clang::PPCallbacks {
        public:
            IncludeCollector(
                clang::SourceManager& source_manager,
                const fs::path& target_header,
                std::vector<ForwardDeclSemanticInclude>& includes
            )
                : source_manager_(source_manager),
                  target_header_(target_header.lexically_normal()),
                  includes_(includes) {}

            void InclusionDirective(
                clang::SourceLocation hash_location,
                const clang::Token&,
                llvm::StringRef,
                bool,
                clang::CharSourceRange filename_range,
                clang::OptionalFileEntryRef file,
                llvm::StringRef,
                llvm::StringRef,
                const clang::Module*,
                clang::SrcMgr::CharacteristicKind
            ) override {
                if (!file.has_value()) {
                    return;
                }
                const fs::path included = fs::path(file->getName().str()).lexically_normal();
                if (included != target_header_) {
                    return;
                }
                const fs::path including = spelling_path(source_manager_, hash_location);
                if (including.empty()) {
                    return;
                }
                const clang::tooling::Replacement typed_range(
                    source_manager_,
                    clang::CharSourceRange::getCharRange(hash_location, filename_range.getEnd()),
                    ""
                );
                includes_.push_back({
                    including,
                    included,
                    typed_range.getOffset(),
                    typed_range.getLength(),
                    source_manager_.getSpellingLineNumber(hash_location) - 1,
                    source_manager_.getSpellingColumnNumber(hash_location) - 1,
                    source_manager_.getSpellingColumnNumber(filename_range.getEnd()) - 1
                });
            }

        private:
            clang::SourceManager& source_manager_;
            fs::path target_header_;
            std::vector<ForwardDeclSemanticInclude>& includes_;
        };

        class SemanticIndexConsumer final : public clang::ASTConsumer {
        public:
            SemanticIndexConsumer(
                const fs::path& header,
                std::vector<ForwardDeclSemanticRecord>& records
            )
                : header_(header), records_(records) {}

            void HandleTranslationUnit(clang::ASTContext& context) override {
                ForwardDeclVisitor visitor(context, header_);
                visitor.TraverseDecl(context.getTranslationUnitDecl());
                auto parsed_records = visitor.take_records();
                records_.insert(
                    records_.end(),
                    std::make_move_iterator(parsed_records.begin()),
                    std::make_move_iterator(parsed_records.end())
                );
            }

        private:
            fs::path header_;
            std::vector<ForwardDeclSemanticRecord>& records_;
        };

        class SemanticIndexAction final : public clang::ASTFrontendAction {
        public:
            SemanticIndexAction(
                const fs::path& header,
                std::vector<ForwardDeclSemanticRecord>& records,
                std::vector<ForwardDeclSemanticInclude>& includes,
                bool& had_errors
            )
                : header_(header), records_(records), includes_(includes), had_errors_(had_errors) {}

            std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
                clang::CompilerInstance& compiler,
                llvm::StringRef
            ) override {
                compiler_ = &compiler;
                compiler.getPreprocessor().addPPCallbacks(
                    std::make_unique<IncludeCollector>(
                        compiler.getSourceManager(),
                        header_,
                        includes_
                    )
                );
                return std::make_unique<SemanticIndexConsumer>(
                    header_,
                    records_
                );
            }

            void EndSourceFileAction() override {
                had_errors_ = compiler_ == nullptr || compiler_->getDiagnostics().hasErrorOccurred();
            }

        private:
            fs::path header_;
            std::vector<ForwardDeclSemanticRecord>& records_;
            std::vector<ForwardDeclSemanticInclude>& includes_;
            bool& had_errors_;
            clang::CompilerInstance* compiler_ = nullptr;
        };
#endif

    }  // namespace

    ForwardDeclSemanticResult analyze_forward_declarations(
        ProjectIndex& project_index,
        const fs::path& header,
        const std::vector<CompilationUnit>& commands
    ) {
        ForwardDeclSemanticResult result;
#if !BHA_HAVE_CLANG_TOOLING
        (void)project_index;
        (void)header;
        (void)commands;
        result.diagnostic = "Clang LibTooling is required for forward-declaration evidence";
        return result;
#else
        const fs::path normalized_header = project_index.resolve(header).lexically_normal();
        for (const auto& command : commands) {
            const auto source = project_index.read_file(command.source_file);
            if (!source.has_value()) {
                result.diagnostic = "Failed to read a compile-command-backed translation unit";
                return result;
            }
            std::vector<ForwardDeclSemanticRecord> records;
            bool had_errors = false;
            const auto arguments = tooling_arguments(command);
            if (!clang::tooling::runToolOnCodeWithArgs(
                    std::make_unique<SemanticIndexAction>(
                        normalized_header,
                        records,
                        result.includes,
                        had_errors
                    ),
                    *source,
                    arguments,
                    command.source_file.string()
                ) || had_errors) {
                result.diagnostic = "Clang failed to build a diagnostic-free AST and include index";
                result.records.clear();
                result.includes.clear();
                return result;
            }
            for (auto& record : records) {
                auto existing = std::ranges::find_if(
                    result.records,
                    [&](const auto& candidate) {
                        return candidate.qualified_name == record.qualified_name;
                    }
                );
                if (existing == result.records.end()) {
                    result.records.push_back(std::move(record));
                    continue;
                }
                existing->macro_generated = existing->macro_generated || record.macro_generated;
                existing->template_declaration = existing->template_declaration || record.template_declaration;
                existing->unsupported_scope = existing->unsupported_scope || record.unsupported_scope;
                for (auto& use : record.uses) {
                    const bool duplicate = std::ranges::any_of(
                        existing->uses,
                        [&](const auto& candidate) {
                            return candidate.source_file == use.source_file &&
                                candidate.requires_complete_type == use.requires_complete_type &&
                                candidate.in_dependent_context == use.in_dependent_context &&
                                candidate.through_alias == use.through_alias &&
                                candidate.through_template == use.through_template &&
                                candidate.macro_expanded == use.macro_expanded;
                        }
                    );
                    if (!duplicate) {
                        existing->uses.push_back(std::move(use));
                    }
                }
            }
        }
        result.available = !result.records.empty();
        if (!result.available && result.diagnostic.empty()) {
            result.diagnostic = "No AST declaration from the target header was observed";
        }
        return result;
#endif
    }

}  // namespace bha::suggestions
