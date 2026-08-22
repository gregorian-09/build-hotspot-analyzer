#include "bha/refactor/pimpl_tooling.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

#if BHA_HAVE_CLANG_TOOLING
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/Basic/ExceptionSpecificationType.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CompilationDatabase.h>
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
            result.diagnostics.push_back({
                .severity = severity,
                .message = std::move(message),
                .file = std::move(file),
                .line = line
            });
        }

#if BHA_HAVE_CLANG_TOOLING
        fs::path normalized_path(const fs::path& path) {
            std::error_code error;
            const auto absolute = fs::absolute(path, error);
            return (error ? path : absolute).lexically_normal();
        }

        bool same_path(const fs::path& left, const fs::path& right) {
            return normalized_path(left) == normalized_path(right);
        }

        std::optional<std::string> read_file(const fs::path& path) {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                return std::nullopt;
            }
            return std::string(
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()
            );
        }

        std::optional<fs::path> source_path_for_location(
            const clang::SourceManager& source_manager,
            clang::SourceLocation location
        ) {
            if (location.isInvalid()) {
                return std::nullopt;
            }
            location = source_manager.getSpellingLoc(location);
            if (location.isInvalid() || location.isMacroID()) {
                return std::nullopt;
            }
            const auto filename = source_manager.getFilename(location);
            if (filename.empty()) {
                return std::nullopt;
            }
            return normalized_path(fs::path(filename.str()));
        }

        std::optional<std::size_t> offset_for_location(
            const clang::SourceManager& source_manager,
            clang::SourceLocation location
        ) {
            if (location.isInvalid() || location.isMacroID()) {
                return std::nullopt;
            }
            const auto spelling = source_manager.getSpellingLoc(location);
            if (spelling.isInvalid() || spelling.isMacroID()) {
                return std::nullopt;
            }
            return source_manager.getFileOffset(spelling);
        }

        std::optional<std::size_t> end_offset_for_token(
            const clang::SourceManager& source_manager,
            const clang::LangOptions& language_options,
            clang::SourceLocation location
        ) {
            if (location.isInvalid() || location.isMacroID()) {
                return std::nullopt;
            }
            const auto spelling = source_manager.getSpellingLoc(location);
            const auto end = clang::Lexer::getLocForEndOfToken(
                spelling,
                0,
                source_manager,
                language_options
            );
            return offset_for_location(source_manager, end);
        }

        std::optional<std::size_t> declaration_end_offset(
            const clang::SourceManager& source_manager,
            const clang::LangOptions& language_options,
            clang::SourceRange range
        ) {
            if (range.isInvalid() || range.getBegin().isMacroID() || range.getEnd().isMacroID()) {
                return std::nullopt;
            }
            const auto char_range = clang::Lexer::getAsCharRange(
                range,
                source_manager,
                language_options
            );
            if (char_range.isInvalid()) {
                return std::nullopt;
            }
            const auto end = offset_for_location(source_manager, char_range.getEnd());
            if (!end) {
                return std::nullopt;
            }
            const auto next = clang::Lexer::findNextToken(
                char_range.getEnd(),
                source_manager,
                language_options
            );
            if (next && next->getKind() == clang::tok::comma) {
                return std::nullopt;
            }
            return end;
        }

        std::optional<std::string> token_source_text(
            const clang::SourceManager& source_manager,
            const clang::LangOptions& language_options,
            clang::SourceRange range
        ) {
            if (range.isInvalid() || range.getBegin().isMacroID() || range.getEnd().isMacroID()) {
                return std::nullopt;
            }
            const auto text = clang::Lexer::getSourceText(
                clang::CharSourceRange::getTokenRange(range),
                source_manager,
                language_options
            );
            if (text.empty()) {
                return std::nullopt;
            }
            return text.str();
        }

        std::vector<std::string> compile_arguments(
            const fs::path& compile_commands_path,
            const fs::path& source_file,
            std::string& error_message
        ) {
            std::string database_error;
            auto database = clang::tooling::JSONCompilationDatabase::loadFromFile(
                compile_commands_path.string(),
                database_error,
                clang::tooling::JSONCommandLineSyntax::AutoDetect
            );
            if (!database) {
                error_message = database_error.empty()
                    ? "Failed to load the requested compilation database"
                    : database_error;
                return {};
            }

            const auto requested_source = normalized_path(source_file);
            std::optional<clang::tooling::CompileCommand> selected;
            for (const auto& command : database->getAllCompileCommands()) {
                fs::path candidate(command.Filename);
                if (candidate.is_relative()) {
                    candidate = fs::path(command.Directory) / candidate;
                }
                if (!same_path(candidate, requested_source)) {
                    continue;
                }
                if (selected.has_value()) {
                    error_message = "Multiple compilation commands matched the requested source file";
                    return {};
                }
                selected = command;
            }
            if (!selected) {
                error_message = "No compilation database entry matched the requested source file";
                return {};
            }

            std::vector<std::string> arguments;
            arguments.reserve(selected->CommandLine.size() + 2);
            arguments.push_back("-working-directory");
            arguments.push_back(selected->Directory);

            for (std::size_t index = 1; index < selected->CommandLine.size(); ++index) {
                const auto& argument = selected->CommandLine[index];
                if (argument == "-c" || argument == "/c" || argument == "-Winvalid-pch" ||
                    argument == "-ftime-trace" || argument.starts_with("-ftime-trace=") ||
                    argument == "/ftime-trace") {
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

                fs::path possible_source(argument);
                if (possible_source.is_relative()) {
                    possible_source = fs::path(selected->Directory) / possible_source;
                }
                if (same_path(possible_source, requested_source)) {
                    continue;
                }
                arguments.push_back(argument);
            }
            return arguments;
        }

        struct TargetRecordVisitor final
            : clang::RecursiveASTVisitor<TargetRecordVisitor> {
            explicit TargetRecordVisitor(std::string requested_name)
                : requested_name_(std::move(requested_name)) {}

            bool VisitCXXRecordDecl(clang::CXXRecordDecl* record) {
                if (record == nullptr || !record->isThisDeclarationADefinition() ||
                    record->getNameAsString().empty() || record->isInjectedClassName()) {
                    return true;
                }
                if (record->getQualifiedNameAsString() != requested_name_ &&
                    record->getNameAsString() != requested_name_) {
                    return true;
                }
                const auto* canonical = record->getCanonicalDecl();
                if (seen_.insert(canonical).second) {
                    matches_.push_back(record);
                }
                return true;
            }

            std::vector<const clang::CXXRecordDecl*> matches() const {
                return matches_;
            }

        private:
            std::string requested_name_;
            std::set<const clang::CXXRecordDecl*> seen_;
            std::vector<const clang::CXXRecordDecl*> matches_;
        };

        struct MethodSet {
            std::vector<const clang::CXXMethodDecl*> definitions;
            std::vector<const clang::CXXConstructorDecl*> constructors;
        };

        bool is_copy_operation(const clang::CXXMethodDecl& method) {
            return (llvm::isa<clang::CXXConstructorDecl>(&method) &&
                    llvm::cast<clang::CXXConstructorDecl>(&method)->isCopyConstructor()) ||
                method.isCopyAssignmentOperator();
        }

        bool is_move_operation(const clang::CXXMethodDecl& method) {
            return (llvm::isa<clang::CXXConstructorDecl>(&method) &&
                    llvm::cast<clang::CXXConstructorDecl>(&method)->isMoveConstructor()) ||
                method.isMoveAssignmentOperator();
        }

        bool supports_unique_ptr(const std::vector<std::string>& arguments) {
            for (const auto& argument : arguments) {
                if (argument == "-std=c++98" || argument == "-std=gnu++98" ||
                    argument == "-std=c++03" || argument == "-std=gnu++03" ||
                    argument == "/std:c++98" || argument == "/std:c++03") {
                    return false;
                }
            }
            return true;
        }

        bool is_target_source_method(
            const clang::SourceManager& source_manager,
            const clang::CXXMethodDecl& method,
            const fs::path& source_file
        ) {
            const auto path = source_path_for_location(source_manager, method.getLocation());
            return path.has_value() && same_path(*path, source_file);
        }

        bool has_written_source_definition(
            const clang::SourceManager& source_manager,
            const clang::CXXMethodDecl& method,
            const fs::path& source_file
        ) {
            const auto* definition = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(method.getDefinition());
            if (definition == nullptr || !is_target_source_method(source_manager, *definition, source_file)) {
                return false;
            }
            return definition->isOutOfLine();
        }

        std::optional<MethodSet> validate_record_shape(
            const clang::CXXRecordDecl& record,
            const clang::SourceManager& source_manager,
            const clang::LangOptions& language_options,
            const fs::path& header_file,
            const fs::path& source_file,
            Result& result
        ) {
            const auto reject = [&](std::string message, const fs::path& file = fs::path{})
                -> std::optional<MethodSet> {
                add_diagnostic(
                    result,
                    DiagnosticSeverity::Error,
                    std::move(message),
                    file.empty() ? header_file : file,
                    source_manager.getSpellingLineNumber(record.getLocation())
                );
                return std::nullopt;
            };

            if (record.getTagKind() != clang::TagTypeKind::Class) {
                return reject("Only class declarations are supported by the structural PIMPL refactor");
            }
            if (record.getLocation().isMacroID()) {
                return reject("Macro-generated class declarations are not supported");
            }
            const auto record_path = source_path_for_location(source_manager, record.getLocation());
            if (!record_path || !same_path(*record_path, header_file)) {
                return reject("The target class definition is not located in the requested header");
            }
            if (record.getDescribedClassTemplate() != nullptr ||
                record.getTemplateSpecializationKind() != clang::TSK_Undeclared) {
                return reject("Template classes and specializations are not supported by the structural PIMPL refactor");
            }
            if (record.getNumBases() != 0) {
                return reject("Classes with inheritance are not supported by the structural PIMPL refactor");
            }

            std::size_t private_fields = 0;
            for (const auto* field : record.fields()) {
                if (field == nullptr) {
                    continue;
                }
                if (field->getAccess() != clang::AS_private) {
                    return reject("Every data member must be a private field for this PIMPL transformation");
                }
                ++private_fields;
                if (field->getLocation().isMacroID() || field->isBitField() ||
                    field->isAnonymousStructOrUnion() || field->hasInClassInitializer() ||
                    field->hasAttrs()) {
                    return reject("Bit-fields, anonymous fields, macro fields, and in-class field initializers are unsupported");
                }
                if (!source_path_for_location(source_manager, field->getLocation()) ||
                    !same_path(*source_path_for_location(source_manager, field->getLocation()), header_file)) {
                    return reject("Every moved field must be spelled directly in the requested header");
                }
                if (!declaration_end_offset(
                        source_manager,
                        language_options,
                        field->getSourceRange())) {
                    return reject("Multiple-declarator field declarations are unsupported; declare each field separately");
                }
            }
            if (private_fields == 0) {
                return reject("The target class has no private non-static data members");
            }

            for (const auto* declaration : record.decls()) {
                if (llvm::isa<clang::FriendDecl>(declaration)) {
                    return reject("Friend declarations are unsupported because they can access moved fields outside target methods");
                }
            }

            MethodSet methods;
            bool has_user_constructor = false;
            bool has_deleted_copy_constructor = false;
            bool has_deleted_copy_assignment = false;
            for (const auto* method : record.methods()) {
                if (method == nullptr || method->isImplicit()) {
                    continue;
                }
                if (method->isVirtual()) {
                    return reject("Virtual members are unsupported by the structural PIMPL refactor");
                }
                if (method->getLocation().isMacroID()) {
                    return reject("Macro-generated member declarations are unsupported");
                }
                if (method->hasInlineBody() && !method->isDefaulted() && !method->isDeleted()) {
                    return reject("Inline member bodies are unsupported; all field-using methods must be out of line");
                }
                if (is_move_operation(*method)) {
                    return reject("Explicit move operations are unsupported because their ownership semantics require a dedicated transformation");
                }
                if (is_copy_operation(*method)) {
                    if (!method->isDeleted()) {
                        return reject("Copy operations must be explicitly deleted; preserving user-defined copy semantics requires a dedicated transformation");
                    }
                    if (llvm::isa<clang::CXXConstructorDecl>(method)) {
                        has_deleted_copy_constructor = true;
                    } else {
                        has_deleted_copy_assignment = true;
                    }
                    continue;
                }

                const auto* definition = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(method->getDefinition());
                if (definition == nullptr) {
                    return reject("Every non-copy member function must have a definition in the requested source file");
                }
                if (!has_written_source_definition(source_manager, *method, source_file)) {
                    return reject("Every member function definition must be out of line in the requested source file", source_file);
                }
                const bool is_destructor = llvm::isa<clang::CXXDestructorDecl>(definition);
                if ((definition->isDefaulted() || !definition->hasBody()) && !is_destructor) {
                    return reject("Defaulted or body-less member definitions are unsupported except for the destructor");
                }
                if (const auto* constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(definition)) {
                    has_user_constructor = true;
                    if (constructor->isDefaulted() || constructor->getBody() == nullptr ||
                        !llvm::isa<clang::CompoundStmt>(constructor->getBody())) {
                        return reject("Constructors must have an out-of-line compound body so PIMPL ownership can be initialized");
                    }
                    if (clang::isNoexceptExceptionSpec(constructor->getExceptionSpecType())) {
                        return reject("Noexcept constructors are unsupported because Impl allocation can throw");
                    }
                    if (constructor->init_begin() != constructor->init_end()) {
                        return reject("Constructor initializer lists are unsupported; field initialization must be moved explicitly");
                    }
                    methods.constructors.push_back(constructor);
                }
                methods.definitions.push_back(definition);
            }

            const auto* destructor = record.getDestructor();
            const auto* destructor_definition = destructor == nullptr
                ? nullptr
                : llvm::dyn_cast_or_null<clang::CXXDestructorDecl>(destructor->getDefinition());
            if (destructor == nullptr || destructor->isImplicit() || destructor->isDeleted() ||
                destructor_definition == nullptr ||
                !has_written_source_definition(source_manager, *destructor, source_file) ||
                !destructor_definition->isOutOfLine()) {
                return reject("The class must declare and define an out-of-line destructor so unique_ptr sees a complete Impl type");
            }
            if (!has_user_constructor) {
                return reject("The class must have an explicit out-of-line constructor so the Impl object is initialized");
            }
            if (!has_deleted_copy_constructor || !has_deleted_copy_assignment) {
                return reject("The class must explicitly delete both copy operations so adding unique_ptr does not change copy semantics");
            }

            if (destructor_definition->hasInlineBody() && !destructor_definition->isDefaulted()) {
                return reject("Inline destructors are unsupported by the structural PIMPL refactor");
            }
            return methods;
        }

        class FieldReferenceCollector final
            : public clang::RecursiveASTVisitor<FieldReferenceCollector> {
        public:
            FieldReferenceCollector(
                clang::ASTContext& context,
                const clang::CXXRecordDecl& record,
                const fs::path& source_file
            )
                : context_(context)
                , record_(record)
                , source_file_(normalized_path(source_file)) {
                for (const auto* field : record_.fields()) {
                    if (field != nullptr) {
                        fields_.insert(field);
                    }
                }
            }

            bool VisitMemberExpr(clang::MemberExpr* expression) {
                const auto* field = llvm::dyn_cast<clang::FieldDecl>(expression->getMemberDecl());
                if (field == nullptr || !fields_.contains(field)) {
                    return true;
                }

                const auto& source_manager = context_.getSourceManager();
                if (expression->getExprLoc().isMacroID() ||
                    !source_manager.isWrittenInSameFile(
                        source_manager.getSpellingLoc(expression->getExprLoc()),
                        source_manager.getSpellingLoc(expression->getMemberLoc())) ||
                    !source_path_for_location(source_manager, expression->getExprLoc()).has_value() ||
                    !same_path(*source_path_for_location(source_manager, expression->getExprLoc()), source_file_)) {
                    valid_ = false;
                    return true;
                }

                clang::SourceLocation begin = expression->getMemberLoc();
                if (!expression->isImplicitAccess()) {
                    const auto* base = expression->getBase();
                    const auto* this_expression = base == nullptr
                        ? nullptr
                        : llvm::dyn_cast<clang::CXXThisExpr>(base->IgnoreParenImpCasts());
                    if (this_expression == nullptr) {
                        valid_ = false;
                        return true;
                    }
                    begin = this_expression->getBeginLoc();
                }

                const auto start = offset_for_location(source_manager, begin);
                const auto end = end_offset_for_token(
                    source_manager,
                    context_.getLangOpts(),
                    expression->getMemberLoc()
                );
                if (!start || !end || *end < *start) {
                    valid_ = false;
                    return true;
                }

                Replacement replacement;
                replacement.file = source_file_;
                replacement.offset = *start;
                replacement.length = *end - *start;
                replacement.replacement_text = "pimpl_->" + field->getNameAsString();
                const auto key = std::to_string(replacement.offset) + ":" +
                    std::to_string(replacement.length);
                if (seen_.insert(key).second) {
                    replacements_.push_back(std::move(replacement));
                    methods_with_references_.insert(current_method_);
                }
                return true;
            }

            void set_current_method(const clang::CXXMethodDecl* method) {
                current_method_ = method;
            }

            [[nodiscard]] bool valid() const noexcept {
                return valid_;
            }

            [[nodiscard]] const std::vector<Replacement>& replacements() const noexcept {
                return replacements_;
            }

            [[nodiscard]] std::size_t rewritten_method_count() const noexcept {
                return methods_with_references_.size();
            }

        private:
            clang::ASTContext& context_;
            const clang::CXXRecordDecl& record_;
            fs::path source_file_;
            std::unordered_set<const clang::FieldDecl*> fields_;
            std::unordered_set<const clang::CXXMethodDecl*> methods_with_references_;
            std::unordered_set<std::string> seen_;
            std::vector<Replacement> replacements_;
            const clang::CXXMethodDecl* current_method_ = nullptr;
            bool valid_ = true;
        };

        std::optional<std::string> apply_file_replacements(
            std::string content,
            std::vector<Replacement> replacements
        ) {
            std::ranges::sort(replacements, [](const Replacement& left, const Replacement& right) {
                if (left.offset != right.offset) {
                    return left.offset > right.offset;
                }
                return left.length > right.length;
            });
            std::size_t previous_start = content.size();
            for (const auto& replacement : replacements) {
                if (replacement.offset > content.size() ||
                    replacement.length > content.size() - replacement.offset ||
                    replacement.offset + replacement.length > previous_start) {
                    return std::nullopt;
                }
                content.replace(replacement.offset, replacement.length, replacement.replacement_text);
                previous_start = replacement.offset;
            }
            return content;
        }

        std::optional<std::string> make_field_text(
            const clang::FieldDecl& field,
            const clang::SourceManager& source_manager,
            const clang::LangOptions& language_options
        ) {
            return token_source_text(source_manager, language_options, field.getSourceRange());
        }

        std::optional<std::vector<Replacement>> build_replacements(
            const PimplRequest& request,
            const std::vector<std::string>& arguments,
            Result& result
        ) {
            const auto source_code = read_file(request.source_file);
            if (!source_code) {
                add_diagnostic(result, DiagnosticSeverity::Error, "Failed to read the requested source file", request.source_file);
                return std::nullopt;
            }

            auto ast = clang::tooling::buildASTFromCodeWithArgs(
                *source_code,
                arguments,
                normalized_path(request.source_file).string()
            );
            if (!ast || ast->getDiagnostics().hasErrorOccurred()) {
                add_diagnostic(result, DiagnosticSeverity::Error, "Clang could not build a diagnostic-free AST for the requested source file", request.source_file);
                return std::nullopt;
            }

            auto& context = ast->getASTContext();
            auto& source_manager = context.getSourceManager();
            TargetRecordVisitor visitor(request.class_name);
            visitor.TraverseDecl(context.getTranslationUnitDecl());
            const auto matches = visitor.matches();
            const auto requested_header = normalized_path(request.header_file);
            std::vector<const clang::CXXRecordDecl*> header_matches;
            for (const auto* match : matches) {
                const auto path = source_path_for_location(source_manager, match->getLocation());
                if (path && same_path(*path, requested_header)) {
                    header_matches.push_back(match);
                }
            }
            if (header_matches.size() != 1) {
                add_diagnostic(
                    result,
                    DiagnosticSeverity::Error,
                    header_matches.empty()
                        ? "The requested class was not found in the requested header"
                        : "The requested class name is ambiguous in the requested header",
                    request.header_file
                );
                return std::nullopt;
            }
            const auto* record = header_matches.front();
            const auto methods = validate_record_shape(
                *record,
                source_manager,
                context.getLangOpts(),
                requested_header,
                normalized_path(request.source_file),
                result
            );
            if (!methods) {
                return std::nullopt;
            }

            std::vector<const clang::FieldDecl*> fields;
            for (const auto* field : record->fields()) {
                fields.push_back(field);
            }
            std::ranges::sort(fields, [&](const auto* left, const auto* right) {
                return offset_for_location(source_manager, left->getBeginLoc()).value_or(0) <
                    offset_for_location(source_manager, right->getBeginLoc()).value_or(0);
            });

            std::vector<Replacement> replacements;
            replacements.push_back({
                .file = requested_header,
                .offset = 0,
                .length = 0,
                .replacement_text = "#include <memory>\n"
            });

            for (std::size_t index = 0; index < fields.size(); ++index) {
                const auto* field = fields[index];
                const auto start = offset_for_location(source_manager, field->getBeginLoc());
                const auto end = declaration_end_offset(
                    source_manager,
                    context.getLangOpts(),
                    field->getSourceRange()
                );
                if (!start || !end) {
                    add_diagnostic(result, DiagnosticSeverity::Error, "Failed to establish an AST source range for a private field", request.header_file);
                    return std::nullopt;
                }
                replacements.push_back({
                    .file = requested_header,
                    .offset = *start,
                    .length = *end - *start,
                    .replacement_text = index == 0
                        ? "struct Impl;\n    std::unique_ptr<Impl> pimpl_"
                        : ""
                });
            }

            std::optional<std::size_t> first_method_offset;
            for (const auto* method : methods->definitions) {
                const auto path = source_path_for_location(source_manager, method->getLocation());
                const auto offset = offset_for_location(source_manager, method->getBeginLoc());
                if (!path || !same_path(*path, request.source_file) || !offset) {
                    add_diagnostic(result, DiagnosticSeverity::Error, "A target method definition is not located in the requested source file", request.source_file);
                    return std::nullopt;
                }
                if (!first_method_offset || *offset < *first_method_offset) {
                    first_method_offset = *offset;
                }
            }
            if (!first_method_offset) {
                add_diagnostic(result, DiagnosticSeverity::Error, "No source definition is available for the structural PIMPL implementation", request.source_file);
                return std::nullopt;
            }

            std::string impl_definition = "struct " + record->getQualifiedNameAsString() + "::Impl {\n";
            for (const auto* field : fields) {
                const auto field_text = make_field_text(*field, source_manager, context.getLangOpts());
                if (!field_text) {
                    add_diagnostic(result, DiagnosticSeverity::Error, "Failed to recover a source-spelled declaration for a private field", request.header_file);
                    return std::nullopt;
                }
                impl_definition += "    " + *field_text + ";\n";
            }
            impl_definition += "};\n\n";
            replacements.push_back({
                .file = normalized_path(request.source_file),
                .offset = *first_method_offset,
                .length = 0,
                .replacement_text = impl_definition
            });

            FieldReferenceCollector field_references(context, *record, request.source_file);
            for (const auto* method : methods->definitions) {
                if (!method->hasBody()) {
                    continue;
                }
                field_references.set_current_method(method);
                field_references.TraverseStmt(method->getBody());
            }
            if (!field_references.valid()) {
                add_diagnostic(result, DiagnosticSeverity::Error, "A private field is used through an unsupported non-this expression", request.source_file);
                return std::nullopt;
            }
            for (auto replacement : field_references.replacements()) {
                replacements.push_back(std::move(replacement));
            }

            for (const auto* constructor : methods->constructors) {
                const auto* body = constructor->getBody();
                const auto body_offset = offset_for_location(source_manager, body->getBeginLoc());
                if (!body_offset) {
                    add_diagnostic(result, DiagnosticSeverity::Error, "Failed to locate a constructor body for PIMPL ownership initialization", request.source_file);
                    return std::nullopt;
                }
                replacements.push_back({
                    .file = normalized_path(request.source_file),
                    .offset = *body_offset,
                    .length = 0,
                    .replacement_text = ": pimpl_(std::unique_ptr<Impl>(new Impl())) "
                });
            }

            result.summary.moved_private_fields = fields.size();
            result.summary.rewritten_methods = field_references.rewritten_method_count();
            result.summary.copy_mode = "explicitly-deleted-copy-operations";
            return replacements;
        }

        bool validate_post_edit_ast(
            const PimplRequest& request,
            const std::vector<std::string>& arguments,
            const std::vector<Replacement>& replacements,
            Result& result
        ) {
            const auto source = read_file(request.source_file);
            const auto header = read_file(request.header_file);
            if (!source || !header) {
                add_diagnostic(result, DiagnosticSeverity::Error, "Failed to read source files for post-edit AST validation");
                return false;
            }

            std::map<fs::path, std::vector<Replacement>> grouped;
            for (const auto& replacement : replacements) {
                grouped[normalized_path(replacement.file)].push_back(replacement);
            }
            const auto modified_source = apply_file_replacements(*source, grouped[normalized_path(request.source_file)]);
            const auto modified_header = apply_file_replacements(*header, grouped[normalized_path(request.header_file)]);
            if (!modified_source || !modified_header) {
                add_diagnostic(result, DiagnosticSeverity::Error, "Generated structural replacements overlap or exceed their original source ranges");
                return false;
            }

            clang::tooling::FileContentMappings mappings;
            mappings.emplace_back(normalized_path(request.header_file).string(), *modified_header);
            if (normalized_path(request.header_file) != request.header_file) {
                mappings.emplace_back(request.header_file.string(), *modified_header);
            }
            auto ast = clang::tooling::buildASTFromCodeWithArgs(
                *modified_source,
                arguments,
                normalized_path(request.source_file).string(),
                "clang-tool",
                std::make_shared<clang::PCHContainerOperations>(),
                clang::tooling::getClangStripDependencyFileAdjuster(),
                mappings
            );
            if (!ast || ast->getDiagnostics().hasErrorOccurred()) {
                add_diagnostic(result, DiagnosticSeverity::Error, "The generated PIMPL replacement set failed post-edit Clang AST validation", request.source_file);
                return false;
            }
            return true;
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
        result.engine = "clang-libtooling-structural";
        result.summary.class_name = request.class_name;
        result.allow_fallback = false;

#if !BHA_HAVE_CLANG_TOOLING
        add_diagnostic(
            result,
            DiagnosticSeverity::Error,
            "Clang LibTooling is required for structural PIMPL refactoring; no text-edit fallback is available"
        );
        return result;
#else
        if (request.class_name.empty() || request.source_file.empty() || request.header_file.empty()) {
            add_diagnostic(result, DiagnosticSeverity::Error, "A class name, source file, and header file are required");
            return result;
        }
        std::string compile_error;
        const auto arguments = compile_arguments(
            request.compile_commands_path,
            request.source_file,
            compile_error
        );
        if (arguments.empty()) {
            add_diagnostic(
                result,
                DiagnosticSeverity::Error,
                compile_error.empty() ? "Failed to load compilation arguments" : compile_error
            );
            return result;
        }
        if (!supports_unique_ptr(arguments)) {
            add_diagnostic(result, DiagnosticSeverity::Error, "The requested compile command uses a pre-C++11 language mode; structural PIMPL requires std::unique_ptr");
            return result;
        }

        const auto replacements = build_replacements(request, arguments, result);
        if (!replacements || replacements->empty()) {
            return result;
        }
        if (!validate_post_edit_ast(request, arguments, *replacements, result)) {
            return result;
        }

        std::set<fs::path> files;
        for (const auto& replacement : *replacements) {
            result.replacements.push_back(replacement);
            files.insert(normalized_path(replacement.file));
        }
        result.files.assign(files.begin(), files.end());
        result.validated_structure = true;
        result.success = true;
        return result;
#endif
    }

}  // namespace bha::refactor
