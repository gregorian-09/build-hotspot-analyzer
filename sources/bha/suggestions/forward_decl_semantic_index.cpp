#include "bha/suggestions/forward_decl_semantic_index.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string_view>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Decl.h>
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
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/VirtualFileSystem.h>
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

        std::vector<ForwardDeclSemanticNamespace> declaration_namespaces(
            const clang::DeclContext* context,
            bool& unsupported
        ) {
            std::vector<ForwardDeclSemanticNamespace> result;
            for (const auto* current = context; current != nullptr; current = current->getParent()) {
                const auto* namespace_decl = llvm::dyn_cast<clang::NamespaceDecl>(current);
                if (!namespace_decl) {
                    if (!current->isTranslationUnit()) {
                        unsupported = true;
                    }
                    break;
                }
                if (namespace_decl->isAnonymousNamespace() || namespace_decl->hasAttrs() ||
                    !namespace_decl->getIdentifier()) {
                    unsupported = true;
                    break;
                }
                result.push_back({
                    namespace_decl->getNameAsString(),
                    namespace_decl->isInline()
                });
            }
            std::ranges::reverse(result);
            return result;
        }

        bool same_namespace_context(
            const std::vector<ForwardDeclSemanticNamespace>& left,
            const std::vector<ForwardDeclSemanticNamespace>& right
        ) {
            if (left.size() != right.size()) {
                return false;
            }
            return std::ranges::equal(left, right, [](const auto& left_namespace, const auto& right_namespace) {
                return left_namespace.name == right_namespace.name &&
                    left_namespace.inline_namespace == right_namespace.inline_namespace;
            });
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
                record.unqualified_name = canonical->getNameAsString();
                record.keyword = record_keyword(*canonical);
                record.complete_definition = true;
                record.template_declaration = declaration->getDescribedClassTemplate() != nullptr;
                record.macro_generated = source_manager_.isMacroBodyExpansion(location) ||
                    source_manager_.isMacroArgExpansion(location);
                record.namespaces = declaration_namespaces(context, record.unsupported_scope);
                record.unsupported_scope = record.unsupported_scope ||
                    !file_or_namespace_scope || record.qualified_name.empty();
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

            bool VisitTypedefNameDecl(clang::TypedefNameDecl* declaration) {
                if (declaration) {
                    record_use(
                        declaration->getUnderlyingType(),
                        declaration->getBeginLoc(),
                        false,
                        declaration->getDeclContext()->isDependentContext(),
                        true
                    );
                }
                return true;
            }

            bool VisitMemberExpr(clang::MemberExpr* expression) {
                if (expression && expression->getBase()) {
                    record_use(expression->getBase()->getType(), expression->getBeginLoc(), true);
                }
                return true;
            }

            bool VisitCXXNamedCastExpr(clang::CXXNamedCastExpr* expression) {
                if (expression) {
                    record_use(expression->getTypeAsWritten(), expression->getBeginLoc(), true);
                    if (expression->getSubExpr()) {
                        record_use(expression->getSubExpr()->getType(), expression->getBeginLoc(), true);
                    }
                }
                return true;
            }

            bool VisitCStyleCastExpr(clang::CStyleCastExpr* expression) {
                if (expression) {
                    record_use(
                        expression->getTypeAsWritten(),
                        expression->getBeginLoc(),
                        requires_complete_object_type(expression->getTypeAsWritten())
                    );
                    if (expression->getSubExpr()) {
                        const auto type = expression->getSubExpr()->getType();
                        record_use(
                            type,
                            expression->getBeginLoc(),
                            requires_complete_object_type(type)
                        );
                    }
                }
                return true;
            }

            bool VisitCXXThrowExpr(clang::CXXThrowExpr* expression) {
                if (expression && expression->getSubExpr()) {
                    const auto type = expression->getSubExpr()->getType();
                    record_use(
                        type,
                        expression->getBeginLoc(),
                        requires_complete_object_type(type)
                    );
                }
                return true;
            }

            bool VisitCXXTypeidExpr(clang::CXXTypeidExpr* expression) {
                if (expression) {
                    if (expression->isTypeOperand()) {
                        record_use(
                            expression->getTypeOperandSourceInfo()->getType(),
                            expression->getBeginLoc(),
                            true
                        );
                    } else if (expression->getExprOperand()) {
                        record_use(expression->getExprOperand()->getType(), expression->getBeginLoc(), true);
                    }
                }
                return true;
            }

            bool VisitUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* expression) {
                if (expression && expression->isArgumentType()) {
                    record_use(expression->getArgumentType(), expression->getBeginLoc(), true);
                } else if (expression && expression->getArgumentExpr()) {
                    record_use(expression->getArgumentExpr()->getType(), expression->getBeginLoc(), true);
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
            static bool requires_complete_object_type(const clang::QualType type) {
                return !type.isNull() && !type->isPointerType() && !type->isReferenceType();
            }

            void record_use(
                clang::QualType type,
                const clang::SourceLocation location,
                const bool force_complete,
                const bool in_dependent_context = false,
                const bool through_alias = false
            ) {
                if (type.isNull()) {
                    return;
                }
                const fs::path use_file = spelling_path(source_manager_, location);
                if (use_file.empty() || use_file == header_) {
                    return;
                }
                const bool macro_expanded = source_manager_.isMacroBodyExpansion(location) ||
                    source_manager_.isMacroArgExpansion(location);
                collect_record_uses(
                    type,
                    use_file,
                    force_complete || (!type->isPointerType() && !type->isReferenceType()),
                    in_dependent_context,
                    false,
                    macro_expanded,
                    through_alias
                );
            }

            void collect_record_uses(
                clang::QualType type,
                const fs::path& use_file,
                const bool requires_complete,
                const bool in_dependent_context,
                const bool through_template,
                const bool macro_expanded,
                const bool inherited_alias
            ) {
                if (type.isNull()) {
                    return;
                }
                const auto* typedef_type = type->getAs<clang::TypedefType>();
                const bool through_alias = inherited_alias || typedef_type != nullptr;
                if (typedef_type != nullptr) {
                    collect_record_uses(
                        typedef_type->desugar(),
                        use_file,
                        requires_complete,
                        in_dependent_context,
                        through_template,
                        macro_expanded,
                        true
                    );
                    return;
                }
                if (type->isPointerType() || type->isReferenceType()) {
                    collect_record_uses(
                        type->getPointeeType(),
                        use_file,
                        requires_complete,
                        in_dependent_context,
                        through_template,
                        macro_expanded,
                        through_alias
                    );
                    return;
                }
                if (const auto* array = type->getAsArrayTypeUnsafe()) {
                    collect_record_uses(
                        array->getElementType(),
                        use_file,
                        true,
                        in_dependent_context,
                        through_template,
                        macro_expanded,
                        through_alias
                    );
                    return;
                }
                if (const auto* template_type = type->getAs<clang::TemplateSpecializationType>()) {
                    const bool nested_template = true;
                    if (const auto* template_decl = template_type->getAsCXXRecordDecl()) {
                        record_if_from_header(
                            template_decl,
                            use_file,
                            requires_complete,
                            in_dependent_context,
                            nested_template,
                            macro_expanded,
                            through_alias
                        );
                    }
                    for (const auto& argument : template_type->template_arguments()) {
                        if (argument.getKind() == clang::TemplateArgument::Type) {
                            collect_record_uses(
                                argument.getAsType(),
                                use_file,
                                requires_complete,
                                in_dependent_context || argument.getAsType()->isInstantiationDependentType(),
                                nested_template,
                                macro_expanded,
                                through_alias
                            );
                        }
                    }
                    return;
                }
                if (const auto* record_type = type->getAs<clang::RecordType>()) {
                    if (const auto* declaration = llvm::dyn_cast<clang::CXXRecordDecl>(record_type->getDecl())) {
                        record_if_from_header(
                            declaration,
                            use_file,
                            requires_complete,
                            in_dependent_context || type->isInstantiationDependentType(),
                            through_template,
                            macro_expanded,
                            through_alias
                        );
                    }
                }
            }

            void record_if_from_header(
                const clang::CXXRecordDecl* declaration,
                const fs::path& use_file,
                const bool requires_complete,
                const bool in_dependent_context,
                const bool through_template,
                const bool macro_expanded,
                const bool through_alias
            ) {
                if (!declaration || !declaration->getCanonicalDecl()) {
                    return;
                }
                if (spelling_path(source_manager_, declaration->getCanonicalDecl()->getLocation()) != header_) {
                    return;
                }
                const std::string name = declaration->getCanonicalDecl()->getQualifiedNameAsString();
                for (auto& record : records_) {
                    if (record.qualified_name == name) {
                        record.uses.push_back({
                            use_file,
                            requires_complete,
                            in_dependent_context,
                            through_alias,
                            through_template,
                            macro_expanded
                        });
                    }
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
                llvm::StringRef filename,
                bool is_angled,
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
                    filename.str(),
                    is_angled,
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

        class SyntaxValidationAction final : public clang::ASTFrontendAction {
        public:
            explicit SyntaxValidationAction(bool& had_errors) : had_errors_(had_errors) {}

            std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
                clang::CompilerInstance& compiler,
                llvm::StringRef
            ) override {
                compiler_ = &compiler;
                return std::make_unique<clang::ASTConsumer>();
            }

            void EndSourceFileAction() override {
                had_errors_ = compiler_ == nullptr || compiler_->getDiagnostics().hasErrorOccurred();
            }

        private:
            bool& had_errors_;
            clang::CompilerInstance* compiler_ = nullptr;
        };

        class IncludeRangeCollector final : public clang::PPCallbacks {
        public:
            IncludeRangeCollector(
                clang::SourceManager& source_manager,
                const fs::path& source_file,
                const std::size_t line,
                const std::string_view spelling,
                std::optional<clang::tooling::Replacement>& replacement
            )
                : source_manager_(source_manager),
                  source_file_(source_file),
                  line_(line),
                  spelling_(spelling),
                  replacement_(replacement) {}

            void InclusionDirective(
                clang::SourceLocation hash_location,
                const clang::Token&,
                llvm::StringRef filename,
                bool,
                clang::CharSourceRange filename_range,
                clang::OptionalFileEntryRef,
                llvm::StringRef,
                llvm::StringRef,
                const clang::Module*,
                clang::SrcMgr::CharacteristicKind
            ) override {
                if (replacement_.has_value() ||
                    spelling_path(source_manager_, hash_location) != source_file_ ||
                    source_manager_.getSpellingLineNumber(hash_location) - 1 != line_ ||
                    filename != spelling_) {
                    return;
                }
                replacement_.emplace(
                    source_manager_,
                    clang::CharSourceRange::getCharRange(hash_location, filename_range.getEnd()),
                    ""
                );
            }

        private:
            clang::SourceManager& source_manager_;
            fs::path source_file_;
            std::size_t line_;
            std::string spelling_;
            std::optional<clang::tooling::Replacement>& replacement_;
        };

        class IncludeValidationAction final : public clang::ASTFrontendAction {
        public:
            IncludeValidationAction(
                const fs::path& source_file,
                const std::size_t line,
                const std::string_view spelling,
                std::optional<clang::tooling::Replacement>& replacement,
                bool& had_errors
            )
                : source_file_(source_file),
                  line_(line),
                  spelling_(spelling),
                  replacement_(replacement),
                  had_errors_(had_errors) {}

            std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
                clang::CompilerInstance& compiler,
                llvm::StringRef
            ) override {
                compiler_ = &compiler;
                compiler.getPreprocessor().addPPCallbacks(
                    std::make_unique<IncludeRangeCollector>(
                        compiler.getSourceManager(),
                        source_file_,
                        line_,
                        spelling_,
                        replacement_
                    )
                );
                return std::make_unique<clang::ASTConsumer>();
            }

            void EndSourceFileAction() override {
                had_errors_ = compiler_ == nullptr || compiler_->getDiagnostics().hasErrorOccurred();
            }

        private:
            fs::path source_file_;
            std::size_t line_;
            std::string spelling_;
            std::optional<clang::tooling::Replacement>& replacement_;
            bool& had_errors_;
            clang::CompilerInstance* compiler_ = nullptr;
        };

        llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> validation_filesystem(
            const fs::path& source_file,
            const std::string& source,
            const fs::path* generated_file = nullptr,
            const std::string_view* generated_content = nullptr
        ) {
            auto memory = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
            memory->addFile(
                source_file.string(),
                0,
                llvm::MemoryBuffer::getMemBufferCopy(source, source_file.string())
            );
            if (generated_file != nullptr && generated_content != nullptr) {
                memory->addFile(
                    generated_file->string(),
                    0,
                    llvm::MemoryBuffer::getMemBufferCopy(
                        *generated_content,
                        generated_file->string()
                    )
                );
            }
            auto overlay = llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(
                llvm::vfs::getRealFileSystem()
            );
            overlay->pushOverlay(memory);
            return overlay;
        }
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
                existing->declaration_shape_conflict = existing->declaration_shape_conflict ||
                    existing->keyword != record.keyword ||
                    !same_namespace_context(existing->namespaces, record.namespaces);
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

    bool validate_forward_decl_replacements(
        ProjectIndex& project_index,
        const std::vector<CompilationUnit>& commands,
        const std::vector<ForwardDeclSemanticInclude>& includes,
        const std::string_view replacement_text,
        std::string& diagnostic
    ) {
#if !BHA_HAVE_CLANG_TOOLING
        (void)project_index;
        (void)commands;
        (void)includes;
        (void)replacement_text;
        diagnostic = "Clang LibTooling is required for forward-declaration validation";
        return false;
#else
        std::map<std::string, clang::tooling::Replacements> replacements_by_file;
        for (const auto& include : includes) {
            const auto file = project_index.resolve(include.including_file).lexically_normal();
            const clang::tooling::Replacement replacement(
                file.string(),
                static_cast<unsigned>(include.offset),
                static_cast<unsigned>(include.length),
                replacement_text
            );
            if (auto error = replacements_by_file[file.generic_string()].add(replacement)) {
                diagnostic = llvm::toString(std::move(error));
                return false;
            }
        }

        for (const auto& command : commands) {
            const auto file = project_index.resolve(command.source_file).lexically_normal();
            const auto replacements = replacements_by_file.find(file.generic_string());
            if (replacements == replacements_by_file.end()) {
                continue;
            }
            const auto source = project_index.read_file(file);
            if (!source.has_value()) {
                diagnostic = "Failed to read an affected translation unit for validation";
                return false;
            }
            auto modified = clang::tooling::applyAllReplacements(*source, replacements->second);
            if (!modified) {
                diagnostic = llvm::toString(modified.takeError());
                return false;
            }
            bool had_errors = false;
            if (!clang::tooling::runToolOnCodeWithArgs(
                    std::make_unique<SyntaxValidationAction>(had_errors),
                    *modified,
                    validation_filesystem(file, *modified),
                    tooling_arguments(command),
                    file.string()
                ) || had_errors) {
                diagnostic = "Clang rejected the forward-declaration replacement in " + file.string();
                return false;
            }
        }
        return true;
#endif
    }

    bool validate_header_split_replacements(
        ProjectIndex& project_index,
        const std::vector<CompilationUnit>& commands,
        const std::vector<ForwardDeclSemanticInclude>& includes,
        const std::string_view replacement_text,
        const fs::path& generated_file,
        const std::string_view generated_content,
        std::string& diagnostic
    ) {
#if !BHA_HAVE_CLANG_TOOLING
        (void)project_index;
        (void)commands;
        (void)includes;
        (void)replacement_text;
        (void)generated_file;
        (void)generated_content;
        diagnostic = "Clang LibTooling is required for header-split validation";
        return false;
#else
        std::map<std::string, clang::tooling::Replacements> replacements_by_file;
        for (const auto& include : includes) {
            const auto file = project_index.resolve(include.including_file).lexically_normal();
            const clang::tooling::Replacement replacement(
                file.string(),
                static_cast<unsigned>(include.offset),
                static_cast<unsigned>(include.length),
                replacement_text
            );
            if (auto error = replacements_by_file[file.generic_string()].add(replacement)) {
                diagnostic = llvm::toString(std::move(error));
                return false;
            }
        }

        const auto generated = project_index.resolve(generated_file).lexically_normal();
        for (const auto& command : commands) {
            const auto file = project_index.resolve(command.source_file).lexically_normal();
            const auto replacements = replacements_by_file.find(file.generic_string());
            if (replacements == replacements_by_file.end()) {
                continue;
            }
            const auto source = project_index.read_file(file);
            if (!source.has_value()) {
                diagnostic = "Failed to read an affected translation unit for validation";
                return false;
            }
            auto modified = clang::tooling::applyAllReplacements(*source, replacements->second);
            if (!modified) {
                diagnostic = llvm::toString(modified.takeError());
                return false;
            }
            bool had_errors = false;
            if (!clang::tooling::runToolOnCodeWithArgs(
                    std::make_unique<SyntaxValidationAction>(had_errors),
                    *modified,
                    validation_filesystem(
                        file,
                        *modified,
                        &generated,
                        &generated_content
                    ),
                    tooling_arguments(command),
                    file.string(),
                    "bha-header-split"
                ) || had_errors) {
                diagnostic = "Clang rejected the header-split replacement in " + file.string();
                return false;
            }
        }
        return true;
#endif
    }

    bool validate_include_removal(
        ProjectIndex& project_index,
        const CompilationUnit& command,
        const fs::path& source_file,
        const std::size_t include_line,
        const std::string_view include_spelling,
        std::string& diagnostic
    ) {
#if !BHA_HAVE_CLANG_TOOLING
        (void)project_index;
        (void)command;
        (void)source_file;
        (void)include_line;
        (void)include_spelling;
        diagnostic = "Clang LibTooling is required for include-removal validation";
        return false;
#else
        const auto source = project_index.read_file(source_file);
        if (!source.has_value()) {
            diagnostic = "Failed to read the translation unit for include-removal validation";
            return false;
        }

        std::optional<clang::tooling::Replacement> replacement;
        bool had_errors = false;
        if (!clang::tooling::runToolOnCodeWithArgs(
                std::make_unique<IncludeValidationAction>(
                    source_file,
                    include_line,
                    include_spelling,
                    replacement,
                    had_errors
                ),
                *source,
                tooling_arguments(command),
                source_file.string()
            ) || had_errors || !replacement.has_value()) {
            diagnostic = "Clang could not resolve the diagnostic include range";
            return false;
        }

        clang::tooling::Replacements replacements;
        if (auto error = replacements.add(*replacement)) {
            diagnostic = llvm::toString(std::move(error));
            return false;
        }
        auto modified = clang::tooling::applyAllReplacements(*source, replacements);
        if (!modified) {
            diagnostic = llvm::toString(modified.takeError());
            return false;
        }

        bool modified_had_errors = false;
        if (!clang::tooling::runToolOnCodeWithArgs(
                std::make_unique<SyntaxValidationAction>(modified_had_errors),
                *modified,
                validation_filesystem(source_file, *modified),
                tooling_arguments(command),
                source_file.string(),
                "bha-include-removal"
            ) || modified_had_errors) {
            diagnostic = "Clang rejected the include removal in " + source_file.string();
            return false;
        }
        return true;
#endif
    }

}  // namespace bha::suggestions
