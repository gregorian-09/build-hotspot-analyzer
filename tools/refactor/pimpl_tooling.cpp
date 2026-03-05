#include "bha/refactor/pimpl_tooling.hpp"
#include "bha/refactor/pimpl_eligibility.hpp"

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "bha/suggestions/pimpl_suggester.hpp"

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Core/Replacement.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/Casting.h>
#endif

namespace bha::refactor {

    namespace fs = std::filesystem;

    namespace {

        void add_diagnostic(
            Result& result,
            const DiagnosticSeverity severity,
            std::string message,
            fs::path file = {},
            const std::size_t line = 0
        ) {
            Diagnostic diagnostic;
            diagnostic.severity = severity;
            diagnostic.message = std::move(message);
            diagnostic.file = std::move(file);
            diagnostic.line = line;
            result.diagnostics.push_back(std::move(diagnostic));
        }

#if BHA_HAVE_CLANG_TOOLING
        std::optional<std::size_t> line_col_to_offset(
            const std::string& content,
            const std::size_t line,
            const std::size_t col
        ) {
            std::size_t current_line = 0;
            std::size_t line_start = 0;

            for (std::size_t index = 0; index < content.size(); ++index) {
                if (current_line == line) {
                    const std::size_t offset = line_start + col;
                    return std::min(offset, content.size());
                }
                if (content[index] == '\n') {
                    ++current_line;
                    line_start = index + 1;
                }
            }

            if (current_line == line) {
                return std::min(line_start + col, content.size());
            }
            if (line > current_line) {
                return content.size();
            }
            return std::nullopt;
        }

        std::optional<Replacement> to_replacement(const TextEdit& edit) {
            std::ifstream in(edit.file, std::ios::binary);
            if (!in) {
                return std::nullopt;
            }
            const std::string content(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>()
            );

            auto start_offset = line_col_to_offset(content, edit.start_line, edit.start_col);
            auto end_offset = line_col_to_offset(content, edit.end_line, edit.end_col);
            if (!start_offset || !end_offset) {
                return std::nullopt;
            }
            if (*start_offset > *end_offset) {
                std::swap(start_offset, end_offset);
            }

            Replacement replacement;
            replacement.file = edit.file;
            replacement.offset = *start_offset;
            replacement.length = *end_offset - *start_offset;
            replacement.replacement_text = edit.new_text;
            return replacement;
        }

        bool is_ast_rewritten_source_line(
            const TextEdit& edit,
            const fs::path& source_file,
            const std::string_view class_name
        ) {
            if (edit.file != source_file) {
                return false;
            }
            if (edit.is_insertion()) {
                return false;
            }
            if (edit.new_text.find("std::make_unique<Impl>()") != std::string::npos) {
                return false;
            }
            if (edit.new_text.find("struct " + std::string(class_name) + "::Impl") != std::string::npos) {
                return false;
            }
            return true;
        }

        std::vector<std::string> load_compile_args_from_db(
            const fs::path& compile_commands_path,
            const fs::path& source_file,
            std::string& working_directory,
            std::string& error_message
        ) {
            using namespace clang::tooling;

            std::string load_error;
            auto database = JSONCompilationDatabase::loadFromFile(
                compile_commands_path.string(),
                load_error,
                JSONCommandLineSyntax::AutoDetect
            );
            if (!database) {
                error_message = load_error.empty()
                    ? "Failed to load compile_commands.json"
                    : load_error;
                return {};
            }

            const fs::path absolute_source = source_file.lexically_normal();
            const fs::path filename_only = source_file.filename();

            std::optional<CompileCommand> selected_command;
            for (const auto& command : database->getAllCompileCommands()) {
                fs::path candidate = command.Filename;
                if (candidate.is_relative()) {
                    candidate = fs::path(command.Directory) / candidate;
                }
                candidate = candidate.lexically_normal();
                if (candidate == absolute_source || candidate.filename() == filename_only) {
                    selected_command = command;
                    break;
                }
            }
            if (!selected_command) {
                error_message = "No compilation database entry matched the requested source file";
                return {};
            }

            const auto& command = *selected_command;
            working_directory = command.Directory;

            std::vector<std::string> args;
            args.reserve(command.CommandLine.size() + 2);
            args.push_back("-working-directory");
            args.push_back(command.Directory);

            for (std::size_t i = 1; i < command.CommandLine.size(); ++i) {
                const std::string& arg = command.CommandLine[i];
                if (arg == "-c" || arg == "-Winvalid-pch") {
                    continue;
                }
                if (arg == "-ftime-trace" || arg.starts_with("-ftime-trace=")) {
                    continue;
                }
                if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ" || arg == "-x") {
                    ++i;
                    continue;
                }
                if (arg.starts_with("-o") && arg.size() > 2) {
                    continue;
                }
                const fs::path candidate = fs::path(arg).lexically_normal();
                if (candidate == absolute_source || candidate.filename() == filename_only) {
                    continue;
                }
                args.push_back(arg);
            }

