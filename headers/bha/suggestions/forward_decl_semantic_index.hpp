#ifndef BHA_FORWARD_DECL_SEMANTIC_INDEX_HPP
#define BHA_FORWARD_DECL_SEMANTIC_INDEX_HPP

#include "bha/project_index.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace bha::suggestions {

    struct ForwardDeclSemanticNamespace {
        std::string name;
        bool inline_namespace = false;
    };

    struct ForwardDeclSemanticUse {
        fs::path source_file;
        bool requires_complete_type = false;
        bool in_dependent_context = false;
        bool through_alias = false;
        bool through_template = false;
        bool macro_expanded = false;
    };

    struct ForwardDeclSemanticRecord {
        fs::path declaration_file;
        std::string qualified_name;
        std::string unqualified_name;
        std::string keyword;
        std::vector<ForwardDeclSemanticNamespace> namespaces;
        bool complete_definition = false;
        bool macro_generated = false;
        bool template_declaration = false;
        bool unsupported_scope = false;
        bool declaration_shape_conflict = false;
        std::vector<ForwardDeclSemanticUse> uses;
    };

    struct ForwardDeclSemanticInclude {
        fs::path including_file;
        fs::path included_file;
        std::string include_spelling;
        bool angled = false;
        std::size_t offset = 0;
        std::size_t length = 0;
        std::size_t line = 0;
        std::size_t col_start = 0;
        std::size_t col_end = 0;
    };

    struct ForwardDeclSemanticResult {
        std::vector<ForwardDeclSemanticRecord> records;
        std::vector<ForwardDeclSemanticInclude> includes;
        std::string diagnostic;
        bool available = false;
    };

    [[nodiscard]] ForwardDeclSemanticResult analyze_forward_declarations(
        ProjectIndex& project_index,
        const fs::path& header,
        const std::vector<CompilationUnit>& commands
    );

    [[nodiscard]] bool validate_forward_decl_replacements(
        ProjectIndex& project_index,
        const std::vector<CompilationUnit>& commands,
        const std::vector<ForwardDeclSemanticInclude>& includes,
        std::string_view replacement_text,
        std::string& diagnostic
    );

    [[nodiscard]] bool validate_header_split_replacements(
        ProjectIndex& project_index,
        const std::vector<CompilationUnit>& commands,
        const std::vector<ForwardDeclSemanticInclude>& includes,
        std::string_view replacement_text,
        const fs::path& generated_file,
        std::string_view generated_content,
        std::string& diagnostic
    );

    [[nodiscard]] bool validate_include_removal(
        ProjectIndex& project_index,
        const CompilationUnit& command,
        const fs::path& source_file,
        std::size_t include_line,
        std::string_view include_spelling,
        std::string& diagnostic
    );

}  // namespace bha::suggestions

#endif  // BHA_FORWARD_DECL_SEMANTIC_INDEX_HPP