            return args;
        }

        class FieldReferenceVisitor final : public clang::RecursiveASTVisitor<FieldReferenceVisitor> {
        public:
            FieldReferenceVisitor(
                clang::ASTContext& context,
                const clang::CXXRecordDecl& target_record
            )
                : context_(context)
                , target_record_(target_record) {
                for (const auto* field : target_record_.fields()) {
                    if (field->getAccess() == clang::AS_private) {
                        tracked_fields_.insert(field);
                    }
                }
            }

            bool VisitMemberExpr(clang::MemberExpr* expr) {
                const auto* field = llvm::dyn_cast<clang::FieldDecl>(expr->getMemberDecl());
                if (!field || !tracked_fields_.contains(field)) {
                    return true;
                }

                auto& source_manager = context_.getSourceManager();
                if (!source_manager.isWrittenInMainFile(expr->getExprLoc())) {
                    return true;
                }

                clang::SourceLocation begin_loc;
                clang::SourceLocation end_loc = expr->getMemberLoc();
                if (expr->isImplicitAccess()) {
                    begin_loc = expr->getMemberLoc();
                } else if (const auto* base = expr->getBase()) {
                    const auto* this_expr =
                        llvm::dyn_cast<clang::CXXThisExpr>(base->IgnoreParenImpCasts());
                    if (!this_expr) {
                        return true;
                    }
                    begin_loc = this_expr->getBeginLoc();
                } else {
                    return true;
                }

                const auto begin_spelling = source_manager.getSpellingLoc(begin_loc);
                const auto end_spelling = source_manager.getSpellingLoc(end_loc);
                if (begin_spelling.isInvalid() || end_spelling.isInvalid()) {
                    return true;
                }
                if (!source_manager.isWrittenInSameFile(begin_spelling, end_spelling)) {
                    return true;
                }

                const auto begin = source_manager.getFileOffset(begin_spelling);
                const auto end_token = clang::Lexer::getLocForEndOfToken(
                    end_spelling,
                    0,
                    source_manager,
                    context_.getLangOpts()
                );
                if (end_token.isInvalid()) {
                    return true;
                }
                const auto end = source_manager.getFileOffset(end_token);
                if (end < begin) {
                    return true;
                }

                Replacement replacement;
                replacement.file = source_manager.getFilename(begin_spelling).str();
                replacement.offset = begin;
                replacement.length = end - begin;
                replacement.replacement_text = "pimpl_->" + field->getNameAsString();

                const auto key = replacement.file.string() + ":" + std::to_string(replacement.offset) +
                    ":" + std::to_string(replacement.length);
                if (seen_.insert(key).second) {
                    replacements_.push_back(std::move(replacement));
                }
                return true;
            }

            [[nodiscard]] const std::vector<Replacement>& replacements() const noexcept {
                return replacements_;
            }

        private:
            clang::ASTContext& context_;
            const clang::CXXRecordDecl& target_record_;
            std::unordered_set<const clang::FieldDecl*> tracked_fields_;
            std::unordered_set<std::string> seen_;
            std::vector<Replacement> replacements_;
        };

        class TargetRecordVisitor final : public clang::RecursiveASTVisitor<TargetRecordVisitor> {
        public:
            explicit TargetRecordVisitor(const std::string& class_name)
                : class_name_(class_name) {}

            bool VisitCXXRecordDecl(clang::CXXRecordDecl* decl) {
                if (!decl || !decl->isThisDeclarationADefinition()) {
                    return true;
                }
                const auto qualified = decl->getQualifiedNameAsString();
                if (qualified == class_name_ || decl->getNameAsString() == class_name_) {
                    record_ = decl;
                    return false;
                }
                return true;
            }

            [[nodiscard]] const clang::CXXRecordDecl* record() const noexcept {
                return record_;
            }

        private:
            std::string class_name_;
            const clang::CXXRecordDecl* record_ = nullptr;
        };

        class MethodDefinitionCollector final : public clang::RecursiveASTVisitor<MethodDefinitionCollector> {
        public:
            explicit MethodDefinitionCollector(const clang::CXXRecordDecl& target_record)
                : target_record_(target_record) {}

            bool VisitCXXMethodDecl(clang::CXXMethodDecl* decl) {
                if (!decl || !decl->hasBody() || !decl->isOutOfLine()) {
                    return true;
                }

                const auto* parent = decl->getParent();
                if (!parent) {
                    return true;
                }

                const auto target_name = target_record_.getQualifiedNameAsString();
                const auto parent_name = parent->getQualifiedNameAsString();
                if (parent == &target_record_ || parent->getCanonicalDecl() == target_record_.getCanonicalDecl() ||
                    parent_name == target_name) {
                    methods_.push_back(decl);
                }
                return true;
            }

            [[nodiscard]] const std::vector<clang::CXXMethodDecl*>& methods() const noexcept {
                return methods_;
            }

        private:
            const clang::CXXRecordDecl& target_record_;
            std::vector<clang::CXXMethodDecl*> methods_;
        };

        std::optional<std::vector<Replacement>> build_tooling_field_replacements(
            const PimplRequest& request,
            Result& diagnostics_result
        ) {
            std::string working_directory;
            std::string compile_error;
            auto args = load_compile_args_from_db(
                request.compile_commands_path,
                request.source_file,
                working_directory,
                compile_error
            );
            if (args.empty()) {
                add_diagnostic(diagnostics_result, DiagnosticSeverity::Error, compile_error);
                return std::nullopt;
            }

            std::ifstream in(request.source_file, std::ios::binary);
            if (!in) {
                add_diagnostic(
                    diagnostics_result,
                    DiagnosticSeverity::Error,
                    "Failed to read the requested source file",
                    request.source_file
                );
                return std::nullopt;
            }
            const std::string code(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>()
            );

            auto ast = clang::tooling::buildASTFromCodeWithArgs(
                code,
                args,
                request.source_file.string()
            );
            if (!ast) {
                add_diagnostic(
                    diagnostics_result,
                    DiagnosticSeverity::Error,
                    "Clang LibTooling failed to build an AST for the requested translation unit",
                    request.source_file
                );
                return std::nullopt;
            }

            auto& context = ast->getASTContext();
            TargetRecordVisitor record_visitor(request.class_name);
            record_visitor.TraverseDecl(context.getTranslationUnitDecl());
            const auto* record = record_visitor.record();
            if (!record) {
                add_diagnostic(
                    diagnostics_result,
                    DiagnosticSeverity::Error,
                    "Failed to locate the requested class in the translation unit AST",
                    request.header_file
                );
                return std::nullopt;
            }

            const auto reject_semantic = [&](std::string_view message, fs::path file = {}) {
                diagnostics_result.allow_fallback = false;
                add_diagnostic(
                    diagnostics_result,
                    DiagnosticSeverity::Warning,
                    std::string(message),
                    file.empty() ? request.header_file : std::move(file)
                );
                return std::nullopt;
            };
            PimplEligibilityState eligibility;
            eligibility.has_compile_context = true;
            eligibility.has_macro_generated_class = record->getLocation().isMacroID();
            eligibility.has_template_declaration =
                record->getDescribedClassTemplate() != nullptr ||
                record->getTemplateSpecializationKind() != clang::TSK_Undeclared;
            eligibility.has_inheritance = record->getNumBases() > 0;

            for (const auto* field : record->fields()) {
                if (!field || field->getAccess() != clang::AS_private) {
                    continue;
                }
                ++eligibility.private_data_members;
                if (field->getLocation().isMacroID()) {
                    eligibility.has_macro_generated_private_declarations = true;
                }
            }

            for (const auto* method_decl : record->methods()) {
                if (!method_decl || method_decl->isImplicit()) {
                    continue;
                }
                if (method_decl->isVirtual()) {
                    eligibility.has_virtual_members = true;
                }
                if (method_decl->getAccess() == clang::AS_private &&
                    method_decl->hasInlineBody() &&
                    !method_decl->isDefaulted()) {
                    eligibility.has_private_inline_method_bodies = true;
                    eligibility.has_private_methods = true;
                }
                if (method_decl->getAccess() == clang::AS_private) {
                    eligibility.has_private_methods = true;
                }
                if (method_decl->getLocation().isMacroID()) {
                    eligibility.has_macro_generated_private_declarations = true;
                }
            }

            FieldReferenceVisitor field_visitor(context, *record);
            MethodDefinitionCollector method_collector(*record);
            method_collector.TraverseDecl(context.getTranslationUnitDecl());
            for (const auto* method : method_collector.methods()) {
                if (!method) {
                    continue;
                }
                const bool is_explicit_copy_ctor =
                    llvm::isa<clang::CXXConstructorDecl>(method) &&
                    llvm::cast<clang::CXXConstructorDecl>(method)->isCopyConstructor() &&
                    !method->isImplicit() &&
                    !method->isDefaulted();
                const bool is_explicit_copy_assign =
                    method->isCopyAssignmentOperator() &&
                    !method->isImplicit() &&
                    !method->isDefaulted();
                if (is_explicit_copy_ctor || is_explicit_copy_assign) {
                    eligibility.has_explicit_copy_definition = true;
                    break;
                }
            }

            if (const auto blocker = first_pimpl_eligibility_blocker(eligibility)) {
                return reject_semantic(pimpl_blocker_message(*blocker));
            }

            for (auto* method : method_collector.methods()) {
                const auto& source_manager = context.getSourceManager();
                if (!source_manager.isWrittenInMainFile(method->getLocation())) {
                    continue;
                }
                field_visitor.TraverseStmt(method->getBody());
            }

            return field_visitor.replacements();
        }
#endif

    }  // namespace

    bool clang_tooling_available() noexcept {
#if BHA_HAVE_CLANG_TOOLING
        return true;
#else
        return false;
#endif
    }

    Result run_pimpl_refactor_with_clang_tooling(const PimplRequest& request) {
        Result result;
        result.refactor_type = "pimpl";
        result.engine = "clang-libtooling";
        result.summary.class_name = request.class_name;

#if !BHA_HAVE_CLANG_TOOLING
        add_diagnostic(
            result,
            DiagnosticSeverity::Note,
            "This build of bha-refactor was compiled without Clang tooling support; falling back to text-based edits"
        );
        return result;
#else
        auto field_replacements = build_tooling_field_replacements(request, result);
        if (!field_replacements) {
            return result;
        }

        auto fallback_edits = bha::suggestions::generate_pimpl_refactor_edits(
            request.compile_commands_path,
            request.source_file,
            request.header_file,
            request.class_name
        );
        if (fallback_edits.is_err()) {
            add_diagnostic(result, DiagnosticSeverity::Error, fallback_edits.error().message());
            return result;
        }

        std::unordered_set<std::string> seen;
        std::unordered_set<std::string> seen_files;
        for (const auto& edit : fallback_edits.value()) {
            if (is_ast_rewritten_source_line(edit, request.source_file, request.class_name)) {
                continue;
            }
            if (auto replacement = to_replacement(edit)) {
                const auto key = replacement->file.string() + ":" + std::to_string(replacement->offset) +
                    ":" + std::to_string(replacement->length);
                if (seen.insert(key).second) {
                    if (seen_files.insert(replacement->file.string()).second) {
                        result.files.push_back(replacement->file);
                    }
                    result.replacements.push_back(std::move(*replacement));
                }
            } else {
                add_diagnostic(
                    result,
                    DiagnosticSeverity::Error,
                    "Failed to translate a generated structural edit into a byte-range replacement",
                    edit.file
                );
                return result;
            }
        }

        for (auto& replacement : *field_replacements) {
            const auto key = replacement.file.string() + ":" + std::to_string(replacement.offset) +
                ":" + std::to_string(replacement.length);
            if (seen.insert(key).second) {
                if (seen_files.insert(replacement.file.string()).second) {
                    result.files.push_back(replacement.file);
                }
                result.replacements.push_back(std::move(replacement));
            }
        }

        if (result.replacements.empty()) {
            add_diagnostic(
                result,
                DiagnosticSeverity::Error,
                "The Clang LibTooling refactor did not produce any replacements"
            );
            return result;
        }

        result.summary.copy_mode = "tooling-first";
        result.summary.rewritten_methods = field_replacements->size();
        result.validated_structure = true;
        result.success = true;
        return result;
#endif
    }

}  // namespace bha::refactor
